package com.openttd.ds;

import android.app.Presentation;
import android.content.Context;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.drawable.ColorDrawable;
import android.os.Build;
import android.os.Bundle;
import android.view.Display;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;

/**
 * Immersive secondary-screen Presentation hosting the bottom Command Desk SurfaceView.
 *
 * Uses FLAG_NOT_FOCUSABLE so touch events work independently on the bottom screen without
 * stealing hardware gamepad or keyboard focus from SDLActivity on the top screen.
 */
public final class SecondaryScreenPresentation extends Presentation
        implements SurfaceHolder.Callback, View.OnTouchListener {

    private SurfaceView surfaceView;
    private int surfaceWidth = 0;
    private int surfaceHeight = 0;
    private boolean surfaceActive = false;
    private int activePrimaryPointerId = MotionEvent.INVALID_POINTER_ID;

    public SecondaryScreenPresentation(Context outerContext, Display display) {
        super(outerContext, display);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);

        Window window = getWindow();
        if (window != null) {
            window.addFlags(
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                            | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                            | WindowManager.LayoutParams.FLAG_FULLSCREEN
            );
            window.setBackgroundDrawable(new ColorDrawable(Color.BLACK));
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                WindowManager.LayoutParams attrs = window.getAttributes();
                attrs.layoutInDisplayCutoutMode =
                        WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                window.setAttributes(attrs);
            }
        }

        FrameLayout root = new FrameLayout(getContext());
        root.setBackgroundColor(Color.BLACK);
        root.setLayoutParams(new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
        ));

        surfaceView = new SurfaceView(getContext());
        surfaceView.setKeepScreenOn(true);
        surfaceView.setFocusable(false);
        surfaceView.setFocusableInTouchMode(false);
        surfaceView.setLayoutParams(new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
        ));
        surfaceView.getHolder().setFormat(PixelFormat.RGBX_8888);
        surfaceView.getHolder().addCallback(this);
        surfaceView.setOnTouchListener(this);

        root.addView(surfaceView);
        setContentView(root);

        if (window != null) {
            window.setLayout(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT
            );
        }

        applyImmersiveMode();
    }

    @Override
    protected void onStart() {
        super.onStart();
        applyImmersiveMode();
    }

    @Override
    protected void onStop() {
        notifySurfaceDestroyedIfNeeded();
        super.onStop();
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        // Native surface dimensions are delivered in surfaceChanged immediately after creation.
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (holder == null || width <= 0 || height <= 0) {
            notifySurfaceDestroyedIfNeeded();
            return;
        }
        Surface surface = holder.getSurface();
        if (surface == null || !surface.isValid()) {
            notifySurfaceDestroyedIfNeeded();
            return;
        }
        surfaceWidth = width;
        surfaceHeight = height;
        surfaceActive = true;
        OpenTTDNative.nativeOnSecondarySurfaceChanged(surface, width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        notifySurfaceDestroyedIfNeeded();
    }

    @Override
    public boolean onTouch(View view, MotionEvent event) {
        if (event == null) {
            return false;
        }

        int width = view.getWidth();
        int height = view.getHeight();
        if (width <= 0) {
            width = surfaceWidth;
        }
        if (height <= 0) {
            height = surfaceHeight;
        }
        if (width <= 0 || height <= 0) {
            return true;
        }

        int pointerCount = event.getPointerCount();
        if (pointerCount <= 0) {
            return true;
        }

        int actionMasked = event.getActionMasked();
        switch (actionMasked) {
            case MotionEvent.ACTION_DOWN: {
                int idx = safeActionIndex(event, pointerCount);
                int pointerId = event.getPointerId(idx);
                activePrimaryPointerId = pointerId;
                forwardTouch(actionMasked, pointerId, event.getX(idx), event.getY(idx), width, height, pointerCount);
                return true;
            }
            case MotionEvent.ACTION_POINTER_DOWN: {
                int idx = safeActionIndex(event, pointerCount);
                int pointerId = event.getPointerId(idx);
                forwardTouch(actionMasked, pointerId, event.getX(idx), event.getY(idx), width, height, pointerCount);
                return true;
            }
            case MotionEvent.ACTION_POINTER_UP: {
                int idx = safeActionIndex(event, pointerCount);
                int pointerId = event.getPointerId(idx);
                if (pointerId == activePrimaryPointerId) {
                    activePrimaryPointerId = MotionEvent.INVALID_POINTER_ID;
                }
                forwardTouch(actionMasked, pointerId, event.getX(idx), event.getY(idx), width, height, pointerCount);
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                int idx = (activePrimaryPointerId != MotionEvent.INVALID_POINTER_ID)
                        ? event.findPointerIndex(activePrimaryPointerId)
                        : -1;
                if (idx < 0 || idx >= pointerCount) {
                    idx = 0;
                    activePrimaryPointerId = event.getPointerId(0);
                }
                int pointerId = event.getPointerId(idx);
                forwardTouch(actionMasked, pointerId, event.getX(idx), event.getY(idx), width, height, pointerCount);
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL: {
                int idx = safeActionIndex(event, pointerCount);
                int pointerId = event.getPointerId(idx);
                activePrimaryPointerId = MotionEvent.INVALID_POINTER_ID;
                forwardTouch(actionMasked, pointerId, event.getX(idx), event.getY(idx), width, height, pointerCount);
                return true;
            }
            default:
                return true;
        }
    }

    private void notifySurfaceDestroyedIfNeeded() {
        surfaceWidth = 0;
        surfaceHeight = 0;
        activePrimaryPointerId = MotionEvent.INVALID_POINTER_ID;
        if (surfaceActive) {
            surfaceActive = false;
            OpenTTDNative.nativeOnSecondarySurfaceDestroyed();
        }
    }

    private static int safeActionIndex(MotionEvent event, int pointerCount) {
        int idx = event.getActionIndex();
        return (idx >= 0 && idx < pointerCount) ? idx : 0;
    }

    private static void forwardTouch(
            int actionMasked,
            int pointerId,
            float rawX,
            float rawY,
            int width,
            int height,
            int pointerCount
    ) {
        float normX = (width > 1) ? clamp01(rawX / (float) (width - 1)) : 0.5f;
        float normY = (height > 1) ? clamp01(rawY / (float) (height - 1)) : 0.5f;
        OpenTTDNative.nativeOnSecondaryTouch(actionMasked, pointerId, normX, normY, pointerCount);
    }

    private static float clamp01(float value) {
        if (Float.isNaN(value) || value < 0.0f) {
            return 0.0f;
        }
        if (value > 1.0f) {
            return 1.0f;
        }
        return value;
    }

    @SuppressWarnings("deprecation")
    private void applyImmersiveMode() {
        Window window = getWindow();
        if (window == null) {
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                );
            }
        } else {
            View decor = window.getDecorView();
            decor.setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            );
        }
    }
}
