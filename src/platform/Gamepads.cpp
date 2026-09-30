#include "platform/Gamepads.hpp"

#include <GLFW/glfw3.h>

namespace Supersonic::Gamepads {

namespace {

// The present joysticks GLFW has a gamepad mapping for, in GLFW's id order.
int mappedIds(int (&ids)[kMaxGamepads]) {
    int count = 0;
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST && count < kMaxGamepads; ++jid) {
        if (glfwJoystickIsGamepad(jid) == GLFW_TRUE) ids[count++] = jid;
    }
    return count;
}

} // namespace

// Under Input::IgnoreLiveDevices (--hidden) there is no pad: XInput answers
// whoever holds one, focus or not, and that is not this run's player.
int Count() {
    if (Input::LiveDevicesIgnored()) return 0;
    int ids[kMaxGamepads]{};
    return mappedIds(ids);
}

bool Get(int index, GamepadState& out) {
    if (Input::LiveDevicesIgnored()) return false;
    int ids[kMaxGamepads]{};
    const int count = mappedIds(ids);
    if (index < 0 || index >= count) return false;

    GLFWgamepadstate state{};
    if (glfwGetGamepadState(ids[index], &state) != GLFW_TRUE) return false;

    // Pad's values mirror GLFW's (Input.hpp), so this is a copy.
    for (int button = 0; button < Pad::ButtonCount; ++button) {
        out.buttons[button] = state.buttons[button] == GLFW_PRESS;
    }
    for (int axis = 0; axis < Pad::AxisCount; ++axis) {
        out.axes[axis] = state.axes[axis];
    }
    return true;
}

} // namespace Supersonic::Gamepads
