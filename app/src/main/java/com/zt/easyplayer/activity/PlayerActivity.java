package com.zt.easyplayer.activity;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Environment;

import android.view.LayoutInflater;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.Fragment;

import com.zt.easyplayer.R;
import com.zt.easyplayer.ffmpeg.PlayerFunc;

/**
 * author : Tan Lang
 * e-mail : 2715009907@qq.com
 * date   : 2022/5/26-10:48
 * desc   :
 * version: 1.0
 */
public class PlayerActivity extends Fragment implements View.OnClickListener, SeekBar.OnSeekBarChangeListener {

    String TAG = "PlayerActivity";

    private View mView;
    private PlayerFunc playerFunc;
    private int duration;
    private TextView tv_time;
    private SeekBar seekBar;
    private Button startBn;
    private ImageButton startImageBtn;
    private boolean isSeek;
    private boolean isTouch; // 手拖拽进度条
    private boolean isPrepared;

    private int WRITE_READ_PERMISSION = 0X20;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {
        mView = inflater.inflate(R.layout.activity_player,container,false);
        requestPermissions();
        initParams();
        return mView;
    }

    @SuppressLint("WrongConstant")
    public void initParams(){
        SurfaceView surfaceView = mView.findViewById(R.id.surfaceView);
        playerFunc = new PlayerFunc();

        // surfaceView设置
        surfaceView.setRotation(90); // 默认是旋转了-90度
        playerFunc.setSurfaceView(surfaceView);

        // 按钮设置
//        startBn = mView.findViewById(R.id.startBN);
//        startBn.setOnClickListener(this);
        startImageBtn = mView.findViewById(R.id.startImageBtn);
        startImageBtn.setOnClickListener(this);

        // tvTime
        tv_time = mView.findViewById(R.id.tv_time);

        // seekBar
        seekBar = mView.findViewById(R.id.seekBar);
        seekBar.setOnSeekBarChangeListener(this);

        // 信息打印
//        TextView textView = mView.findViewById(R.id.textInfo);
//        textView.setText(wangYiPlayer.printInfo());
        Toast.makeText(getContext(),playerFunc.printInfo(),2).show();

        // 数据源设置
        playerFunc.setDataSource(Environment.getExternalStorageDirectory(),"atestfile/input.mp4");

        playerFunc.setOnprepareLisener(new PlayerFunc.OnPreparedListener() {
            @Override
            public void onPrepared() {
                // 也可以 放到一个新的线程里执行 匿名线程

                // Bug因为android中相关view和控件操作都不是线程安全，所以android禁止在非ui线程中操作ui
                // 两种方法在子线程中操作UI
                //
                isPrepared = true;
                getActivity().runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        duration = playerFunc.getDuration();
                        if (duration!=0){
                            // 本地视频文件
                            tv_time.setText("00:00/"+getMinutes(duration)+":"+getSeconds(duration));
                            tv_time.setVisibility(View.VISIBLE);
                            seekBar.setVisibility(View.VISIBLE);
                        }
                    }
                });

                getActivity().runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        Toast.makeText(getContext(),"开始播放！",Toast.LENGTH_SHORT).show(); // BUG 没有设置Looper.prepare 线程之间通信
                        playerFunc.start();
                    }
                });
            }
        });
        // 进度监听
        playerFunc.setProgressListener(new PlayerFunc.OnProgressListener() {
            /**
             * progress 是底层获取到的进度（单位 秒）
             * @param progress
             */
            @Override
            public void onProgress(int progress) {
                getActivity().runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        if (!isTouch){
                            // 进度转为百分比
                            if (duration!=0){
                                tv_time.setText(getMinutes(progress)+":"+getSeconds(progress)+"/"
                                        +getMinutes(duration)+":"+getSeconds(duration));
                                seekBar.setProgress(progress*100/duration);
                            }
                        }
                    }
                });
            }
        });

    }

    public String getMinutes(int duration){
        int minutes = duration / 60;
        if(minutes<=9){
            return "0" + minutes;
        }
        return "" + minutes;
    }

    public String getSeconds(int duration){
        int seconds = duration % 60;
        if (seconds<=9){
            return "0" + seconds;
        }
        return "" + seconds;
    }

    @Override
    public void onResume() {
        super.onResume();
    }

    @Override
    public void onStop() {
        super.onStop();
        playerFunc.stop();
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        playerFunc.release();
    }

    private void requestPermissions(){
        String write = Manifest.permission.WRITE_EXTERNAL_STORAGE;
        String read = Manifest.permission.READ_EXTERNAL_STORAGE;

        String[] write_read_permission = new String[] {write,read};

        int checkWrite = ContextCompat.checkSelfPermission(getContext(),write);
        int checkRead = ContextCompat.checkSelfPermission(getContext(),read);
        int ok = PackageManager.PERMISSION_GRANTED;

        if(checkWrite!=ok && checkRead!=ok){
            // 重新申请权限
            ActivityCompat.requestPermissions(this.getActivity(),write_read_permission,WRITE_READ_PERMISSION);
        }
    }

    @Override
    public void onClick(View view) {
        switch (view.getId()){
            case R.id.startImageBtn:
                if (!startImageBtn.isSelected() && !isPrepared){
                    startImageBtn.setSelected(true);
                    playerFunc.prepare();
                }else if(!startImageBtn.isSelected() && isPrepared){
                    startImageBtn.setSelected(true);
                    playerFunc.continuePlay();
                }else if (startImageBtn.isSelected()) {
                    startImageBtn.setSelected(false);
                    playerFunc.pause();
                }
                break;
        }
    }

    /**
     * 拖动进度时候，刷新进度时间
     * @param seekBar
     * @param progress  百分比的进度
     * @param fromUser
     */
    @Override
    public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
        if (fromUser){
            tv_time.setText(getMinutes(progress*duration/100)+":"+getSeconds(progress*duration/100)+"/"
                    +getMinutes(duration)+":"+getSeconds(duration));
            seekBar.setProgress(progress);
        }
    }

    /**
     * 开始拖拽
     * @param seekBar
     */
    @Override
    public void onStartTrackingTouch(SeekBar seekBar) {
        isTouch=true;
    }

    /**
     * 松手
     * @param seekBar
     */
    @Override
    public void onStopTrackingTouch(SeekBar seekBar) {
        isTouch = false;
        int progress = seekBar.getProgress();   // 百分比记录
        // 转成时间戳
        int playTime = progress * duration / 100;
        playerFunc.seek(playTime);  // 单位秒
    }
}
