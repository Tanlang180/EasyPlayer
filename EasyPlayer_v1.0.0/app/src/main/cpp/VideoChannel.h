#ifndef EASYPLAYER_VIDEOCHANNEL_H
#define EASYPLAYER_VIDEOCHANNEL_H

#include "BaseChannel.h"
#include "AudioChannel.h"
// 使用C语言库就需要extern "C"
extern "C"{
#include "libswscale/swscale.h"
#include "ffmpeg/libavutil/time.h"
}

typedef void(* RenderCallBack)(uint8_t *,int,int,int);  // 函数指针 返回类型（* 指针名）（参数1类型, 参数2类型, ...）

class VideoChannel : public BaseChannel{
public:
    VideoChannel(int stream_index, AVCodecContext *avCodecContext,int fps,AVRational time_base);
    ~VideoChannel();

    void start();

    void _decode();

    void _play();

    void setRenderCallBack(RenderCallBack renderCallBack);

    int setAudioChannel(AudioChannel *pChannel);

    void pause();

    void keep_on();

    void stop();

private:
    pthread_t pid_decode;
    pthread_t pid_play;
    RenderCallBack renderCallBack;
    bool isPlaying;
    int fps;
    AudioChannel *audioChannel;
};

#endif //EASYPLAYER_VIDEOCHANNEL_H
