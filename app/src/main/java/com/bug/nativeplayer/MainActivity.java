package com.bug.nativeplayer;


import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

public class MainActivity extends AppCompatActivity implements SurfaceHolder.Callback {

    /*
    *  说明：先在SDCard根目录下放一个名为“testfile.mp4”的文件
    * */

    private static final String TAG = "my_out_main_activity";
    private SurfaceHolder mSurfaceHolder;


    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
                                           @NonNull int[] grantResults) {
        if (permissions.length > 0 && grantResults.length > 0) {
            if (grantResults[0] == PackageManager.PERMISSION_GRANTED && requestCode == 1) {

                Toast.makeText(this,"授权完成，请重新打开应用",Toast.LENGTH_SHORT).show();
            }
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        int ret = ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE);
        if (ret != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.READ_EXTERNAL_STORAGE}, 1);
        } else {
            Log.d(TAG, "初始化播放器");
            SurfaceView surfaceView = findViewById(R.id.surface_view);
            mSurfaceHolder = surfaceView.getHolder();
            mSurfaceHolder.addCallback(this);
        }

    }


    @Override
    public void surfaceCreated(SurfaceHolder surfaceHolder) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                String dir = Environment.getExternalStorageDirectory().getPath();
                String url = dir + "/testfile.mp4";
                NativePlayer.playVideo(url, mSurfaceHolder.getSurface());
            }
        }).start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder surfaceHolder, int i, int i1, int i2) {

    }

    @Override
    public void surfaceDestroyed(SurfaceHolder surfaceHolder) {

    }
}
