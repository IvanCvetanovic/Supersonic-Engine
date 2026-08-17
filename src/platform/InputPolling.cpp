#include "platform/InputPolling.hpp"

#include <vector>

#include <GLFW/glfw3.h>

#include "platform/Window.hpp"

namespace Supersonic {

namespace {
float g_pendingScroll = 0.0f;

// The key codes GLFW defines, built once. Anything absent here is a code GLFW
// would reject rather than report as released.
const std::vector<int>& validKeys() {
    static const std::vector<int> keys = [] {
        std::vector<int> result;
        const auto add = [&result](int first, int last) {
            for (int key = first; key <= last; ++key) result.push_back(key);
        };
        add(GLFW_KEY_SPACE, GLFW_KEY_SPACE);
        add(GLFW_KEY_APOSTROPHE, GLFW_KEY_APOSTROPHE);
        add(GLFW_KEY_COMMA, GLFW_KEY_SLASH);          // , - . /
        add(GLFW_KEY_0, GLFW_KEY_9);
        add(GLFW_KEY_SEMICOLON, GLFW_KEY_SEMICOLON);
        add(GLFW_KEY_EQUAL, GLFW_KEY_EQUAL);
        add(GLFW_KEY_A, GLFW_KEY_RIGHT_BRACKET);      // A..Z plus [ \\ ]
        add(GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_GRAVE_ACCENT);
        add(GLFW_KEY_ESCAPE, GLFW_KEY_END);
        add(GLFW_KEY_CAPS_LOCK, GLFW_KEY_PAUSE);
        add(GLFW_KEY_F1, GLFW_KEY_F25);
        add(GLFW_KEY_KP_0, GLFW_KEY_KP_EQUAL);
        add(GLFW_KEY_LEFT_SHIFT, GLFW_KEY_MENU);
        return result;
    }();
    return keys;
}

// GLFW's key and button codes are what Key:: and MouseButton:: mirror, so the
// bridge is a straight copy. These asserts are what keep that true.
static_assert(Key::Space == GLFW_KEY_SPACE, "key codes must mirror GLFW");
static_assert(Key::W == GLFW_KEY_W, "key codes must mirror GLFW");
static_assert(Key::Escape == GLFW_KEY_ESCAPE, "key codes must mirror GLFW");
static_assert(Key::LeftShift == GLFW_KEY_LEFT_SHIFT, "key codes must mirror GLFW");
static_assert(Key::Last == GLFW_KEY_LAST, "key range must mirror GLFW");
static_assert(MouseButton::Left == GLFW_MOUSE_BUTTON_LEFT, "mouse codes must mirror GLFW");
static_assert(Pad::A == GLFW_GAMEPAD_BUTTON_A, "pad buttons must mirror GLFW");
static_assert(Pad::DpadLeft == GLFW_GAMEPAD_BUTTON_DPAD_LEFT, "pad buttons must mirror GLFW");
static_assert(Pad::ButtonCount == GLFW_GAMEPAD_BUTTON_LAST + 1, "pad button count must mirror GLFW");
static_assert(Pad::AxisCount == GLFW_GAMEPAD_AXIS_LAST + 1, "pad axis count must mirror GLFW");
} // namespace

void InputPolling::AccumulateScroll(float delta) {
    g_pendingScroll += delta;
}

void InputPolling::Poll(Window& window) {
    GLFWwindow* native = window.GetNativeWindow();
    if (!native) return;

    RawInputState state{};

    // Only the codes GLFW actually defines. Its key range is sparse - nothing
    // below 32, and gaps throughout - and glfwGetKey on an undefined code does
    // not return "released", it raises GLFW_INVALID_VALUE. Walking the range
    // blindly floods the error callback with hundreds of lines every frame.
    for (const int key : validKeys()) {
        state.keys[key] = glfwGetKey(native, key) == GLFW_PRESS;
    }

    for (int button = 0; button < MouseButton::Count; ++button) {
        state.mouseButtons[button] = glfwGetMouseButton(native, button) == GLFW_PRESS;
    }

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(native, &x, &y);
    state.mousePosition = glm::vec2(static_cast<float>(x), static_cast<float>(y));

    state.scroll = g_pendingScroll;
    g_pendingScroll = 0.0f;

    // The first gamepad only. Split-screen would want the rest, and the
    // snapshot has room for it, but a single pad is what one player needs.
    GLFWgamepadstate pad{};
    if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1) && glfwGetGamepadState(GLFW_JOYSTICK_1, &pad)) {
        state.padConnected = true;
        for (int button = 0; button < Pad::ButtonCount; ++button) {
            state.padButtons[button] = pad.buttons[button] == GLFW_PRESS;
        }
        for (int axis = 0; axis < Pad::AxisCount; ++axis) {
            state.padAxes[axis] = pad.axes[axis];
        }
    }

    Input::Update(state);
}

} // namespace Supersonic
