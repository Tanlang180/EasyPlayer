#include "NEFFmpeg.h"

NEFFmpeg::NEFFmpeg(const char* data_source_,JavaCallHelper *javaCallHelper_) {
//    this->data_source = data_source; // 可能悬空指针
    this->data_source = new char[strlen(data_source_)+1]; //重新分配一个内存 加上\0
    strcpy(this->data_source,data_source_);
    this->javaCallHelper = javaCallHelper_;
    pthread_mutex_init(&seek_mutex,0);
}

NEFFmpeg::~NEFFmpeg() {
    DELETE(data_source);
    DELETE(javaCallHelper);
    pthread_mutex_destroy(&seek_mutex);
}

void* task_prepare(void *args){
    NEFFmpeg *player = static_cast<NEFFmpeg *>(args);  // BUG -> null 函数指针传参失败
    player->_prepare();
    return 0;
}

void NEFFmpeg::_prepare() {
    /**
    * 1.打开媒体地址
    * 文件：io
    * 直播：网络流
    */
    avFormatContext = avformat_alloc_context();
    // 设置超时
    AVDictionary *dictionary = nullptr;
    av_dict_set(&dictionary,"timeout","5000000",0);

    int ret = avformat_open_input(&avFormatContext,this->data_source,NULL,&dictionary);
    if (ret<0){
        // 告诉用户错误信息
        char msg[512];
        av_make_error_string(msg,512,ret);
        LOGE_NEEFFmpeg("Couldn't open input stream. Error(%d)(%s).\n", ret, msg);
        return;
    }

    /**
     * 2.找流信息
     */
    if (avformat_find_stream_info(avFormatContext, NULL)<0){
        LOGE_NEEFFmpeg("Read the video stream failed!");
        return;
    }

    // 获取信息总时长
    duration = avFormatContext->duration / AV_TIME_BASE;

    /**
     * 3.遍历流
     */
    for (int i = 0; i < avFormatContext->nb_streams; ++i) {
        /**
         * 4.获取流信息
         */
        AVStream *avStream = avFormatContext->streams[i];

         /**
          * 5.获取流中参数
          */
        AVCodecParameters *avCodecParameters = avStream->codecpar;

        /**
         * 6.通过编解码器id,找到解码器
         */
        AVCodec *codec = avcodec_find_decoder(avCodecParameters->codec_id);
        if (codec==NULL){
            //返回错误信息
            LOGE_NEEFFmpeg("Not found the decoder!");
            return;
        }

        /**
         * 7.创建解码器上下文
         */
        avCodecContext = avcodec_alloc_context3(codec);
        if (avCodecContext==NULL){
            LOGE_NEEFFmpeg("Decoder context alloc failed!");
            return;
        }

        /**
         * 8.设置解码器上下文参数
         */
        if(avcodec_parameters_to_context(avCodecContext,avCodecParameters)<0){
            LOGE_NEEFFmpeg("Decoder context fill failed");
            return;
        }

        /**
         * 9.打开解码器
         */
         if (avcodec_open2(avCodecContext,codec,NULL)<0){
             LOGE_NEEFFmpeg("Open decoder failed!");
             return;
         }

         /**
          * 10. 获取流类型
          */
         AVRational time_base = avStream->time_base;
        if (avCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO){
            // 封面
            if(avStream->disposition == AV_DISPOSITION_ATTACHED_PIC){
                continue;
            }

            // 视频流
            AVRational avg_frame_rate = avStream->avg_frame_rate;  // 分子，分母
            // 转fps
            int fps = av_q2d(avg_frame_rate);     // 转double类型

            videoChannel = new VideoChannel(i,avCodecContext,fps,time_base);
            videoChannel->setRenderCallBack(renderCallBack);
            if(duration!=0){
                // 非直播
                videoChannel->setJavaCallHelper(javaCallHelper);
            }

        }else if (avCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO){
            // 音频流
            audioChannel = new AudioChannel(i,avCodecContext,time_base);
            if(duration!=0){
                // 非直播
                audioChannel->setJavaCallHelper(javaCallHelper);
            }
        }
    }

    /**
     * 11. 判断流中既没有 视频 也没 音频
     */
    if (audioChannel==NULL&&videoChannel==NULL){
        LOGE_NEEFFmpeg("没发现视频流和音频流");
        return;
    }

    // 准备工作做完了！ 通知java可以播了 （JNI）
    if(javaCallHelper!=NULL){
        javaCallHelper->onPrepared();
    }
}

/**
 * 准备工作
 */
void NEFFmpeg::prepare() {

    /**
     * 创建子线程
     * void* (*__start_routine)(void*) == run(){} 线程函数
     * 线程函数需要是静态方法
     */
     pthread_create(&pid_prepare, 0, task_prepare, this);  // this是传递的参数

}

void* task_start(void *args){
    NEFFmpeg *player = static_cast<NEFFmpeg *>(args);
    player->_start();
    return 0;
}

