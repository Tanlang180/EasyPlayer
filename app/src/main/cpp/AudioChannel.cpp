#include "AudioChannel.h"

AudioChannel::~AudioChannel() {
    DELETE(out_buffers);
    if (swrContext){
        swr_free(&swrContext);
        swrContext = 0;
    }
}

AudioChannel::AudioChannel(int stream_index, AVCodecContext *avCodecContext,AVRational time_base) : BaseChannel(
        stream_index, avCodecContext,time_base) {

    this->out_sample_rate = 44100; // 采样率44100，兼容性最好
    this->out_sample_size = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16); // 单个样本占字节数量
    this->out_channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO); // 输出声道数

    out_buffer_size= out_sample_rate * out_sample_size * out_channels;  // 输出缓冲区大小
    this->out_buffers = static_cast<uint8_t *>(malloc(out_buffer_size));

    memset(out_buffers,0,out_buffer_size); // 缓冲区初始化

    // 重采样上下文
    swrContext = swr_alloc_set_opts(
            0,
            AV_CH_LAYOUT_STEREO,
            AV_SAMPLE_FMT_S16,
            out_sample_rate,
            avCodecContext->channel_layout,
            avCodecContext->sample_fmt,
            avCodecContext->sample_rate,
            0,0);

    //初始化重采样上下文
    swr_init(swrContext);   // BUG 之前没添加
}

void *task_audio_decode(void *args){
    AudioChannel *audioChannel = static_cast<AudioChannel *>(args);
    audioChannel->_decode();
    return 0;
}

/**
 * 音频解码
 */
void AudioChannel::_decode() {
    AVPacket *packet = 0;
    while (isPlaying){
        // 暂停
        if (isPause){
            continue;
        }

        if (isPlaying && frames.size() >50){
            // 暂时不生产，等待消费
            av_usleep(10*1000);  // 单位微妙
            continue;
        }
        int ret = packets.pop(packet);
        if(!isPlaying){
            // 停止播放
            break;
        }

        if (ret<=0){
            continue;
        }
        // 取成功，数据包交给解码器解码
        int sendPacketState = avcodec_send_packet(avCodecContext,packet);
        if (sendPacketState==0){
            AVFrame *frame = av_frame_alloc();
            int receiveFrameState = avcodec_receive_frame(avCodecContext,frame);
            if(receiveFrameState==0){
                frames.push(frame);  // 解码后数据放入队列
            }else if (receiveFrameState == AVERROR(EAGAIN)) {
                continue;
            } else if (receiveFrameState!=0){
                releaseAVFrame(&frame);
                break;
            }
        }else if (sendPacketState==AVERROR(EAGAIN)){
            continue;
        }else if(sendPacketState!=0){
            break;
        }
    }
    releaseAVPacket(&packet);
}

/**
 * 视频播放
 * @param args
 * @return
 */
void *task_audio_play(void *args){
    AudioChannel *audioChannel = static_cast<AudioChannel *>(args);
    audioChannel->_play();
    return 0;
}

/**
 * 获取PCM数据，返回字节size
 * @return
 */
int AudioChannel::getPCM() {
    AVFrame *frame = 0;
    int pcm_size = 0;
    while (isPlaying){
        // 暂停
        if (isPause){
            continue;
        }

        int ret = frames.pop(frame);  // BUG frames == null
        if(!isPlaying){
            // 停止播放
            break;
        }
        if(ret<=0){
            continue;
        }

        // 重采样
        // a*b/c 新样本数 == 旧采样率 * 样本数  / 新采样率
        // 计算重采样输出的样本数
        int out_nb_samples = av_rescale_rnd(
                swr_get_delay(swrContext,frame->sample_rate) + frame->nb_samples,
                frame->sample_rate,out_sample_rate,AV_ROUND_UP
                );      // swr_get_delay 计算可能的尾巴
        //格式转换
        int samples_per_channel = swr_convert(swrContext, &out_buffers, out_nb_samples,
                    (const uint8_t **)(frame->data), frame->nb_samples);    // BUG ret -22
        //计算每个通道的样本字节size
        pcm_size = samples_per_channel * out_sample_size * out_channels;

        // 获取音频时间戳 audio_time需要被VideoChannel获取
        this->audio_time = frame->best_effort_timestamp * av_q2d(time_base);
        if(javaCallHelper!=NULL){
            javaCallHelper->onProgress(audio_time);
        }
        break;
    }
//    isPlaying = 0;  // Bug
    releaseAVFrame(&frame);
    return pcm_size;
}

//4.3 创建回调函数
void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *args) {
    AudioChannel *audioChannel = static_cast<AudioChannel *>(args);
    int pcm_size = audioChannel->getPCM(); // BUG size_pcm 《0
    if (pcm_size>0){
        (*bq)->Enqueue(bq,audioChannel->out_buffers,pcm_size);
    }
}

/**
 * 音频播放
 */
