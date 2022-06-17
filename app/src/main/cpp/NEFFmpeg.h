#ifndef EASYPLAYER_NEFFMPEG_H
#define EASYPLAYER_NEFFMPEG_H
#include <threads.h>
#include <string.h>
#include <jni.h>

#include "AudioChannel.h"
#include "VideoChannel.h"
#include "JavaCallHelper.h"


// 使用C语言库就需要extern "C"
extern "C"{
#include "ffmpeg/libavutil/avutil.h"
#include "ffmpeg/libavutil/dict.h"
#include "ffmpeg/libavutil/error.h"
#include "ffmpeg/libavformat/avformat.h"
#include "ffmpeg/libavcodec/avcodec.h"
#include "ffmpeg/libavutil/time.h"
#include <android/log.h>
#define TAG_NEEFFmpeg "NEEFFmpeg"
#define LOGD_NEEFFmpeg(...) __android_log_print(ANDROID_LOG_DEBUG, TAG_NEEFFmpeg, __VA_ARGS__)
#define LOGI_NEEFFmpeg(...) __android_log_print(ANDROID_LOG_INFO, TAG_NEEFFmpeg, __VA_ARGS__)
#define LOGE_NEEFFmpeg(...) __android_log_print(ANDROID_LOG_ERROR, TAG_NEEFFmpeg, __VA_ARGS__)
}

class NEFFmpeg {
    friend void *task_stop(void *args);
public:
    NEFFmpeg(const char* data_source,JavaCallHelper *javaCallHelper);
    ~NEFFmpeg();

    void prepare();
    void _prepare();

    void start();
    void _start();

    void setRenderCallBack(RenderCallBack renderCallBack);

    int getDuration();

    void seek(int play_time);

    void stop();

    void release();

    void pasuePlay();

    void continuePlay();

private:
    char *data_source;
    pthread_t pid_prepare;  // 线程id
    pthread_t pid_start;  // 线程id
    pthread_t pid_pause;
    pthread_t pid_stop;

    AudioChannel* audioChannel = 0;
    VideoChannel* videoChannel = 0;
    JavaCallHelper* javaCallHelper = 0;

    AVFormatContext *avFormatContext = 0;
    AVCodecContext *avCodecContext = 0;

    bool isPlaying;
    RenderCallBack renderCallBack;
    int duration;
    pthread_mutex_t seek_mutex;

};


#endif //EASYPLAYER_NEFFMPEG_H