void NEFFmpeg::_start() {  // packet -> packets
    while (isPlaying){   // 此循环 生产者（生产AVPacket）  生产和消费者能力相近将能减少内存消耗
        // 条件判断控制队列数据长度
        if (videoChannel!=NULL && videoChannel->packets.size() > 50){
            // 暂时不生产，等待消费
            av_usleep(10*1000);  // 单位微妙
            continue;
        }

        AVPacket  *packet = av_packet_alloc();
        pthread_mutex_lock(&seek_mutex);
        int ret = av_read_frame(avFormatContext,packet);
        pthread_mutex_unlock(&seek_mutex);
        if (ret>=0) {
            // 读取成功判断 流类型
            if (videoChannel != NULL && packet->stream_index == videoChannel->stream_index) {
                // 视频 packet进入队列
                videoChannel->packets.push(packet);
            } else if (audioChannel != NULL && packet->stream_index == audioChannel->stream_index) {
                // 音频 packet进入队列
                audioChannel->packets.push(packet);  // Bug 错写 videoChannel
            }else if (ret==AVERROR_EOF){
                // 读到音视频流尾部
                // 考虑是否播放完
                if(audioChannel->packets.empty() && audioChannel->frames.empty()
                    &&videoChannel->packets.empty() && videoChannel->frames.empty()){
                    // 播放完毕
                    av_packet_free(&packet);
                    break;
                }
                LOGI_NEEFFmpeg("Stream id:%d is read over.",packet->stream_index);
                break;
            }else {
                // 出错了
                // 告诉用户错误信息
                char msg[512];
                av_make_error_string(msg,512,ret);
                LOGE_NEEFFmpeg("av_read_frame :: Error(%d)(%s).\n", ret, msg);
                break;
            }
        }
    }
    isPlaying = 0;
    audioChannel->stop();
    videoChannel->stop();

}

/**
 * 开始播放
 * 一个线程解码 在另外一个线程播放
 */
void NEFFmpeg::start() {
    isPlaying = true;
    if (videoChannel!=NULL){
        videoChannel->start();      // packet -> frame -> 播放
        videoChannel->setAudioChannel(audioChannel);  // 有可能是NULL
    }
    if (audioChannel!=NULL){
        audioChannel->start();      // packet -> frame -> 播放
    }

    pthread_create(&pid_start,NULL,task_start,this);  // packet -> packets
}

void NEFFmpeg::setRenderCallBack(RenderCallBack renderCallBack) {
    this->renderCallBack = renderCallBack;
}

int NEFFmpeg::getDuration() {
    return duration;
}

/**
 * 从指定位置重新播放
 * play_time: 播放进度时间
 * @param play_time
 */
void NEFFmpeg::seek(int play_time) {
    if (play_time<0 || play_time>duration){
        return;
    }
    if (audioChannel==NULL && videoChannel==NULL){
        return;
    }
    if (avFormatContext==NULL){
        return;
    }

    pthread_mutex_lock(&seek_mutex);

    int ret = av_seek_frame(avFormatContext,-1,play_time*AV_TIME_BASE,AVSEEK_FLAG_BACKWARD);  //  按时间戳找关键帧  flag向前找关键帧
    if (ret<0){
        LOGE_NEEFFmpeg("seek error!");
        return;
    }

    // 队列中可能存在未消费的数据，需要将队列重置
    if (audioChannel!=NULL){
        audioChannel->packets.setWork(0);
        audioChannel->frames.setWork(0);
        audioChannel->packets.clear();
        audioChannel->frames.clear();
        audioChannel->packets.setWork(1);
        audioChannel->frames.setWork(1);
    }
    if (videoChannel!=NULL){
        videoChannel->packets.setWork(0);
        videoChannel->frames.setWork(0);
        videoChannel->packets.clear();
        videoChannel->frames.clear();
        videoChannel->packets.setWork(1);
        videoChannel->frames.setWork(1);
    }

    pthread_mutex_unlock(&seek_mutex);
}

void *task_stop(void *args){
    NEFFmpeg *player = static_cast<NEFFmpeg *>(args); // 友元函数
    player->javaCallHelper = 0;
    player->isPlaying = false;
    pthread_join(player->pid_prepare,0);// pthread_join 会引发ANR
    pthread_join(player->pid_start,0);

    if (player->avFormatContext){
        avformat_close_input(&player->avFormatContext);
        avformat_free_context(player->avFormatContext);
        player->avFormatContext = 0;
    }

    DELETE(player->audioChannel);
    DELETE(player->videoChannel);
    DELETE(player);
    return 0; // 指针函数一定要记得return 0
}

void NEFFmpeg::stop() {
    pthread_create(&pid_stop,0,task_stop,this);
}

void NEFFmpeg::release() {

}

void NEFFmpeg::pasuePlay(){
    if(videoChannel!=NULL){
        videoChannel->pause();
    }
    if (audioChannel!=NULL){
        audioChannel->pause();
    }

}

void NEFFmpeg::continuePlay() {
    if(videoChannel!=NULL){
        videoChannel->keep_on();
    }
    if (audioChannel!=NULL){
        audioChannel->keep_on();
    }
}







