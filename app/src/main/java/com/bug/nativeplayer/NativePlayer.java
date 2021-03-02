package com.bug.nativeplayer;


import android.view.Surface;


public class NativePlayer {

    static {
        System.loadLibrary("native-lib");
    }

    public static native int nativeWindowPlayVideo(String url, Surface surface);

    public static native int openGlPlayVideo(String url, Surface surface);
}
