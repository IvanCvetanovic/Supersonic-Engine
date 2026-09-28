#pragma once

// THE iOS HOST: the process's UIKit application, as the rest of the engine
// needs to see it. iOS only (CMakeLists.txt filters src/platform/ios/ out of
// every other build).
//
// UIKit owns the process's real entry. The engine's main() (IOSApp.mm) hands
// it to UIApplicationMain, and once the app's one scene is connected and its
// window shows a view backed by a CAMetalLayer, the game's own entry is called
// - SupersonicMain below - from inside UIKit's run loop, on the main thread.
// The game then runs its own loop and never returns to UIKit's; instead each
// frame's Window::PollEvents runs UIKit's run loop until it has nothing left to
// deliver (the way SDL runs a game on iOS). Everything UIKit delivers - touches,
// keys, the scene going inactive or to the background, the view's size and
// safe area changing - therefore arrives inside PollEvents, as the Android
// looper's commands do, and this file keeps what it leaves behind. The Window,
// InputPolling, NativeWindowControl and Gamepads implementations beside it are
// thin readers of that state.

#include <functional>

#include "core/Input.hpp"
#include "platform/Gamepads.hpp"

// The GAME's entry, on a platform where the engine owns the process's real one.
//
// Defined by the game, as on Android (platform/android/AndroidApp.hpp), usually
// by calling the function its desktop main() calls. Called with the process's
// own argc and argv once there is a view to draw on; the process exits with
// what it returns. A game adds what a desktop command line would carry (paths
// to the files in its bundle, development flags) itself, before handing on.
int SupersonicMain(int argc, char** argv);

namespace Supersonic::IOS {

// ---- For the platform classes beside this file ------------------------------

// The view's CAMetalLayer (a CAMetalLayer*), or null before the scene connects.
void* CurrentLayer();

// UIKit is ending the app, or its scene went away.
bool DestroyRequested();

// Whether the scene is active - on screen and receiving events. A loss of
// focus is LATCHED: false is answered at least once after the scene resigned
// active, even when it is active again by the time anyone asks. Going to the
// background and coming back is usually delivered in a single pass of the run
// loop, and a game that pauses on losing focus has to see one frame without it.
bool TakeFocused();

// Runs UIKit's run loop until nothing is pending; then, for as long as the app
// is in the background, waits there - handling events as they come - because a
// backgrounded app may not submit GPU work. Returns once the app is on screen
// again, or asked to finish. Also drains the autorelease pool the previous
// frame's Objective-C objects went into.
void WaitUntilDrawable();

// The view's drawable size in pixels, and whether it changed since the last
// Take.
void WindowSize(int& width, int& height);
bool TakeWindowSizeChanged();

// Whether WaitUntilDrawable waited since the last Take.
bool TakeResumedFromSuspend();

// Called when the scene enters the background, before UIKit suspends the app:
// the swapchain and surface must be gone when it returns (a suspended app that
// still owns GPU work in flight is terminated). The next frame on screen
// builds them again, as on Android.
void SetSurfaceLostCallback(std::function<void()> callback);

// Called with true when the app leaves the screen or its audio session is
// interrupted (a phone call), false when it may play again.
void SetAudioSuspendHandler(std::function<void(bool suspended)> handler);

// This frame's devices: a hardware keyboard's keys, the touch contacts, the
// mouse the first finger stands in for, and the first gamepad. Everything that
// went down since the last call is in it for at least this one frame.
void FillRawInput(RawInputState& state);

// The extended gamepads GameController reports, in the order it lists them.
int GamepadCount();
bool GetGamepad(int index, GamepadState& out);

// Monotonic seconds.
double Now();

} // namespace Supersonic::IOS
