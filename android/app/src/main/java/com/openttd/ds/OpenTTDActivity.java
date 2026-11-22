package com.openttd.ds;

import android.content.Context;
import android.content.DialogInterface;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Main Android entry point for OpenTTD Dual Screen on AYN Thor.
 *
 * Responsibilities:
 * 1. Extracts bundled runtime assets (assets/openttd_data/) to app external storage before native
 *    startup, using a version/install stamp to avoid redundant re-extraction while preserving user
 *    saves and config files across upgrades.
 * 2. Registers the writable data directory with native code via OpenTTDNative.nativeSetDataDir.
 * 3. Launches SDL3 with the dual-screen driver ("-v", "sdl_ds").
 * 4. Enumerates and hotplug-tracks the secondary display via DisplayManager, hosting
 *    SecondaryScreenPresentation on the bottom screen throughout resume/pause lifecycle transitions.
 */
public class OpenTTDActivity extends SDLActivity {

    private static final String TAG = "OpenTTDActivity";
    private static final String ASSET_DATA_ROOT = "openttd_data";
    private static final String VERSION_STAMP_FILE = ".openttd_assets_version";
    private static final int COPY_BUFFER_SIZE = 32 * 1024;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private DisplayManager displayManager;
    private boolean displayListenerRegistered = false;
    private boolean isActivityResumed = false;
    private SecondaryScreenPresentation secondaryPresentation;
    private int secondaryDisplayId = Display.INVALID_DISPLAY;

    private final DisplayManager.DisplayListener displayListener = new DisplayManager.DisplayListener() {
        @Override
        public void onDisplayAdded(int displayId) {
            Log.i(TAG, "Display added: " + displayId);
            if (isActivityResumed) {
                attachSecondaryDisplay();
            }
        }

        @Override
        public void onDisplayRemoved(int displayId) {
            Log.i(TAG, "Display removed: " + displayId);
            if (displayId == secondaryDisplayId) {
                detachSecondaryDisplay();
                if (isActivityResumed) {
                    attachSecondaryDisplay();
                }
            }
        }

        @Override
        public void onDisplayChanged(int displayId) {
            if (!isActivityResumed) {
                return;
            }
            if (displayId == secondaryDisplayId) {
                Display d = (displayManager != null) ? displayManager.getDisplay(displayId) : null;
                if (!isUsableSecondaryDisplay(d, getCurrentDisplayId())) {
                    detachSecondaryDisplay();
                    attachSecondaryDisplay();
                }
            } else if (secondaryPresentation == null) {
                attachSecondaryDisplay();
            }
        }
    };

    @Override
    protected String[] getLibraries() {
        return new String[] {
                "SDL3",
                "main"
        };
    }

    @Override
    protected String[] getArguments() {
        return new String[] {
                "-v", "sdl_ds"
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Extract bundled runtime data before SDLActivity loads libraries or prepares startup.
        File dataDir = resolveDataDir();
        extractBundledAssetsIfNeeded(dataDir);

        super.onCreate(savedInstanceState);

        if (SDLActivity.mBrokenLibraries) {
            return;
        }

        Window window = getWindow();
        if (window != null) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }

        OpenTTDNative.nativeSetDataDir(dataDir.getAbsolutePath());

        displayManager = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        if (displayManager != null) {
            displayManager.registerDisplayListener(displayListener, mainHandler);
            displayListenerRegistered = true;
        }

        // Do not call SDLActivity.setWindowStyle(true) here on the UI thread: SDLActivity's
        // sendCommand(COMMAND_CHANGE_WINDOW_STYLE, 1) waits up to 500ms on the calling thread for
        // surfaceChanged(), which would stall the UI thread during onCreate(). Instead, apply
        // immersive fullscreen immediately and again once SDLActivity's queued initial window-style
        // message has drained from the main looper.
        applyImmersiveMode();
        mainHandler.post(new Runnable() {
            @Override
            public void run() {
                if (!isFinishing() && !isDestroyed()) {
                    applyImmersiveMode();
                }
            }
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (SDLActivity.mBrokenLibraries) {
            return;
        }
        isActivityResumed = true;
        applyImmersiveMode();
        attachSecondaryDisplay();
    }

    @Override
    protected void onPause() {
        isActivityResumed = false;
        detachSecondaryDisplay();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        isActivityResumed = false;
        mainHandler.removeCallbacksAndMessages(null);
        detachSecondaryDisplay();
        if (displayManager != null && displayListenerRegistered) {
            displayManager.unregisterDisplayListener(displayListener);
            displayListenerRegistered = false;
        }
        super.onDestroy();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveMode();
        }
    }

    private File resolveDataDir() {
        File ext = getExternalFilesDir(null);
        if (ext != null) {
            if (ext.exists() || ext.mkdirs()) {
                return ext;
            }
        }
        File internal = getFilesDir();
        if (!internal.exists()) {
            internal.mkdirs();
        }
        return internal;
    }

    private String computeAssetVersionStamp() {
        try {
            PackageInfo info = getAppPackageInfo();
            long versionCode = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P)
                    ? info.getLongVersionCode()
                    : getLegacyVersionCode(info);
            String versionName = (info.versionName != null) ? info.versionName : "0";
            return versionCode + ":" + versionName + ":" + info.lastUpdateTime;
        } catch (PackageManager.NameNotFoundException e) {
            Log.w(TAG, "Unable to query PackageInfo for asset stamp", e);
            return "unknown";
        }
    }