void AudioChannel::_play() {
    /**
     * 1. 创建引擎并获取引擎接口
     */
    SLresult result;
    // 1.1 创建引擎对象：SLObjectItf engineObject
    result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    if (SL_RESULT_SUCCESS != result) {
        return;
    }
    // 1.2 初始化引擎
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    if (SL_RESULT_SUCCESS != result) {
        return;
    }
    // 1.3 获取引擎接口 SLEngineItf engineInterface
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineInterface);
    if (SL_RESULT_SUCCESS != result) {
        return;
    }

    /**
     * 2。设置混音器
     */
    // 2.1 创建混音器：SLObjectItf outputMixObject
    result = (*engineInterface)->CreateOutputMix(engineInterface, &outputMixObject, 0,
                                                 0, 0);
    if (SL_RESULT_SUCCESS != result) {
        return;
    }
    // 2.2 初始化混音器
    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    if (SL_RESULT_SUCCESS != result) {
        return;
    }

    //不启用混响可以不用获取混音器接口
    // 获得混音器接口
    // 设置特效
    //result = (*outputMixObject)->GetInterface(outputMixObject, SL_IID_ENVIRONMENTALREVERB,
    //                                         &outputMixEnvironmentalReverb);
    //if (SL_RESULT_SUCCESS == result) {
    //设置混响 ： 默认。
    //SL_I3DL2_ENVIRONMENT_PRESET_ROOM: 室内
    //SL_I3DL2_ENVIRONMENT_PRESET_AUDITORIUM : 礼堂 等
    //const SLEnvironmentalReverbSettings settings = SL_I3DL2_ENVIRONMENT_PRESET_DEFAULT;
    //(*outputMixEnvironmentalReverb)->SetEnvironmentalReverbProperties(
    //       outputMixEnvironmentalReverb, &settings);
    //}


    /**
     * 3. 创建播放器
     */
    //3.1 配置输入声音信息
        //创建buffer缓冲类型的队列 2个队列
        SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
                                                           2};
        //pcm数据格式
        //SL_DATAFORMAT_PCM：数据格式为pcm格式
        //2：双声道
        //SL_SAMPLINGRATE_44_1：采样率为44100
        //SL_PCMSAMPLEFORMAT_FIXED_16：采样格式为16bit
        //SL_PCMSAMPLEFORMAT_FIXED_16：数据大小为16bit
        //SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT：左右声道（双声道）
        //SL_BYTEORDER_LITTLEENDIAN：小端模式
        SLDataFormat_PCM format_pcm = {SL_DATAFORMAT_PCM, 2, SL_SAMPLINGRATE_44_1,
                                       SL_PCMSAMPLEFORMAT_FIXED_16,
                                       SL_PCMSAMPLEFORMAT_FIXED_16,
                                       SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
                                       SL_BYTEORDER_LITTLEENDIAN};

        //数据源 将上述配置信息放到这个数据源中
        SLDataSource audioSrc = {&loc_bufq, &format_pcm};

    //3.2 配置音轨（输出）
        //设置混音器
        SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
        SLDataSink audioSnk = {&loc_outmix, NULL};
        //需要的接口 操作队列的接口
        const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};
        const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    //3.3 创建播放器
        result = (*engineInterface)->CreateAudioPlayer(engineInterface, &bqPlayerObject, &audioSrc, &audioSnk, 1, ids, req);
        if (SL_RESULT_SUCCESS != result) {
            return;
        }
    //3.4 初始化播放器：SLObjectItf bqPlayerObject
        result = (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);
        if (SL_RESULT_SUCCESS != result) {
            return;
        }
    //3.5 获取播放器接口：SLPlayItf bqPlayerPlay
        result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerPlay);
        if (SL_RESULT_SUCCESS != result) {
            return;
        }


    /**
     * 4. 设置播放回调函数
     */
    //4.1 获取播放器队列接口：SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue
    (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE, &bqPlayerBufferQueue);

    //4.2 设置回调 void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context)
    (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, bqPlayerCallback, this);

    /**
     * 5.设置播放器播放状态
     */
    (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);

    /**
     * 6.手动激活回调函数
     */
    bqPlayerCallback(bqPlayerBufferQueue, this);

    /**
     * 7.释放资源
     * 析构函数里
     */

}

/**
 * 开始视频播放（解码和播放）
 * 开两个线程
 * 一个解码，一个播放
 */
void AudioChannel::start() {
    packets.setWork(1);  // 队列设置为工作状态
    frames.setWork(1);   // BUG frames == null
    isPlaying = 1;

    pthread_create(&pid_decode, 0, task_audio_decode, this);
    pthread_create(&pid_play,0,task_audio_play, this);
}

/**
 * 停止播放
 */
void AudioChannel::stop(){
    packets.setWork(0);  // 队列设置为非工作状态
    frames.setWork(0);   // BUG frames == null
    isPlaying = 0;

    pthread_join(pid_decode,0);
    pthread_join(pid_play,0);

    //7.1 设置停止状态
    if (bqPlayerPlay) {
        (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_STOPPED);
        bqPlayerPlay = 0;
    }
    //7.2 销毁播放器
    if (bqPlayerObject) {
        (*bqPlayerObject)->Destroy(bqPlayerObject);
        bqPlayerObject = 0;
        bqPlayerBufferQueue = 0;
    }
    //7.3 销毁混音器
    if (outputMixObject) {
        (*outputMixObject)->Destroy(outputMixObject);
        outputMixObject = 0;
    }
    //7.4 销毁引擎
    if (engineObject) {
        (*engineObject)->Destroy(engineObject);
        engineObject = 0;
        engineInterface = 0;
    }
}

void AudioChannel::pause() {
    this->isPause = true;
}

void AudioChannel::keep_on(){
    this->isPause = false;
}

