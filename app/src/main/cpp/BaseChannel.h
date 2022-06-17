#ifndef EASYPLAYER_BASECHANNEL_H
#define EASYPLAYER_BASECHANNEL_H
#include "safe_queue.h"
#include "JavaCallHelper.h"

#define DELETE(object) if(object) {delete object; object=0;}
extern "C"{
#include "libavcodec/avcodec.h"
#include "libavutil/imgutils.h"
#include <android/log.h>
#define LOG_TAG "BaseChannel"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
};

class BaseChannel {
public:
    BaseChannel(int stream_index, AVCodecContext *avCodecContext,AVRational time_base) : stream_index(stream_index),avCodecContext(avCodecContext),time_base(time_base){   // 这里是赋值的意思  stream_index 赋值给stream_index, 多参赋值时加','隔开
        packets.setReleaseCallback(releaseAVPacket);
        frames.setReleaseCallback(releaseAVFrame);
    }
    ~BaseChannel(){
        packets.clear();
    }

    /**
     * 释放AVPacket
     * @param packet
     */
    static void releaseAVPacket(AVPacket **packet){
        if (*packet!= nullptr){
            av_packet_free(packet);
            packet = 0;
        }
    }

    static void releaseAVFrame(AVFrame **frame){
        if (*frame!= nullptr){
            av_frame_free(frame);
            frame = 0;
        }
    }

    void setJavaCallHelper(JavaCallHelper *javaCallHelper){
        this->javaCallHelper = javaCallHelper;
    }

    int stream_index = 0;
//    类型<泛型 *>
    SafeQueue<AVPacket *> packets; // 待解码数据包队列
    SafeQueue<AVFrame *> frames; // 解码后数据包队列

    AVCodecContext *avCodecContext = 0;

    double audio_time;
    AVRational time_base;
    JavaCallHelper *javaCallHelper;
    bool isPause;
    pthread_t pid_pause;
};
#endif //EASYPLAYER_BASECHANNEL_H
