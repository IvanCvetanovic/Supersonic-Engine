#pragma once

#include "core/Input.hpp"

namespace Supersonic {

// Every connected gamepad, for a game that needs more than the first.
//
// Input's snapshot carries ONE pad, which is what one player needs; two players
// on one screen need two, and until this existed the only way to read the
// second was to call GLFW directly - which a game did, and which left it with
// nothing to call on a platform that has no GLFW.
//
// In Pad's standard layout on every platform: buttons by meaning (A is the
// bottom face button), sticks with y down, triggers resting at -1 and pressed
// at +1. A pad the platform cannot map to that layout is not listed.
//
// Main thread, inside the frame, like everything the polling layer reads.
// Desktop answers from GLFW (Gamepads.cpp), Android from the input events the
// activity delivered (platform/android/).
struct GamepadState {
    bool buttons[Pad::ButtonCount]{};
    float axes[Pad::AxisCount]{};
};

namespace Gamepads {

constexpr int kMaxGamepads = 4;

// How many pads are connected now, at most kMaxGamepads.
int Count();

// The index-th connected pad, in the order they were connected. False, and
// `out` untouched, for an index nobody has.
bool Get(int index, GamepadState& out);

} // namespace Gamepads

} // namespace Supersonic
