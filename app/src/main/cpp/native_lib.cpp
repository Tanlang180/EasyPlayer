#include <jni.h>
#include <string.h>
#include "NEFFmpeg.h"
#include "JavaCallHelper.h"

extern "C"{
#include "ffmpeg/libavutil/avutil.h"
#include "ffmpeg/libavformat/avformat.h"
#include "ffmpeg/libswscale/swscale.h"
#include "ffmpeg/libavutil/imgutils.h"
#include "ffmpeg/libavcodec/avcodec.h"
#include "ffmpeg/libavutil/dict.h"
#include "ffmpeg/libavutil/frame.h"
#include <android/native_window_jni.h>
#include "ffmpeg/libavutil/time.h"
#include <zconf.h>

#include <android/log.h>
#define TAG_native_lib "ffmpeg_lib.native_startVideo_out"
#define LOGD_native_lib(...) __android_log_print(ANDROID_LOG_DEBUG, TAG_native_lib, __VA_ARGS__)
#define LOGI_native_lib(...) __android_log_print(ANDROID_LOG_INFO, TAG_native_lib, __VA_ARGS__)
#define LOGE_native_lib(...) __android_log_print(ANDROID_LOG_ERROR, TAG_native_lib, __VA_ARGS__)
}

/**
 * 打印ffmpeg info
 */
extern "C"{
#include "ffmpeg/libavutil/parseutils.h"
#include "ffmpeg/libavcodec/jni.h"
JNIEXPORT jstring JNICALL
Java_com_zt_easyplayer_ffmpeg_PlayerFunc_printInfo(JNIEnv *env, jobject thiz)  {
    char info[40000] = {0};
    sprintf(info,"ffmpeg version:%s\n",av_version_info());
    AVCodec *c_temp = av_codec_next(NULL);
    while (c_temp != NULL) {
        if (c_temp->decode != NULL) {
            sprintf(info, "%sdecode:", info);
        } else {
            sprintf(info, "%sencode:", info);
        }
        switch (c_temp->type) {
            case AVMEDIA_TYPE_VIDEO:
                sprintf(info, "%s(video):", info);
                break;
            case AVMEDIA_TYPE_AUDIO:
                sprintf(info, "%s(audio):", info);
                break;
            default:
                sprintf(info, "%s(other):", info);
                break;
        }
        sprintf(info, "%s[%s]\n", info, c_temp->name);
        c_temp = c_temp->next;
    }
    return env->NewStringUTF(info);
}
}

/**
 * 实现视频解码播放
 */
