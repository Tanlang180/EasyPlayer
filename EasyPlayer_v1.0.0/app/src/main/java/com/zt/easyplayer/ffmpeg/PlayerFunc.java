package com.zt.easyplayer.ffmpeg;

import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import androidx.annotation.NonNull;

import java.io.File;

/**
 * author : Tan Lang
 * e-mail : 2715009907@qq.com
 * date   : 2022/5/22-21:13
 * desc   :
 * version: 1.0
 */
public class PlayerFunc implements SurfaceHolder.Callback {

    private String TAG = "WangYiPlayer";
    private String dataSource;
    private OnPreparedListener onPreparedListener;
    private OnProgressListener onProgressListener;

    public PlayerFunc() {

    }

    static {
        System.loadLibrary("ffmpeg_lib");
    }

    private SurfaceHolder surfaceHolder;

    // 绘制NDK path surfaceView
    public void setSurfaceView(SurfaceView surfaceView){
        if (null!=this.surfaceHolder){
            this.surfaceHolder.removeCallback(this);
        }
        this.surfaceHolder = surfaceView.getHolder();
        this.surfaceHolder.addCallback(this);
    }

    public void setDataSource(File externalStorageDirectory, String path){

        File file = new File(externalStorageDirectory,path);
        if(file.exists()){
            this.dataSource = new File(externalStorageDirectory,path).getAbsolutePath();
        }

    }

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder surfaceHolder) {

    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder surfaceHolder, int i, int i1, int i2) {
        setSurfaceNative(surfaceHolder.getSurface());
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder surfaceHolder) {

    }

    // 视频解码并播放
    public void startVideo(String path) {
        native_startVideo(path,this.surfaceHolder.getSurface());
    }

    /**
     * 播放前准备工作
     */
    public void prepare(){
        prepareNative(this.dataSource);
    }

    /**
     * 开始直播
     */
    public void start(){
        startNative();
    }

    /**
     * 暂停
     */
    public void pause() {
        pauseNative();
    }

    public void continuePlay() {
        continuePlayNative();
    }

    /**
     * 停止直播
     */
    public void stop(){
        stopNative();
    }

    /**
     * 资源释放
     */
    public void release(){
        releaseNative();
    }

    public void setOnprepareLisener(OnPreparedListener onPreparedListener) {
        this.onPreparedListener = onPreparedListener;
    }

    public void seek(int playTime) {
        seekNative(playTime);
    }

    /**
     * JNI反射调用的
     */
    public void onPrepared(){
        onPreparedListener.onPrepared();
    }

    public interface OnPreparedListener{
        public void onPrepared();
    }

    /**
     * JNI反射调用的
     */
    public void onProgress(int progress){
        if (null!=this.onProgressListener){
            this.onProgressListener.onProgress(progress);
        }
    }

    public void setProgressListener(OnProgressListener onProgressListener) {
        this.onProgressListener = onProgressListener;
    }

    public interface OnProgressListener{
        public void onProgress(int progress);
    }

    /**
     * 获取总时长
     * @return
     */
    public int getDuration(){
        return getDurationNative();
    }

    // 打印ffmpeg info
    public native String printInfo();

    public native int getDurationNative();

    public native void setSurfaceNative(Surface surface);

    public native void native_startVideo(String path, Surface surface);

    // 音频解码为pcm数据
    // input.mp3 output.pcm
    public native void sound(String input,String output);

    private native void prepareNative(String dataSource);

    private native void startNative();

    private native void pauseNative();

    private native void continuePlayNative();

    private native void stopNative();

    private native void releaseNative();

    private native void seekNative(int playTime);
}
