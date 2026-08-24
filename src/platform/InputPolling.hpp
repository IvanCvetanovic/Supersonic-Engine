#pragma once

#include "core/Input.hpp"

namespace Supersonic {

class Window;

// Reads the real devices into a RawInputState and hands it to Input.
//
// Separate from Input itself so the mapping and edge detection can be tested
// without a window: this file is the only one that touches GLFW, and no test
// target links it.
class InputPolling {
public:
    // Once per frame, before anything queries input.
    static void Poll(Window& window);

    // Puts the window into the cursor mode Input has settled on.
    //
    // Separate from Poll, and called EARLIER in the frame - straight after
    // glfwPollEvents, before ImGui's new frame. ImGui reads the cursor position
    // itself, and under a locked pointer GLFW reports unbounded virtual
    // coordinates that neither ImGui backend callback guards against; applying
    // the mode after ImGui had already sampled would leave the two disagreeing
    // about where the pointer is for a frame.
    //
    // Reports whether the window has focus into Input on the way, because
    // losing focus is the guaranteed way out of a locked pointer.
    static void ApplyCursorMode(Window& window);

    // GLFW delivers scroll through a callback rather than as pollable state, so
    // the window forwards it here and it is consumed by the next Poll.
    static void AccumulateScroll(float delta);
};

} // namespace Supersonic