extern "C"{
JNIEXPORT void JNICALL
Java_com_zt_easyplayer_ffmpeg_PlayerFunc_native_1startVideo(JNIEnv *env, jobject thiz,
                                                            jstring path_, jobject surface) {

    // FFmpeg视频绘制 音频绘制
    // 解析获取char类型路径
    const char *path = env->GetStringUTFChars(path_, 0);
    // 注册解码器以及相关协议
//    av_register_all(); // deprecated
    // 初始化网络模块
    avformat_network_init();
    // avFormat总上下文
    AVFormatContext *avFormatContext = avformat_alloc_context();
    // 打开URL
    AVDictionary *opts = NULL;
    // 设置超时3s
    // 视频时长（单位：微秒us，转换为秒需要除以1000000）
    av_dict_set(&opts,"timeout","3000000",0);

    //强制指定AVFormatContext中AVInputFormat的。这个参数一般情况下可以设置为NULL，这样FFmpeg可以自动检测AVInputFormat。
    //输入文件的封装格式
    //av_find_input_format("avi")
    // ret为零 表示成功
    // 视频相关属性存入avFormatContext
    if (int ret = avformat_open_input(&avFormatContext, path, NULL, &opts)<0){
        char msg[512];
        av_make_error_string(msg,512,ret);
        LOGE_native_lib("Couldn't open input stream. Error(%d)(%s).\n", ret, msg);
        return;
    }

    // 视频流
    int video_stream_idx = -1;
    // Read packets of a media file to get stream information.
    if (avformat_find_stream_info(avFormatContext,NULL)<0) {
        LOGE_native_lib("Read the video stream failed!");
        return;
    }
    LOGD_native_lib("Current Video includes media numbers : %d",avFormatContext->nb_streams);
    for (int i=0;i<avFormatContext->nb_streams; ++i) {
        if(avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){    // 之前用了AVMEDIA_TYPE_AUDIO，所以播放不出视频
            video_stream_idx = i;
            break;
        }
    }
    if (video_stream_idx==-1){
        LOGE_native_lib("Not found the Video stream!");
        return;
    }

    //通过编解码器的id——codec_id 获取对应（视频）流解码器
    AVCodecParameters *codecParameters = avFormatContext->streams[video_stream_idx]->codecpar;
    // 解码器 h264
    AVCodec *dec = avcodec_find_decoder(codecParameters->codec_id);
    if (dec==NULL){
        LOGE_native_lib("Not found the decoder!");
        return;
    }

    // 通过解码分配一个解码器的上下文
    AVCodecContext *codecContext = avcodec_alloc_context3(dec);
    if (codecContext==NULL){
        LOGE_native_lib("Decoder context alloc failed!");
        return;
    }

    // 将解码器参数copy到解码器上下文
    if (avcodec_parameters_to_context(codecContext,codecParameters)<0){
        LOGE_native_lib("Decoder context fill failed");
        return;
    }

    // 打开解码器
    if (avcodec_open2(codecContext,dec,NULL)<0){
        LOGE_native_lib("Open decoder failed!");
        return;
    }

    AVPixelFormat dstFormat = AV_PIX_FMT_RGBA;
    //分配存储压缩数据的结构体对象AVPacket
    //如果是视频流，AVPacket会包含一帧的压缩数据。
    //但如果是音频则可能会包含多帧的压缩数据
    AVPacket *packet = av_packet_alloc();
    //分配解码后的每一数据信息的结构体（指针）YUV
    AVFrame *avFrame = av_frame_alloc();
    //分配最终显示出来的目标帧信息的结构体（指针）RGB
    AVFrame *outFrame = av_frame_alloc();

    //缓存区
    uint8_t *out_buffer = static_cast<uint8_t *>(av_malloc(
            (size_t) av_image_get_buffer_size(dstFormat, codecContext->width, codecContext->height, 1)));
    //根据指定的数据初始化/填充缓冲区
    av_image_fill_arrays(outFrame->data,outFrame->linesize,out_buffer,dstFormat,
                         codecContext->width,codecContext->height,1);

    // 初始化SwsContext
    /**
     * 重视速度：fast_bilinear, point
     * 重视质量：cubic,spline,lanczos
     */
    SwsContext *swsContext = sws_getContext(
            codecContext->width,codecContext->height,codecContext->pix_fmt,
            codecContext->width,codecContext->height,dstFormat,
                   SWS_BILINEAR,NULL,NULL,NULL);

    if (swsContext==NULL){
        LOGE_native_lib("SwsContext==NULL");
        return;
    }

    // android 原生绘制工具
    ANativeWindow *aNativeWindow = ANativeWindow_fromSurface(env,surface);
    // 定义绘图缓冲区
    ANativeWindow_Buffer outBuffer;
    // 通过设置宽高限制缓冲区中的像素数量，而非屏幕的物流显示尺寸
    //如果缓冲区与物理屏幕的显示尺寸不相符，则实际显示可能会是拉伸，或者被压缩的图像
    ANativeWindow_setBuffersGeometry(aNativeWindow,codecContext->width,codecContext->height,WINDOW_FORMAT_RGBA_8888);

    //循环读取数据流的下一帧
    while (av_read_frame(avFormatContext,packet)==0){   // 使用 == 0 表示读取帧成功 不然跳出循环
        if(packet->stream_index == video_stream_idx){
            int sendPacketState = avcodec_send_packet(codecContext,packet);
            if(sendPacketState==0){
                int receiveFrameState = avcodec_receive_frame(codecContext,avFrame);
                if (receiveFrameState==0){
                    // ANativeWindow上锁
                    ANativeWindow_lock(aNativeWindow,&outBuffer,NULL);

                    //格式转换  (const uint8_t *const *) avFrame->data
                    sws_scale(swsContext, avFrame->data,
                              avFrame->linesize, 0,avFrame->height,
                              outFrame->data, outFrame->linesize);

                    // 屏幕缓冲区的首地址
                    uint8_t *dst = static_cast<uint8_t *>(outBuffer.bits);
                    //获取一行RGBA字节数
                    int oneLineByte = outBuffer.stride * 4;

                    //解码后的像素数据首地址
                    //这里由于使用的是RGBA格式，所以解码图像数据只保存在data[0]中。但如果是YUV就会有data[0]
                    //data[1],data[2]
                    uint8_t *src = outFrame->data[0];
                    //复制一行内存的实际数量
                    int srcStride = outFrame->linesize[0];

                    // 内存拷贝
                    for (int i = 0; i < codecContext->height; i++) {
                        memcpy(dst + i * oneLineByte, src + i * srcStride, srcStride);
                    }

                    // ANativeWindow解锁
                    ANativeWindow_unlockAndPost(aNativeWindow);

                    //进行短暂休眠。如果休眠时间太长会导致播放的每帧画面有延迟感，如果短会有加速播放的感觉。
                    //一般一每秒60帧——16毫秒一帧的时间进行休眠
                    usleep (1000 * 20);  // 20ms
                }else if (receiveFrameState == AVERROR(EAGAIN)) {
                    LOGD_native_lib("从解码器-接收-数据失败：AVERROR(EAGAIN)");
                } else if (receiveFrameState == AVERROR_EOF) {
                    LOGD_native_lib("从解码器-接收-数据失败：AVERROR_EOF");
                } else if (receiveFrameState == AVERROR(EINVAL)) {
                    LOGD_native_lib("从解码器-接收-数据失败：AVERROR(EINVAL)");
                } else {
                    LOGD_native_lib("从解码器-接收-数据失败：未知");
                }
            }else if (sendPacketState == AVERROR(EAGAIN)) {//发送数据被拒绝，必须尝试先读取数据
                LOGE_native_lib("向解码器-发送-数据包失败：AVERROR(EAGAIN)");//解码器已经刷新数据但是没有新的数据包能发送给解码器
            } else if (sendPacketState == AVERROR_EOF) {
                LOGE_native_lib("向解码器-发送-数据失败：AVERROR_EOF");
            } else if (sendPacketState == AVERROR(EINVAL)) {//遍解码器没有打开，或者当前是编码器，也或者需要刷新数据
                LOGE_native_lib("向解码器-发送-数据失败：AVERROR(EINVAL)");
            } else if (sendPacketState == AVERROR(ENOMEM)) {//数据包无法压如解码器队列，也可能是解码器解码错误
                LOGE_native_lib("向解码器-发送-数据失败：AVERROR(ENOMEM)");
            } else {
                LOGE_native_lib("向解码器-发送-数据失败：未知");
            }
        }

    }
    // 内存释放
    ANativeWindow_release(aNativeWindow);
    av_frame_free(&outFrame);
    av_frame_free(&avFrame);
    av_packet_free(&packet);
    avcodec_free_context(&codecContext);
    avformat_close_input(&avFormatContext);
    avformat_free_context(avFormatContext);
    env->ReleaseStringUTFChars(path_, path);
}
}

