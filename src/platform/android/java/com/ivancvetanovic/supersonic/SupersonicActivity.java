package com.ivancvetanovic.supersonic;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.DialogInterface;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.graphics.Insets;
import android.os.Build;
import android.os.Bundle;
import android.text.util.Linkify;
import android.util.Log;
import android.view.Display;
import android.view.DisplayCutout;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.TextView;

/**
 * The engine's activity on Android: NativeActivity, plus the two things only
 * Java can do for it, both on the UI thread where Android requires them.
 *
 * <ul>
 * <li>IMMERSIVE FULLSCREEN. The navigation and status bars are hidden, and hidden
 * again whenever the window regains focus (a swipe from an edge shows them for
 * a moment; Home and back undoes it). NativeActivity's own window flags reach
 * only the status bar.</li>
 * <li>THE SAFE AREA. The window is kept clear of a display cutout, and the
 * insets that still cover it - a bar that is showing, a cutout the system laid
 * it into anyway - are handed to the engine in window pixels
 * (platform/SafeArea.hpp).</li>
 * <li>THE REFRESH RATE a game prefers (WindowControl::SetPreferredRefreshRate):
 * the display mode of that rate at the current resolution, made the window's
 * preferred one. Asked by the engine through JNI, not by a native callback:
 * requestRefreshRate below.</li>
 * <li>A MESSAGE for the player, for a game that cannot start and has no window
 * to draw it in: showMessage and isMessageOpen below, asked and polled by the
 * engine through JNI (Android::ShowMessage).</li>
 * </ul>
 *
 * A game names this class in its manifest instead of android.app.NativeActivity,
 * with the same android.app.lib_name meta-data; its native code is unchanged.
 * Deliberately tiny: everything else stays in C++.
 */
public class SupersonicActivity extends NativeActivity {

    // Implemented in src/platform/android/AndroidApp.cpp.
    private static native void nativeSetSafeArea(int left, int top, int right, int bottom);

    private boolean mLibraryLoaded;

    // Whether the dialog showMessage made is still on the screen. Read by the
    // engine's thread, set on the UI thread.
    private volatile boolean mMessageOpen;

    @Override
    @SuppressWarnings("deprecation")
    protected void onCreate(Bundle savedInstanceState) {
        // The game's library, loaded through the JNI path so the native method
        // above resolves in it. NativeActivity then opens the same file itself
        // and is handed the same library.
        loadGameLibrary();
        super.onCreate(savedInstanceState);

        Window window = getWindow();
        // Never into a display cutout: the window is letterboxed clear of the
        // notch, so everything a game draws - its HUD included - is visible
        // without the game knowing the notch is there. (Laid out into it on
        // the short edges, the picture reached the notch and only what read
        // the safe area moved out of its way.)
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams attributes = window.getAttributes();
            attributes.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER;
            window.setAttributes(attributes);
        }
        // Edge to edge, so the insets below describe the whole window. From
        // API 35 an app targeting 35 is edge to edge already and the call is
        // deprecated; below 30 the layout flags in hideSystemBars do it.
        if (Build.VERSION.SDK_INT >= 30 && Build.VERSION.SDK_INT < 35) {
            window.setDecorFitsSystemWindows(false);
        }

