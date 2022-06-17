#include "VideoChannel.h"

VideoChannel::~VideoChannel() {

}

/**
 * 丢帧方法1：丢packet
 * @param q
 */
void dropAVPacket(queue<AVPacket *> &q){
    while (!q.empty()){
        AVPacket *packet = q.front();
        if(packet->flags!=AV_PKT_FLAG_KEY){
            // 非关键帧才丢帧
            BaseChannel::releaseAVPacket(&packet);
            q.pop();
        }else{
            break;
        }
    }
}

/**
 * 丢帧方法2：丢frame
 * @param q
 */
void dropAVFrame(queue<AVFrame *> &q){
    while (!q.empty()){
        AVFrame *frame = q.front();
        if(frame->flags!=AV_PKT_FLAG_KEY){
            // 非关键帧才丢帧
            BaseChannel::releaseAVFrame(&frame);
            q.pop();
        }else{
            break;
        }
    }
}

VideoChannel::VideoChannel(int stream_index, AVCodecContext *avCodecContext,int fps,AVRational time_base) :BaseChannel(stream_index,avCodecContext,time_base) {  // 父类初始化
    this->fps = fps;
    packets.setSyncCallback(dropAVPacket);
    frames.setSyncCallback(dropAVFrame);
}

void *task_video_decode(void *args){
    VideoChannel *videoChannel = static_cast<VideoChannel *>(args);
    videoChannel->_decode();
    return 0;
}

void VideoChannel::_decode() {
    AVPacket *packet = 0;
    while (isPlaying) {  // 消费者  （消耗 AVPacket）
        // 暂停
        if (isPause){
            continue;
        }

        if (isPlaying && frames.size() > 50) {
            // 暂时不生产，等待消费
            av_usleep(10 * 1000);  // 单位微妙
            continue;
        }

        int ret = packets.pop(packet);
        if (!isPlaying) {
            // 停止播放
            break;
        }
        if (ret <= 0) {
            continue;
        }
        // 取成功，数据包交给解码器解码
        int sendPacketState = avcodec_send_packet(avCodecContext, packet);
        if (sendPacketState == 0) {
            AVFrame *frame = av_frame_alloc();
            int receiveFrameState = avcodec_receive_frame(avCodecContext,frame);    // 生产者frame （消费AVPacket）
            if (receiveFrameState == 0) {
                frames.push(frame);  // 解码后数据放入队列    // BUG frames == null
            } else if (receiveFrameState == AVERROR(EAGAIN)) {
                continue;
            } else if (receiveFrameState != 0) {
                releaseAVFrame(&frame);
                break;
            }
        } else if (sendPacketState == AVERROR(EAGAIN)) {
            continue;
        } else if (sendPacketState != 0) {
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
void *task_video_play(void *args) {
    VideoChannel *videoChannel = static_cast<VideoChannel *>(args);
    videoChannel->_play();
    return 0;
}

void VideoChannel::_play() {
    AVFrame *frame = 0;
    AVPixelFormat dstFormat = AV_PIX_FMT_RGBA;
    // YUV转RGB上下文
    SwsContext *swsContext = sws_getContext(
            avCodecContext->width,avCodecContext->height,avCodecContext->pix_fmt,
            avCodecContext->width,avCodecContext->height,dstFormat,
            SWS_BILINEAR,NULL,NULL,NULL);

    // 分配buffer 显示RGB
    AVFrame *outFrame = av_frame_alloc();
    uint8_t *out_buffer = static_cast<uint8_t *>(av_malloc(
            (size_t) av_image_get_buffer_size(dstFormat, avCodecContext->width, avCodecContext->height, 1)));
    //根据指定的数据初始化/填充缓冲区
    av_image_fill_arrays(outFrame->data,outFrame->linesize,out_buffer,dstFormat,
                         avCodecContext->width,avCodecContext->height,1);
    // !BUG outFrame height, width is 0 这不是bug
    // BUG frame = null
//    av_image_alloc(outFrame->data,outFrame->linesize,frame->width,frame->height,AV_PIX_FMT_RGBA,1); // 给outframe填充宽，高，图像格式

    while (isPlaying){  // 消费者 （消费frame）
        // 暂停
        if (isPause){
            continue;
        }

        int ret = frames.pop(frame);
        if(!isPlaying){
            // 停止播放
            break;
        }
        if (ret<=0){
            continue;
        }
        // AVframe中数据YUV转RGBA
        sws_scale(swsContext,frame->data,frame->linesize,0,avCodecContext->height,
                outFrame->data,outFrame->linesize);

        // 控制延时时间
        // 帧特有的额外延时时间，相对与 avg_delay
        double xtra_delay = frame->repeat_pict / (2*fps);    // xtra_delay = repeat_pict / (2*fps)
        // 平均延时时间
        double avg_delay = 1.0/fps;
        double real_delay = xtra_delay + avg_delay;

//        av_usleep(real_delay*1000000); // 单位微妙  不与音频同步时

        // 与音频同步
        // 视频时间time_base
        double video_time = frame->best_effort_timestamp * av_q2d(time_base);
        // 音频时间
        if (audioChannel==NULL){
            av_usleep(real_delay*1000000); // 单位微妙  不与音频同步时 因为没有音频
            if (javaCallHelper!=NULL){
                // 没有音频传递，以视频进度时间为基准
                javaCallHelper->onProgress(video_time);
            }
        } else {
            // 有音频，同步以音频时间为基准
            double audioTime = audioChannel->audio_time;
            double diff = video_time - audioTime;
            if (diff>0){
                // 视频快于音频，等音频
                if (diff>1){ // 模拟快进
                    av_usleep((real_delay*2)*1000000);  // 单位微妙
                }else{
                    av_usleep((real_delay+diff)*1000000);  // 单位微妙
                }
            }else if(diff<0){
                // 音频快于视频，等视频
                // 丢帧
                if(abs(diff) >=0.05){
//                    packets.sync();
                    frames.sync();
                    continue;
                }
            }else{
                // 完美同步
            }
            javaCallHelper->onProgress(audioTime);
        }

        // 绘制RGBA图像
        // 设置回调参数
        // w,h,lineSize
        renderCallBack(outFrame->data[0],avCodecContext->width,avCodecContext->height,outFrame->linesize[0]);
        releaseAVFrame(&frame);
    }
    isPlaying = 0;
    av_frame_free(&frame);
    av_frame_free(&outFrame);
    sws_freeContext(swsContext);

}

void VideoChannel::setRenderCallBack(RenderCallBack renderCallBack) {
    this->renderCallBack = renderCallBack;
}

/**
 * 开始视频播放（解码和播放）
 * 开两个线程
 * 一个解码，一个播放
 */
void VideoChannel::start() {
    packets.setWork(1);  // 队列设置为工作状态
    frames.setWork(1);
    isPlaying = 1;

    pthread_create(&pid_decode, 0, task_video_decode, this);
    pthread_create(&pid_play,0,task_video_play, this);
}

int VideoChannel::setAudioChannel(AudioChannel *audioChannel) {
    if (audioChannel==NULL){
        return -1;
    }
    this->audioChannel = audioChannel;
    return 0;
}

void VideoChannel::stop() {
    packets.setWork(0);  // 队列设置为非工作状态
    frames.setWork(0);   // BUG frames == null
    isPlaying = 0;

    pthread_join(pid_decode,0);
    pthread_join(pid_play,0);
}

void VideoChannel::pause() {
    this->isPause = true;
}

void VideoChannel::keep_on() {
    this->isPause = false;
}






