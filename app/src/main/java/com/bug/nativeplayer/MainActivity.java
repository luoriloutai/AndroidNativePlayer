package com.bug.nativeplayer;


import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.Button;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

public class MainActivity extends AppCompatActivity {

    /*
    *  说明：先在SDCard根目录下放一个名为“testfile.mp4”的文件
    *       项目目录file下提供了一个testfile.mp4,可以直接把它
    *       放在SDCard根目录
    * */

    private static final String TAG = "my_out_main_activity";



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
        }else{
            Button nativePlayerBtn = findViewById(R.id.nativeWindowPlayerButton);
            nativePlayerBtn.setOnClickListener(view -> {
                Intent intent = new Intent(MainActivity.this,NativeWindowPlayerActivity.class);
                startActivity(intent);
            });

            Button openglPlayerBtn = findViewById(R.id.openglPlayerButton);
            openglPlayerBtn.setOnClickListener(view -> {
                Intent intent = new Intent(MainActivity.this,OpenglPlayerActivity.class);
                startActivity(intent);
            });
        }

    }

}