/**
 * 实现音频解码播放
 */
extern "C"{
#include "ffmpeg/libswresample/swresample.h"
#include "ffmpeg/libswscale/swscale.h"
#include "ffmpeg/libavutil/channel_layout.h"
JNIEXPORT void JNICALL
Java_com_zt_easyplayer_ffmpeg_PlayerFunc_sound(JNIEnv *env, jobject thiz, jstring input_,
                                               jstring output_) {

    const char *input = env->GetStringUTFChars(input_, 0);
    const char *output = env->GetStringUTFChars(output_,0);

    // 初始化
//    av_register_all(); // deprecated
    avformat_network_init();
    // 总上下文
    AVFormatContext *avFormatContext = avformat_alloc_context();
    // 打开音频文件
    int ret=avformat_open_input(&avFormatContext, input, NULL, NULL);
    if (ret<0){
        char msg[512];
        av_make_error_string(msg,512,ret);
        LOGE_native_lib("Couldn't open input stream. Error(%d)(%s).\n", ret, msg);
        return;
    }
    // 检查媒体信息是否可获取
    if(avformat_find_stream_info(avFormatContext,NULL)<0){
        LOGE_native_lib("无法获取输入文件信息");
        return;
    }

    // 找到音频流
    // 音视频时长（单位微秒，10e-6s）
    int audio_stream_index = -1;
    for (int i = 0; i < avFormatContext->nb_streams; ++i) {
        if (avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            audio_stream_index = i;
            break;
        }
    }
    if(audio_stream_index==-1){
        LOGE_native_lib("找不到音频");
        return;
    }

    // 配置解码器
    AVCodecParameters *codecParameters = avFormatContext->streams[audio_stream_index]->codecpar;
    // 找到解码器
    AVCodec *dec = avcodec_find_decoder(codecParameters->codec_id);
    // 创建解码器上下文
    AVCodecContext *codecContext = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(codecContext,codecParameters);
    // 打开解码器
    if (avcodec_open2(codecContext,dec,NULL)<0){
        LOGE_native_lib("无法打开解码器");
        return;
    }

    /**
     * 转换音频格式
     */
    SwrContext *swrContext = swr_alloc();
    // 输入参数
    AVSampleFormat in_sample = codecContext->sample_fmt; // 采样格式 每帧音频数据占几个字节
    int in_sample_rate = codecContext->sample_rate;         // 采样频率
    uint64_t in_ch_layout = codecContext->channel_layout; // 通道布局格式 非通道数，但有对应通道数属性
    // 输出参数
    AVSampleFormat out_sample = AV_SAMPLE_FMT_S16; // 采样格式 每帧音频数据占几个字节
    int out_sample_rate = 44100;                    // 采样频率
    uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO; // 通道布局格式
    // 设置参数
    swr_alloc_set_opts(swrContext,
                       out_ch_layout,out_sample,out_sample_rate,
                       in_ch_layout,in_sample,in_sample_rate,
                       0,NULL);
    // 初始化转换器其他默认参数
    swr_init(swrContext);

    // 压缩数据
    AVPacket *packet = av_packet_alloc();
    // 解压数据
    AVFrame *frame = av_frame_alloc();

    // 输出buffer
    uint8_t *out_buffer = static_cast<uint8_t *>(av_malloc(2 * 44100));

    // 打开输出文件
    FILE *fp_pcm = fopen(output,"wb+");
    if(fp_pcm == NULL){
        LOGE_native_lib("打开文件失败");
        return;
    }

    while (av_read_frame(avFormatContext,packet)==0){
        int sendPacketState = avcodec_send_packet(codecContext,packet); // 给codec发送一个packet
        if (sendPacketState==0){
            int receiveFrameState = avcodec_receive_frame(codecContext,frame); //从codec中拿到解压数据frame
            if (receiveFrameState==0){
                // 转换格式
                swr_convert(swrContext,&out_buffer,2*44100,
                            (const uint8_t **)frame->data,frame->nb_samples);
                // 缓冲区大小
                int out_channel_nb = av_get_channel_layout_nb_channels(out_ch_layout);
                int out_buffer_size = av_samples_get_buffer_size(NULL,out_channel_nb,frame->nb_samples,out_sample,1);

                fwrite(out_buffer,1,out_buffer_size,fp_pcm);

            }else if (receiveFrameState == AVERROR(EAGAIN)) {
                LOGD_native_lib("从解码器-接收-数据失败：AVERROR(EAGAIN)");
            } else if (receiveFrameState == AVERROR_EOF) {
                LOGD_native_lib("从解码器-接收-数据失败：AVERROR_EOF");
            } else if (receiveFrameState == AVERROR(EINVAL)) {
                LOGD_native_lib("从解码器-接收-数据失败：AVERROR(EINVAL)");
            } else {
                LOGD_native_lib("从解码器-接收-数据失败：未知");
            }
        }else if (sendPacketState == AVERROR(EAGAIN)) {//发送数据被拒绝，必须尝试先读取数据
            LOGE_native_lib("向解码器-发送-数据包失败：AVERROR(EAGAIN)");//解码器已经刷新数据但是没有新的数据包能发送给解码器
        } else if (sendPacketState == AVERROR_EOF) {
            LOGE_native_lib("向解码器-发送-数据失败：AVERROR_EOF");
        } else if (sendPacketState == AVERROR(EINVAL)) {//遍解码器没有打开，或者当前是编码器，也或者需要刷新数据
            LOGE_native_lib("向解码器-发送-数据失败：AVERROR(EINVAL)");
        } else if (sendPacketState == AVERROR(ENOMEM)) {//数据包无法压如解码器队列，也可能是解码器解码错误
            LOGE_native_lib("向解码器-发送-数据失败：AVERROR(ENOMEM)");
        } else {
            LOGE_native_lib("向解码器-发送-数据失败：未知");
        }
    }

    // 释放内存
    fclose(fp_pcm);
    av_packet_free(&packet);
    av_frame_free(&frame);
    av_free(out_buffer);
    swr_free(&swrContext);
    avcodec_close(codecContext);
    avformat_close_input(&avFormatContext);
}
}


