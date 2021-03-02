package com.bug.nativeplayer;

/**
 * Created by hejunlin on 17/3/1.
 */

public class NativePlayer {

    static {
        System.loadLibrary("native-lib");
    }

    public static native int playVideo(String url, Object surface);
}
