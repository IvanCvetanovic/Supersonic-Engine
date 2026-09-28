#pragma once

namespace Supersonic {

// How far in from each edge of the window the platform may cover or cut away
// what is drawn: a camera notch or punch hole, rounded corners, a status bar or
// home indicator that is showing. Anything a player has to reach or read -
// on-screen buttons, a HUD - belongs inside it; the picture itself does not
// have to be, and on a phone usually is not.
//
// In WINDOW PIXELS: the pixels of the swapchain's extent, of
// Input::MousePosition and of the touch contacts, with the origin at the top
// left. Measured from each edge inward, so an inset of 80 on the left means
// x < 80 may be hidden.
//
// All zero on the desktop, where the window is a rectangle nothing covers.
struct SafeAreaInsets {
    float left{0.0f};
    float top{0.0f};
    float right{0.0f};
    float bottom{0.0f};

    bool IsZero() const { return left == 0.0f && top == 0.0f && right == 0.0f && bottom == 0.0f; }
    bool operator==(const SafeAreaInsets& other) const = default;
};

namespace SafeArea {

// The window's safe-area insets now. Main thread, inside the frame, like the
// polling layer's other reads; it changes when the platform says so (a
// rotation, a bar shown or hidden), so read it each frame rather than once.
//
// Implemented once per window backend (platform/WindowBackend.hpp): SafeArea.cpp
// answers zero wherever the window is GLFW's, and a native-surface backend
// defines this function itself - Android from what its activity reports
// (platform/android/), iOS from the view's safeAreaInsets.
SafeAreaInsets Get();

} // namespace SafeArea

} // namespace Supersonic