    @SuppressWarnings("deprecation")
    private PackageInfo getAppPackageInfo() throws PackageManager.NameNotFoundException {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return getPackageManager().getPackageInfo(
                    getPackageName(),
                    PackageManager.PackageInfoFlags.of(0)
            );
        }
        return getPackageManager().getPackageInfo(getPackageName(), 0);
    }

    @SuppressWarnings("deprecation")
    private static long getLegacyVersionCode(PackageInfo info) {
        return info.versionCode;
    }

    private void extractBundledAssetsIfNeeded(File dataDir) {
        if (!dataDir.exists() && !dataDir.mkdirs()) {
            Log.e(TAG, "Failed to create OpenTTD data directory: " + dataDir);
            return;
        }

        String expectedStamp = computeAssetVersionStamp();
        File stampFile = new File(dataDir, VERSION_STAMP_FILE);
        String currentStamp = readStamp(stampFile);

        File langDir = new File(dataDir, "lang");
        File basesetDir = new File(dataDir, "baseset");
        if (expectedStamp.equals(currentStamp) && isNonEmptyDirectory(langDir) && isNonEmptyDirectory(basesetDir)) {
            Log.i(TAG, "OpenTTD runtime assets up to date (" + expectedStamp + ")");
            return;
        }

        AssetManager assets = getAssets();
        try {
            String[] rootEntries = assets.list(ASSET_DATA_ROOT);
            if (rootEntries == null || rootEntries.length == 0) {
                Log.w(TAG, "No bundled assets found under assets/" + ASSET_DATA_ROOT);
                return;
            }

            Log.i(TAG, "Extracting OpenTTD runtime assets into " + dataDir.getAbsolutePath());
            byte[] copyBuffer = new byte[COPY_BUFFER_SIZE];
            for (String entry : rootEntries) {
                extractAssetEntry(assets, ASSET_DATA_ROOT + "/" + entry, new File(dataDir, entry), copyBuffer);
            }
            writeStamp(stampFile, expectedStamp);
            Log.i(TAG, "Finished extracting OpenTTD runtime assets (" + expectedStamp + ")");
        } catch (IOException e) {
            Log.e(TAG, "Failed to extract bundled OpenTTD assets", e);
        }
    }

    private static boolean isNonEmptyDirectory(File dir) {
        if (!dir.isDirectory()) {
            return false;
        }
        String[] entries = dir.list();
        return entries != null && entries.length > 0;
    }

    private void extractAssetEntry(AssetManager assets, String assetPath, File outPath, byte[] buffer)
            throws IOException {
        // Try opening as a regular asset file first (O(1) lookup) to avoid calling
        // AssetManager.list() on every leaf file during cold start.
        InputStream in;
        try {
            in = assets.open(assetPath);
        } catch (FileNotFoundException notAFile) {
            String[] children = assets.list(assetPath);
            if (children != null && children.length > 0) {
                if (!outPath.exists() && !outPath.mkdirs()) {
                    throw new IOException("Could not create directory " + outPath);
                }
                for (String child : children) {
                    extractAssetEntry(assets, assetPath + "/" + child, new File(outPath, child), buffer);
                }
                return;
            }
            // Empty asset directory entry (returned by parent's assets.list() but has no children).
            Log.w(TAG, "Skipping empty asset directory: " + assetPath);
            return;
        }

        File parent = outPath.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            try {
                in.close();
            } catch (IOException ignored) {
                // Best-effort close on setup failure.
            }
            throw new IOException("Could not create parent directory " + parent);
        }

        File tmpFile = new File(parent, outPath.getName() + ".tmp");
        try (InputStream assetIn = in) {
            try (OutputStream out = new FileOutputStream(tmpFile)) {
                int read;
                while ((read = assetIn.read(buffer)) != -1) {
                    out.write(buffer, 0, read);
                }
                out.flush();
            }
            if (outPath.exists() && !outPath.delete()) {
                Log.w(TAG, "Could not remove existing asset file before replace: " + outPath);
            }
            if (!tmpFile.renameTo(outPath)) {
                copyStreamFallback(tmpFile, outPath, buffer);
            }
        } finally {
            if (tmpFile.exists()) {
                tmpFile.delete();
            }
        }
    }

    private static void copyStreamFallback(File src, File dst, byte[] buffer) throws IOException {
        try (InputStream in = new FileInputStream(src); OutputStream out = new FileOutputStream(dst)) {
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            out.flush();
        }
    }

    private static String readStamp(File stampFile) {
        if (!stampFile.isFile()) {
            return null;
        }
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(new FileInputStream(stampFile), StandardCharsets.UTF_8))) {
            return reader.readLine();
        } catch (IOException e) {
            return null;
        }
    }

    private static void writeStamp(File stampFile, String stamp) {
        try (BufferedWriter writer = new BufferedWriter(
                new OutputStreamWriter(new FileOutputStream(stampFile), StandardCharsets.UTF_8))) {
            writer.write(stamp);
            writer.newLine();
        } catch (IOException e) {
            Log.w(TAG, "Unable to write asset version stamp", e);
        }
    }

    @SuppressWarnings("deprecation")
    private int getCurrentDisplayId() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Display current = getDisplay();
            if (current != null) {
                return current.getDisplayId();
            }
        }
        WindowManager wm = getWindowManager();
        if (wm != null) {
            Display def = wm.getDefaultDisplay();
            if (def != null) {
                return def.getDisplayId();
            }
        }
        return Display.DEFAULT_DISPLAY;
    }

    private List<Display> collectSecondaryDisplays() {
        List<Display> candidates = new ArrayList<>();
        if (displayManager == null) {
            return candidates;
        }
        int primaryId = getCurrentDisplayId();
        Set<Integer> seenIds = new HashSet<>();

        Display[] presentationDisplays =
                displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        if (presentationDisplays != null) {
            for (Display display : presentationDisplays) {
                if (isUsableSecondaryDisplay(display, primaryId) && seenIds.add(display.getDisplayId())) {
                    candidates.add(display);
                }
            }
        }

        // Fallback for dual-screen handhelds whose built-in bottom panel is not flagged PRESENTATION.
        Display[] allDisplays = displayManager.getDisplays();
        if (allDisplays != null) {
            for (Display display : allDisplays) {
                if (isUsableSecondaryDisplay(display, primaryId) && seenIds.add(display.getDisplayId())) {
                    candidates.add(display);
                }
            }
        }

        return candidates;
    }

    private static boolean isUsableSecondaryDisplay(Display display, int primaryId) {
        return display != null
                && display.isValid()
                && display.getDisplayId() != primaryId
                && display.getState() != Display.STATE_OFF
                && (display.getFlags() & Display.FLAG_PRIVATE) == 0;
    }

    private void attachSecondaryDisplay() {
        if (!isActivityResumed || isFinishing() || isDestroyed() || SDLActivity.mBrokenLibraries) {
            return;
        }

        List<Display> candidates = collectSecondaryDisplays();
        if (candidates.isEmpty()) {
            detachSecondaryDisplay();
            return;
        }

        if (secondaryPresentation != null && secondaryPresentation.isShowing()) {
            boolean currentStillValid = false;
            for (Display candidate : candidates) {
                if (candidate.getDisplayId() == secondaryDisplayId) {
                    currentStillValid = true;
                    break;
                }
            }
            Display preferred = candidates.get(0);
            boolean preferredIsPresentation = (preferred.getFlags() & Display.FLAG_PRESENTATION) != 0;
            if (currentStillValid
                    && (preferred.getDisplayId() == secondaryDisplayId || !preferredIsPresentation)) {
                return;
            }
        }

        detachSecondaryDisplay();

        for (Display target : candidates) {
            try {
                final SecondaryScreenPresentation presentation = new SecondaryScreenPresentation(this, target);
                presentation.setOnDismissListener(new DialogInterface.OnDismissListener() {
                    @Override
                    public void onDismiss(DialogInterface dialog) {
                        if (secondaryPresentation == presentation) {
                            secondaryPresentation = null;
                            secondaryDisplayId = Display.INVALID_DISPLAY;
                            if (!SDLActivity.mBrokenLibraries) {
                                OpenTTDNative.nativeOnSecondarySurfaceDestroyed();
                            }
                        }
                    }
                });
                presentation.show();
                secondaryPresentation = presentation;
                secondaryDisplayId = target.getDisplayId();
                Log.i(TAG, "Attached secondary Command Desk presentation on display " + secondaryDisplayId);
                return;
            } catch (WindowManager.InvalidDisplayException e) {
                Log.w(TAG, "Secondary display " + target.getDisplayId() + " disappeared while attaching presentation", e);
            } catch (RuntimeException e) {
                Log.e(TAG, "Failed to show secondary presentation on display " + target.getDisplayId(), e);
            }
        }

        secondaryPresentation = null;
        secondaryDisplayId = Display.INVALID_DISPLAY;
    }

    private void detachSecondaryDisplay() {
        SecondaryScreenPresentation presentation = secondaryPresentation;
        secondaryPresentation = null;
        secondaryDisplayId = Display.INVALID_DISPLAY;

        if (presentation != null) {
            try {
                presentation.dismiss();
            } catch (RuntimeException e) {
                Log.w(TAG, "Error dismissing secondary presentation", e);
            }
            if (!SDLActivity.mBrokenLibraries) {
                OpenTTDNative.nativeOnSecondarySurfaceDestroyed();
            }
        }
    }

    @SuppressWarnings("deprecation")
    private void applyImmersiveMode() {
        Window window = getWindow();
        if (window == null) {
            return;
        }
        window.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams attrs = window.getAttributes();
            attrs.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            window.setAttributes(attrs);
        }
        SDLActivity.mFullscreenModeActive = true;

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
