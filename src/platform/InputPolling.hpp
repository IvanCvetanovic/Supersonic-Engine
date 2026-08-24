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

    // Registers the callbacks for the input GLFW does not let you poll.
    //
    // Must run BEFORE ImGui's GLFW backend is initialised. That backend keeps
    // whatever callback it displaces and calls it first on every event, so
    // installed before it both get everything; installed after it, ours
    // silently REPLACES ImGui's and every text box in the editor stops
    // accepting characters, with nothing anywhere reporting why. There is no
    // way to make that a compile error, so this logs one if it finds a callback
    // already installed.
    static void InstallCallbacks(Window& window);

    // Scroll and characters arrive by callback rather than as pollable state,
    // accumulate here, and are drained into the snapshot by the next Poll.
    static void AccumulateScroll(float delta);
    static void AccumulateCharacter(unsigned int codepoint);
};

} // namespace Supersonic
