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

    // GLFW delivers scroll through a callback rather than as pollable state, so
    // the window forwards it here and it is consumed by the next Poll.
    static void AccumulateScroll(float delta);
};

} // namespace Supersonic
