#ifndef EASYPLAYER_AUDIOCHANNEL_H
#define EASYPLAYER_AUDIOCHANNEL_H

#include "BaseChannel.h"
#include "JavaCallHelper.h"
#include <SLES/OpenSLES.h>      // OpenSL 硬件音频加速
#include <SLES/OpenSLES_Android.h>

// 使用C语言库就需要extern "C"
extern "C"{
#include "libswresample/swresample.h"
#include "ffmpeg/libavutil/time.h"
}

typedef void(* RenderCallBack)(uint8_t *,int,int,int);  // 函数指针 返回类型（* 指针名）（参数1类型, 参数2类型, ...）

class AudioChannel : public BaseChannel{
public:
    AudioChannel(int stream_index, AVCodecContext *avCodecContext,AVRational time_base);
    ~AudioChannel();

    void start();

    void _decode();

    void _play();

    int getPCM();

    void pause();

    void keep_on();

    void stop();

    int out_sample_rate;
    int out_sample_size;
    int out_channels;
    int out_buffer_size;
    uint8_t *out_buffers = 0;


private:
    //引擎
    SLObjectItf engineObject = 0;
    //引擎接口
    SLEngineItf engineInterface = 0;
    //混音器
    SLObjectItf outputMixObject = 0;
    //播放器
    SLObjectItf bqPlayerObject = 0;
    //播放器接口
    SLPlayItf bqPlayerPlay = 0;
    //播放器队列接口
    SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue = 0;

    pthread_t pid_decode;
    pthread_t pid_play;
    bool isPlaying;

    SwrContext *swrContext;
};


#endif //EASYPLAYER_AUDIOCHANNEL_H