JavaVM * javaVm = 0;
NEFFmpeg *neffmpeg = 0;
ANativeWindow *window = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // 静态初始化

/**
 * 创建JavaVM
 */
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved){
    javaVm = vm;
    return JNI_VERSION_1_6;
}

/**
 * 渲染绘制
 * @param src_data
 * @param width
 * @param height
 * @param src_lineSize
 */
void render(uint8_t *src_data,int width,int height,int src_lineSize){
    // 加锁
    pthread_mutex_lock(&mutex);
    if(window==NULL){
        return;
    }
    // 设置窗口属性
    ANativeWindow_setBuffersGeometry(window,width,height,WINDOW_FORMAT_RGBA_8888);
    // 定义绘图缓冲区
    ANativeWindow_Buffer windowBuffer;
    if (ANativeWindow_lock(window,&windowBuffer,0)<0){  // BUG 进来release了
        ANativeWindow_release(window);
        window = 0;
        return;
    }

    // 内存拷贝
    uint8_t *dst_data = static_cast<uint8_t *>(windowBuffer.bits);
    int dst_lineSize = windowBuffer.stride * 4;
    for (int i = 0; i < windowBuffer.height; i++) {
        memcpy(dst_data + i * dst_lineSize, src_data+i*src_lineSize,dst_lineSize);
    }

    ANativeWindow_unlockAndPost(window);
    pthread_mutex_unlock(&mutex);
}