        window.getDecorView().setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View view, WindowInsets insets) {
                reportSafeArea(insets);
                return view.onApplyWindowInsets(insets);
            }
        });
        hideSystemBars();
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    /**
     * The display mode for a refresh rate, asked by the engine from its own thread
     * (src/platform/android/AndroidApp.cpp, RequestRefreshRate). Among the modes at
     * the display's current resolution - a rate is not worth a resolution change -
     * {@code hz} 0 picks the highest rate, a positive {@code hz} the nearest to it,
     * and a negative one withdraws the preference (mode 0, the system's choice). The
     * window's preferredDisplayModeId is set on the UI thread, where Android requires
     * window attributes to change; the display may still decide otherwise (a battery
     * saver, a thermal limit). Returns the chosen mode's rate, 0 when none was chosen.
     */
    public float requestRefreshRate(float hz) {
        if (Build.VERSION.SDK_INT < 23) return 0f;
        Display display = currentDisplay();
        if (display == null) return 0f;
        Display.Mode current = display.getMode();

        Display.Mode chosen = null;
        StringBuilder offered = new StringBuilder();
        if (hz >= 0f) {
            for (Display.Mode mode : display.getSupportedModes()) {
                if (mode.getPhysicalWidth() != current.getPhysicalWidth()
                        || mode.getPhysicalHeight() != current.getPhysicalHeight()) {
                    continue;
                }
                if (offered.length() > 0) offered.append(", ");
                offered.append(mode.getRefreshRate()).append(" Hz (mode ").append(mode.getModeId()).append(')');
                if (chosen == null) {
                    chosen = mode;
                } else if (hz == 0f ? mode.getRefreshRate() > chosen.getRefreshRate()
                        : Math.abs(mode.getRefreshRate() - hz) < Math.abs(chosen.getRefreshRate() - hz)) {
                    chosen = mode;
                }
            }
        }

        final int modeId = chosen != null ? chosen.getModeId() : 0;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Window window = getWindow();
                WindowManager.LayoutParams attributes = window.getAttributes();
                attributes.preferredDisplayModeId = modeId;
                window.setAttributes(attributes);
            }
        });

        String asked = hz < 0f ? "no preference" : hz == 0f ? "the highest" : hz + " Hz";
        Log.i("Supersonic", "[Android] Display: refresh rate asked for " + asked + " at "
                + current.getPhysicalWidth() + "x" + current.getPhysicalHeight() + " (now "
                + current.getRefreshRate() + " Hz, mode " + current.getModeId() + "); offered "
                + (offered.length() > 0 ? offered : "nothing asked") + "; preferred mode " + modeId
                + (chosen != null ? " at " + chosen.getRefreshRate() + " Hz." : ", the system's choice."));
        return chosen != null ? chosen.getRefreshRate() : 0f;
    }

    /**
     * A dialog with {@code text} under {@code title} and an OK button, shown on the UI
     * thread; returns at once, and isMessageOpen says when the dialog is gone (closed by
     * the player, or never shown because the window is going). Asked by the engine from
     * its own thread (src/platform/android/AndroidApp.cpp, ShowMessage).
     */
    public void showMessage(final String title, final String text) {
        mMessageOpen = true;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    AlertDialog dialog = new AlertDialog.Builder(SupersonicActivity.this)
                            .setTitle(title)
                            .setMessage(text)
                            .setPositiveButton(android.R.string.ok, null)
                            .create();
                    dialog.setOnDismissListener(new DialogInterface.OnDismissListener() {
                        @Override
                        public void onDismiss(DialogInterface shown) {
                            mMessageOpen = false;
                        }
                    });
                    // Only OK closes it. A player who sees a black screen taps it and presses
                    // Back, and the message must still be there when the dialog appears: a tap
                    // outside it or Back would end the game with nothing read.
                    dialog.setCancelable(false);
                    dialog.setCanceledOnTouchOutside(false);
                    dialog.show();
                    // The text can be copied (a long press), and an address in it can be tapped:
                    // a tester has to send the details line and the phone's model on.
                    TextView message = dialog.findViewById(android.R.id.message);
                    if (message != null) {
                        Linkify.addLinks(message, Linkify.WEB_URLS);
                        message.setTextIsSelectable(true);
                    }
                } catch (RuntimeException e) {
                    // The window is already going (a bad window token): there is
                    // nothing to show it on, and nothing to wait for.
                    Log.w("Supersonic", "[Android] Message: could not be shown: " + e);
                    mMessageOpen = false;
                }
            }
        });
    }

    /** Whether the dialog showMessage made is still up. */
    public boolean isMessageOpen() {
        return mMessageOpen;
    }

    @SuppressWarnings("deprecation")
    private Display currentDisplay() {
        if (Build.VERSION.SDK_INT >= 30) return getDisplay();
        return getWindowManager().getDefaultDisplay();
    }

    private void loadGameLibrary() {
        String name = "main";
        try {
            ActivityInfo info = getPackageManager().getActivityInfo(
                    getIntent().getComponent(), PackageManager.GET_META_DATA);
            if (info.metaData != null) {
                String declared = info.metaData.getString("android.app.lib_name");
                if (declared != null) name = declared;
            }
        } catch (PackageManager.NameNotFoundException e) {
            // NativeActivity's own default, which it will look for anyway.
        }
        try {
            System.loadLibrary(name);
            mLibraryLoaded = true;
        } catch (UnsatisfiedLinkError e) {
            // NativeActivity reports a library it cannot load, and says why.
            mLibraryLoaded = false;
        }
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        Window window = getWindow();
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            window.getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }
    }

    // What is covered now: the cutout, and the bars only while they show (a
    // hidden bar covers nothing, and reserving room for one would push every
    // control in for a bar the player swiped away).
    @SuppressWarnings("deprecation")
    private void reportSafeArea(WindowInsets insets) {
        if (!mLibraryLoaded) return;
        int left;
        int top;
        int right;
        int bottom;
        if (Build.VERSION.SDK_INT >= 30) {
            Insets covered = insets.getInsets(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            left = covered.left;
            top = covered.top;
            right = covered.right;
            bottom = covered.bottom;
        } else {
            left = insets.getSystemWindowInsetLeft();
            top = insets.getSystemWindowInsetTop();
            right = insets.getSystemWindowInsetRight();
            bottom = insets.getSystemWindowInsetBottom();
            if (Build.VERSION.SDK_INT >= 28) {
                DisplayCutout cutout = insets.getDisplayCutout();
                if (cutout != null) {
                    left = Math.max(left, cutout.getSafeInsetLeft());
                    top = Math.max(top, cutout.getSafeInsetTop());
                    right = Math.max(right, cutout.getSafeInsetRight());
                    bottom = Math.max(bottom, cutout.getSafeInsetBottom());
                }
            }
        }
        nativeSetSafeArea(left, top, right, bottom);
    }
}
