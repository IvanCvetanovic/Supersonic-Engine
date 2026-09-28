#pragma once

// THE ANDROID HOST: the process's NativeActivity, as the rest of the engine
// needs to see it. Android only (CMakeLists.txt filters src/platform/android/
// out of every other build).
//
// The NDK's native_app_glue starts a thread and calls android_main on it; that
// is the engine's (AndroidApp.cpp), and the game's own entry is called from it
// once there is a window to draw on - see SupersonicMain below. Everything the
// platform then delivers arrives through one looper on that thread: lifecycle
// commands (a window created or destroyed, the app paused, focus gained) and
// input events. This file owns what they leave behind. The Window,
// InputPolling, NativeWindowControl and Gamepads implementations beside it are
// thin readers of that state, so the classes above them are the desktop's.

#include <functional>
#include <string>

#include "core/Input.hpp"
#include "platform/Gamepads.hpp"

struct android_app;
struct ANativeWindow;

// The GAME's entry, on a platform where the engine owns the process's real one.
//
// Defined by the game, exactly as main() is on desktop, and usually by calling
// the same function its main() calls. android_main calls it with argv[0] only,
// once the activity is resumed and has a window, and ends the process when it
// returns - so a game adds what a desktop command line would carry (paths to
// its files, development flags) itself, before handing argc/argv on.
int SupersonicMain(int argc, char** argv);

namespace Supersonic::Android {

// The glue's app state, for a game that needs the activity (its asset manager,
// its JavaVM). Null outside android_main.
android_app* App();

// The activity's private files directory (Context.getFilesDir()), and its
// app-specific external one (getExternalFilesDir(null)), which adb can read
// and write without run-as. The second may be empty.
std::string InternalDataPath();
std::string ExternalDataPath();

// Handles whatever the platform has delivered, without waiting. For a game
// doing long work before its app exists (unpacking its files on first run):
// the glue holds the UI thread in onStart, onResume and window changes until
// this thread reads each one, so a thread that stops reading for seconds is
// an "application not responding" dialog. False once the app has been asked
// to finish.
bool PumpEvents();

// ---- For the platform classes beside this file ------------------------------

ANativeWindow* CurrentWindow();
bool DestroyRequested();
bool IsFocused();

// Handles everything pending, then waits - handling events as they come -
// for as long as the app is paused or has no window. Returns at once when it
// is in the foreground with a window, or when it has been asked to finish.
void WaitUntilDrawable();

// The window's size in pixels, and whether it changed since the last Take.
void WindowSize(int& width, int& height);
bool TakeWindowSizeChanged();

// Whether WaitUntilDrawable waited since the last Take.
bool TakeResumedFromSuspend();

// Called while handling APP_CMD_TERM_WINDOW, before the glue releases the
// native window: whatever was made from it must be gone when it returns.
void SetSurfaceLostCallback(std::function<void()> callback);

// Called with true when the activity pauses and false when it resumes, so a
// sound stream stops with the game instead of playing on in the background.
void SetAudioSuspendHandler(std::function<void(bool suspended)> handler);

// This frame's devices: keys, the touch contacts, the mouse they stand in for
// and the first gamepad. Everything that went down since the last call is in
// it for at least this one frame, even if it has already come up - a tap is a
// down and an up in the same few milliseconds, and a snapshot taken on either
// side of both would never see it.
void FillRawInput(RawInputState& state);

int GamepadCount();
bool GetGamepad(int index, GamepadState& out);

// Monotonic seconds.
double Now();

} // namespace Supersonic::Android