extern "C"{
JNIEXPORT void JNICALL
Java_com_zt_easyplayer_ffmpeg_PlayerFunc_prepareNative(JNIEnv *env, jobject thiz,
                                                       jstring data_source_) {
    const char* data_source = env->GetStringUTFChars(data_source_, 0);
    JavaCallHelper *javaCallHelper=  new JavaCallHelper(javaVm,env,thiz);

    neffmpeg = new NEFFmpeg(data_source,javaCallHelper);
    neffmpeg->setRenderCallBack(render);
    neffmpeg->prepare();
    env->ReleaseStringUTFChars(data_source_,data_source);
}
}

extern "C"
JNIEXPORT void JNICALL
Java_com_zt_easyplayer_ffmpeg_PlayerFunc_startNative(JNIEnv *env, jobject thiz) {
    if (neffmpeg!=NULL) {
        neffmpeg->start();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_zt_easyplayer_ffmpeg_PlayerFunc_stopNative(JNIEnv *env, jobject thiz) {
    if (neffmpeg!=NULL){
        neffmpeg->stop();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_zt_easyplayer_ffmpeg_PlayerFunc_releaseNative(JNIEnv *env, jobject thiz) {
    // 加锁
    pthread_mutex_lock(&mutex);
    if(window!=NULL){
        ANativeWindow_release(window);
        window = 0;
    }
    pthread_mutex_unlock(&mutex);
    DELETE(neffmpeg);

}

extern "C"
JNIEXPORT void JNICALL
Java_com_zt_easyplayer_ffmpeg_PlayerFunc_setSurfaceNative(JNIEnv *env, jobject thiz,
                                                          jobject surface) {
    // 加锁
    pthread_mutex_lock(&mutex);
    if(window!=NULL){
        ANativeWindow_release(window);
        window = 0;
    }
    window = ANativeWindow_fromSurface(env,surface);
    pthread_mutex_unlock(&mutex);
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_zt_easyplayer_ffmpeg_PlayerFunc_getDurationNative(JNIEnv *env, jobject thiz) {
    if (neffmpeg!=NULL) {
        return neffmpeg->getDuration();
    }
    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_zt_easyplayer_ffmpeg_PlayerFunc_seekNative(JNIEnv *env, jobject thiz, jint play_time) {
    if (neffmpeg!=NULL){
        neffmpeg->seek(play_time);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_zt_easyplayer_ffmpeg_PlayerFunc_pauseNative(JNIEnv *env, jobject thiz) {
    if (neffmpeg!=NULL){
        neffmpeg->pasuePlay();
    }
}
extern "C"
JNIEXPORT void JNICALL
Java_com_zt_easyplayer_ffmpeg_PlayerFunc_continuePlayNative(JNIEnv *env, jobject thiz) {
    if(neffmpeg!=NULL){
        neffmpeg->continuePlay();
    }
}