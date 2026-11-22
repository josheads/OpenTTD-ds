package com.openttd.ds;

import android.view.Surface;

/**
 * JNI bridge declarations matching src/os/android/android_ds_jni.cpp verbatim.
 */
public final class OpenTTDNative {
    private OpenTTDNative() {
    }

    /**
     * Sets the writable OpenTTD data directory where bundled runtime assets were extracted.
     */
    public static native void nativeSetDataDir(String path);

    /**
     * Provides or updates the secondary display Surface and physical pixel dimensions.
     */
    public static native void nativeOnSecondarySurfaceChanged(Surface surface, int width, int height);

    /**
     * Notifies native code that the secondary display Surface is no longer valid.
     */
    public static native void nativeOnSecondarySurfaceDestroyed();

    /**
     * Forwards normalized [0, 1] touch events from the secondary display SurfaceView.
     */
    public static native void nativeOnSecondaryTouch(
            int actionMasked,
            int pointerId,
            float normX,
            float normY,
            int pointerCount
    );
}
