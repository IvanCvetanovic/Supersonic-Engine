#include "platform/android/AndroidApp.hpp"

#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <dlfcn.h>
#include <jni.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

// Key codes only; nothing from GLFW is linked on Android (CMakeLists.txt).
#include <GLFW/glfw3.h>

#include "imgui.h"

#include "core/Log.hpp"
#include "core/WindowControl.hpp"
#include "platform/SafeArea.hpp"

namespace Supersonic::Android {

namespace {

constexpr const char* kLogTag = "Supersonic";

// How long frames keep running after the activity pauses, while it still has
// its window. A game that pauses itself when it loses focus has to run at
// least one tick that sees the focus gone, and Android can pause the activity
// BEFORE it reports the focus lost (on the API 33 emulator, pressing Home gave
// PAUSE and then LOST_FOCUS 270 ms later once, and the other order another
// time). Stopping at PAUSE, as the platform allows, meant no tick saw the
// focus go in the first case. A quarter of a second is fifteen ticks at 60 Hz,
// and the window is only taken away later still (TERM_WINDOW followed STOP by
// 700 ms), so these frames still have somewhere to draw.
constexpr double kLeavingGraceSeconds = 0.25;

// One finger, from the event that put it down to the snapshot after the one
// that lifted it.
struct Finger {
    int32_t id{-1};
    glm::vec2 position{0.0f};
    // In a snapshot already. A touch that lifts before it has been is kept for
    // exactly one more, so a tap too quick for any frame to see is still one
    // frame of contact rather than nothing.
    bool reported{false};
    bool lifted{false};
};

// One gamepad, keyed by the input device that sends it events.
struct PadSlot {
    int32_t deviceId{-1};
    GamepadState state{};
    // The d-pad as a hat (axis) rather than as keys: some pads send one, some
    // the other, some both, and the two must not release each other.
    bool hat[4]{};   // up, right, down, left
    // Latched as the keys are: pressed since the last snapshot, and what the
    // last snapshot latched, which every read of this frame sees (a game asks
    // Gamepads::Get once per tick, so the latch cannot be spent by one read).
    // A press and release between two frames is otherwise no press at all.
    bool pressed[Pad::ButtonCount]{};
    bool latched[Pad::ButtonCount]{};
};

struct State {
    android_app* app{nullptr};

    ANativeWindow* window{nullptr};
    bool resumed{false};
    bool focused{false};
    int width{0};
    int height{0};
    bool sizeChanged{false};
    bool resumedFromSuspend{false};
    double leavingUntil{0.0};   // Now() before which a paused app keeps running frames

    std::function<void()> surfaceLost;
    std::function<void(bool)> audioSuspend;

    std::array<bool, Key::Last + 1> keysDown{};
    std::array<bool, Key::Last + 1> keysPressed{};   // went down since the last snapshot
    std::vector<unsigned int> typed;

    std::vector<Finger> touches;   // in the order they went down
    int32_t primaryId{-1};        // the finger the mouse follows
    glm::vec2 mousePosition{0.0f};

    std::array<PadSlot, Gamepads::kMaxGamepads> pads{};
    int padCount{0};

    // RequestRefreshRate: whether a game asked, and the rate every window it
    // draws on is told (ANativeWindow_setFrameRate is per surface, and the
    // surface is a new one after each return from the background). 0 is no
    // preference.
    bool frameRateAsked{false};
    float frameRate{0.0f};
};

State g;

bool findTouchConst(int32_t id) {
    for (const Finger& touch : g.touches) {
        if (touch.id == id) return true;
    }
    return false;
}

// ---- Logging ------------------------------------------------------------------

// stdout and stderr go nowhere in an app process. The engine's log, and every
// std::cerr a game writes, are read by pumping both into logcat, one line per
// record: stdout at INFO, stderr at WARN, which is how Log::Submit splits them.
void pumpToLogcat(int fd, android_LogPriority priority) {
    std::string line;
    char buffer[1024];
    for (;;) {
        const ssize_t n = read(fd, buffer, sizeof buffer);
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; ++i) {
            if (buffer[i] == '\n') {
                __android_log_write(priority, kLogTag, line.c_str());
                line.clear();
            } else {
                line += buffer[i];
            }
        }
    }
}

void redirectStdioToLogcat() {
    static bool done = false;
    if (done) return;
    done = true;

    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    int out[2];
    int err[2];
    if (pipe(out) == 0) {
        dup2(out[1], STDOUT_FILENO);
        std::thread(pumpToLogcat, out[0], ANDROID_LOG_INFO).detach();
    }
    if (pipe(err) == 0) {
        dup2(err[1], STDERR_FILENO);
        std::thread(pumpToLogcat, err[0], ANDROID_LOG_WARN).detach();
    }
}

// ---- The window ---------------------------------------------------------------

void refreshWindowSize() {
    if (!g.window) return;
    const int width = ANativeWindow_getWidth(g.window);
    const int height = ANativeWindow_getHeight(g.window);
    if (width > 0 && height > 0 && (width != g.width || height != g.height)) {
        g.width = width;
        g.height = height;
        g.sizeChanged = true;
    }
}

// ---- Input ----------------------------------------------------------------------

// Everything held is let go: a finger on the glass when the notification shade
// came down never reports lifting, and a key held across a pause would stay
// held in the game until pressed again.
void releaseAllInput() {
    g.keysDown.fill(false);
    // A finger no frame has seen yet still gets its one frame; the rest end now.
    g.touches.erase(std::remove_if(g.touches.begin(), g.touches.end(),
                                   [](const Finger& t) { return t.reported; }),
                    g.touches.end());
    for (Finger& touch : g.touches) touch.lifted = true;
    if (!findTouchConst(g.primaryId)) g.primaryId = -1;
    for (PadSlot& pad : g.pads) {
        std::fill(std::begin(pad.state.buttons), std::end(pad.state.buttons), false);
        std::fill(std::begin(pad.hat), std::end(pad.hat), false);
        std::fill(std::begin(pad.pressed), std::end(pad.pressed), false);
    }
}

// Android's key codes, as the GLFW codes Input speaks (Input.hpp mirrors GLFW).
// -1 for a key the engine has no name for.
int glfwKeyFor(int32_t keyCode) {
    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) return GLFW_KEY_A + (keyCode - AKEYCODE_A);
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) return GLFW_KEY_0 + (keyCode - AKEYCODE_0);
    if (keyCode >= AKEYCODE_F1 && keyCode <= AKEYCODE_F12) return GLFW_KEY_F1 + (keyCode - AKEYCODE_F1);
    switch (keyCode) {
    // The system back button and gesture are the only "cancel" a phone has.
    case AKEYCODE_BACK: return GLFW_KEY_ESCAPE;
    case AKEYCODE_ESCAPE: return GLFW_KEY_ESCAPE;
    case AKEYCODE_ENTER: return GLFW_KEY_ENTER;
    case AKEYCODE_NUMPAD_ENTER: return GLFW_KEY_KP_ENTER;
    case AKEYCODE_DPAD_CENTER: return GLFW_KEY_ENTER;
    case AKEYCODE_SPACE: return GLFW_KEY_SPACE;
    case AKEYCODE_TAB: return GLFW_KEY_TAB;
    case AKEYCODE_DEL: return GLFW_KEY_BACKSPACE;
    case AKEYCODE_FORWARD_DEL: return GLFW_KEY_DELETE;
    case AKEYCODE_INSERT: return GLFW_KEY_INSERT;
    case AKEYCODE_MOVE_HOME: return GLFW_KEY_HOME;
    case AKEYCODE_MOVE_END: return GLFW_KEY_END;
    case AKEYCODE_PAGE_UP: return GLFW_KEY_PAGE_UP;
    case AKEYCODE_PAGE_DOWN: return GLFW_KEY_PAGE_DOWN;
    case AKEYCODE_DPAD_UP: return GLFW_KEY_UP;
    case AKEYCODE_DPAD_DOWN: return GLFW_KEY_DOWN;
    case AKEYCODE_DPAD_LEFT: return GLFW_KEY_LEFT;
    case AKEYCODE_DPAD_RIGHT: return GLFW_KEY_RIGHT;
    case AKEYCODE_SHIFT_LEFT: return GLFW_KEY_LEFT_SHIFT;
    case AKEYCODE_SHIFT_RIGHT: return GLFW_KEY_RIGHT_SHIFT;
    case AKEYCODE_CTRL_LEFT: return GLFW_KEY_LEFT_CONTROL;
    case AKEYCODE_CTRL_RIGHT: return GLFW_KEY_RIGHT_CONTROL;
    case AKEYCODE_ALT_LEFT: return GLFW_KEY_LEFT_ALT;
    case AKEYCODE_ALT_RIGHT: return GLFW_KEY_RIGHT_ALT;
    case AKEYCODE_MINUS: return GLFW_KEY_MINUS;
    case AKEYCODE_EQUALS: return GLFW_KEY_EQUAL;
    case AKEYCODE_COMMA: return GLFW_KEY_COMMA;
    case AKEYCODE_PERIOD: return GLFW_KEY_PERIOD;
    case AKEYCODE_SLASH: return GLFW_KEY_SLASH;
    case AKEYCODE_SEMICOLON: return GLFW_KEY_SEMICOLON;
    case AKEYCODE_APOSTROPHE: return GLFW_KEY_APOSTROPHE;
    case AKEYCODE_LEFT_BRACKET: return GLFW_KEY_LEFT_BRACKET;
    case AKEYCODE_RIGHT_BRACKET: return GLFW_KEY_RIGHT_BRACKET;
    case AKEYCODE_BACKSLASH: return GLFW_KEY_BACKSLASH;
    case AKEYCODE_GRAVE: return GLFW_KEY_GRAVE_ACCENT;
    case AKEYCODE_NUMPAD_ADD: return GLFW_KEY_KP_ADD;
    case AKEYCODE_NUMPAD_SUBTRACT: return GLFW_KEY_KP_SUBTRACT;
    case AKEYCODE_BREAK: return GLFW_KEY_PAUSE;
    case AKEYCODE_SYSRQ: return GLFW_KEY_PRINT_SCREEN;
    default: return -1;
    }
}

// What a hardware keyboard types, for the few printable keys a name needs.
// The NDK carries no key-to-character map (that is KeyCharacterMap, in Java),
// so this is US ASCII: letters, digits, space and basic punctuation, shifted
// by the Shift state. 0 for anything else.
unsigned int characterFor(int32_t keyCode, int32_t metaState) {
    const bool shift = (metaState & AMETA_SHIFT_ON) != 0;
    const bool caps = (metaState & AMETA_CAPS_LOCK_ON) != 0;
    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
        const unsigned int base = (shift != caps) ? 'A' : 'a';
        return base + static_cast<unsigned int>(keyCode - AKEYCODE_A);
    }
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9 && !shift) {
        return '0' + static_cast<unsigned int>(keyCode - AKEYCODE_0);
    }
    switch (keyCode) {
    case AKEYCODE_SPACE: return ' ';
    case AKEYCODE_MINUS: return shift ? '_' : '-';
    case AKEYCODE_EQUALS: return shift ? '+' : '=';
    case AKEYCODE_COMMA: return shift ? '<' : ',';
    case AKEYCODE_PERIOD: return shift ? '>' : '.';
    case AKEYCODE_SLASH: return shift ? '?' : '/';
    case AKEYCODE_SEMICOLON: return shift ? ':' : ';';
    case AKEYCODE_APOSTROPHE: return shift ? '"' : '\'';
    default: return 0;
    }
}

// A gamepad's key, as a Pad button. -1 for one that is not a pad button.
int padButtonFor(int32_t keyCode) {
    switch (keyCode) {
    case AKEYCODE_BUTTON_A: return Pad::A;
    case AKEYCODE_BUTTON_B: return Pad::B;
    case AKEYCODE_BUTTON_X: return Pad::X;
    case AKEYCODE_BUTTON_Y: return Pad::Y;
    case AKEYCODE_BUTTON_L1: return Pad::LeftBumper;
    case AKEYCODE_BUTTON_R1: return Pad::RightBumper;
    case AKEYCODE_BUTTON_SELECT: return Pad::Back;
    case AKEYCODE_BUTTON_START: return Pad::Start;
    case AKEYCODE_BUTTON_MODE: return Pad::Guide;
    case AKEYCODE_BUTTON_THUMBL: return Pad::LeftThumb;
    case AKEYCODE_BUTTON_THUMBR: return Pad::RightThumb;
    case AKEYCODE_DPAD_UP: return Pad::DpadUp;
    case AKEYCODE_DPAD_RIGHT: return Pad::DpadRight;
    case AKEYCODE_DPAD_DOWN: return Pad::DpadDown;
    case AKEYCODE_DPAD_LEFT: return Pad::DpadLeft;
    default: return -1;
    }
}

// The slot for a device, taking the next free one the first time it is heard
// from. The NDK reports no connect or disconnect, so a pad is connected from
// its first event for the rest of the run.
PadSlot* padFor(int32_t deviceId) {
    for (int i = 0; i < g.padCount; ++i) {
        if (g.pads[static_cast<std::size_t>(i)].deviceId == deviceId) return &g.pads[static_cast<std::size_t>(i)];
    }
    if (g.padCount >= Gamepads::kMaxGamepads) return nullptr;
    PadSlot& slot = g.pads[static_cast<std::size_t>(g.padCount++)];
    slot = PadSlot{};
    slot.deviceId = deviceId;
    // Triggers rest at -1 (Input.hpp's Pad), not at the zero a fresh array holds.
    slot.state.axes[Pad::LeftTrigger] = -1.0f;
    slot.state.axes[Pad::RightTrigger] = -1.0f;
    SUPERSONIC_LOG_INFO("Android") << "Gamepad " << g.padCount << " connected (input device " << deviceId << ").";
    return &slot;
}

bool isPadSource(int32_t source) {
    return (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD ||
           (source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK;
}

int32_t handleKey(const AInputEvent* event) {
    const int32_t keyCode = AKeyEvent_getKeyCode(event);
    const int32_t action = AKeyEvent_getAction(event);
    const int32_t source = AInputEvent_getSource(event);

    // The volume keys are the system's; claimed, they would stop working.
    if (keyCode == AKEYCODE_VOLUME_UP || keyCode == AKEYCODE_VOLUME_DOWN || keyCode == AKEYCODE_VOLUME_MUTE) {
        return 0;
    }
    if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) return 0;
    const bool down = action == AKEY_EVENT_ACTION_DOWN;

    if (isPadSource(source) && keyCode != AKEYCODE_BACK) {
        const int button = padButtonFor(keyCode);
        if (button >= 0) {
            if (PadSlot* pad = padFor(AInputEvent_getDeviceId(event))) {
                pad->state.buttons[button] = down;
                if (down) pad->pressed[button] = true;
            }
            return 1;
        }
        // The analogue triggers of a pad that also reports them as keys.
        if (keyCode == AKEYCODE_BUTTON_L2 || keyCode == AKEYCODE_BUTTON_R2) {
            if (PadSlot* pad = padFor(AInputEvent_getDeviceId(event))) {
                pad->state.axes[keyCode == AKEYCODE_BUTTON_L2 ? Pad::LeftTrigger : Pad::RightTrigger] =
                    down ? 1.0f : -1.0f;
            }
            return 1;
        }
    }

    const int key = glfwKeyFor(keyCode);
    if (key < 0) return 0;
    g.keysDown[static_cast<std::size_t>(key)] = down;
    if (down) {
        g.keysPressed[static_cast<std::size_t>(key)] = true;
        const unsigned int c = characterFor(keyCode, AKeyEvent_getMetaState(event));
        if (c != 0 && g.typed.size() < static_cast<std::size_t>(Text::kMaxCharacters)) g.typed.push_back(c);
    }
    // Back is claimed whatever the engine does with it: unclaimed, the system
    // finishes the activity.
    return 1;
}

float axis(const AInputEvent* event, int32_t which) { return AMotionEvent_getAxisValue(event, which, 0); }

int32_t handlePadMotion(const AInputEvent* event) {
    PadSlot* pad = padFor(AInputEvent_getDeviceId(event));
    if (!pad) return 1;
    GamepadState& s = pad->state;
    // Android's sticks are already y down, as GLFW's and Pad's are.
    s.axes[Pad::LeftX] = axis(event, AMOTION_EVENT_AXIS_X);
    s.axes[Pad::LeftY] = axis(event, AMOTION_EVENT_AXIS_Y);
    s.axes[Pad::RightX] = axis(event, AMOTION_EVENT_AXIS_Z);
    s.axes[Pad::RightY] = axis(event, AMOTION_EVENT_AXIS_RZ);
    // 0..1 here; Pad's triggers run -1..1. Some pads report BRAKE/GAS instead.
    const float left = std::max(axis(event, AMOTION_EVENT_AXIS_LTRIGGER), axis(event, AMOTION_EVENT_AXIS_BRAKE));
    const float right = std::max(axis(event, AMOTION_EVENT_AXIS_RTRIGGER), axis(event, AMOTION_EVENT_AXIS_GAS));
    s.axes[Pad::LeftTrigger] = std::clamp(left, 0.0f, 1.0f) * 2.0f - 1.0f;
    s.axes[Pad::RightTrigger] = std::clamp(right, 0.0f, 1.0f) * 2.0f - 1.0f;
    const float hatX = axis(event, AMOTION_EVENT_AXIS_HAT_X);
    const float hatY = axis(event, AMOTION_EVENT_AXIS_HAT_Y);
    pad->hat[0] = hatY < -0.5f;
    pad->hat[1] = hatX > 0.5f;
    pad->hat[2] = hatY > 0.5f;
    pad->hat[3] = hatX < -0.5f;
    return 1;
}

Finger* findTouch(int32_t id) {
    for (Finger& touch : g.touches) {
        if (touch.id == id) return &touch;
    }
    return nullptr;
}

void pointerDown(int32_t id, glm::vec2 position) {
    if (Finger* existing = findTouch(id)) {
        // Down again before the snapshot that would have ended it: the same
        // finger, still down, as far as any frame can tell.
        existing->position = position;
        existing->lifted = false;
        return;
    }
    Finger touch;
    touch.id = id;
    touch.position = position;
    g.touches.push_back(touch);
}

void pointerUp(int32_t id) {
    Finger* touch = findTouch(id);
    if (!touch) return;
    if (touch->reported) {
        g.touches.erase(g.touches.begin() + (touch - g.touches.data()));
        if (id == g.primaryId) g.primaryId = -1;
    } else {
        touch->lifted = true;
    }
}

// ImGui has no platform backend here, so the pointer it hit-tests with (the
// engine's own UI canvas reads io.MousePos) is fed from the same finger the
// mouse follows.
void feedImGuiPointer(bool buttonDown) {
    if (ImGui::GetCurrentContext() == nullptr) return;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(g.mousePosition.x, g.mousePosition.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, buttonDown);
}

int32_t handlePointerMotion(const AInputEvent* event) {
    const int32_t action = AMotionEvent_getAction(event);
    const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
    const auto index = static_cast<size_t>((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                                           AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    const size_t count = AMotionEvent_getPointerCount(event);
    const auto positionOf = [event](size_t i) {
        return glm::vec2(AMotionEvent_getX(event, i), AMotionEvent_getY(event, i));
    };

    switch (masked) {
    case AMOTION_EVENT_ACTION_DOWN: {
        // The first finger of a gesture. It is the one the mouse follows until
        // it lifts, as Android's own pointer emulation does; later fingers are
        // contacts only.
        const int32_t id = AMotionEvent_getPointerId(event, 0);
        g.primaryId = id;
        g.mousePosition = positionOf(0);
        pointerDown(id, positionOf(0));
        feedImGuiPointer(true);
        break;
    }
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
        if (index < count) pointerDown(AMotionEvent_getPointerId(event, index), positionOf(index));
        break;
    case AMOTION_EVENT_ACTION_MOVE:
        for (size_t i = 0; i < count; ++i) {
            const int32_t id = AMotionEvent_getPointerId(event, i);
            if (Finger* touch = findTouch(id)) touch->position = positionOf(i);
            if (id == g.primaryId) g.mousePosition = positionOf(i);
        }
        if (g.primaryId >= 0) feedImGuiPointer(true);
        break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP: {
        const size_t which = masked == AMOTION_EVENT_ACTION_UP ? 0 : index;
        if (which < count) {
            const int32_t id = AMotionEvent_getPointerId(event, which);
            if (Finger* touch = findTouch(id)) touch->position = positionOf(which);
            // The mouse stays where the finger left the glass: moved anywhere
            // else, the game would read the lift as the pointer moving.
            if (id == g.primaryId) {
                g.mousePosition = positionOf(which);
                feedImGuiPointer(false);
            }
            pointerUp(id);
        }
        break;
    }
    case AMOTION_EVENT_ACTION_CANCEL:
        // The system took the gesture (an edge swipe, a dialog). Every finger
        // ends where it was.
        for (size_t i = 0; i < count; ++i) pointerUp(AMotionEvent_getPointerId(event, i));
        if (g.primaryId >= 0) feedImGuiPointer(false);
        break;
    case AMOTION_EVENT_ACTION_HOVER_MOVE:
        // A mouse or stylus over the screen without touching it.
        g.mousePosition = positionOf(0);
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::GetIO().AddMousePosEvent(g.mousePosition.x, g.mousePosition.y);
        }
        break;
    default:
        return 0;
    }
    return 1;
}

int32_t onInputEvent(android_app* /*app*/, AInputEvent* event) {
    switch (AInputEvent_getType(event)) {
    case AINPUT_EVENT_TYPE_KEY:
        return handleKey(event);
    case AINPUT_EVENT_TYPE_MOTION: {
        const int32_t source = AInputEvent_getSource(event);
        if ((source & AINPUT_SOURCE_CLASS_JOYSTICK) != 0) return handlePadMotion(event);
        if ((source & AINPUT_SOURCE_CLASS_POINTER) != 0) return handlePointerMotion(event);
        return 0;
    }
    default:
        return 0;
    }
}

// ---- Lifecycle ---------------------------------------------------------------

const char* commandName(int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW: return "INIT_WINDOW";
    case APP_CMD_TERM_WINDOW: return "TERM_WINDOW";
    case APP_CMD_WINDOW_RESIZED: return "WINDOW_RESIZED";
    case APP_CMD_CONTENT_RECT_CHANGED: return "CONTENT_RECT_CHANGED";
    case APP_CMD_CONFIG_CHANGED: return "CONFIG_CHANGED";
    case APP_CMD_GAINED_FOCUS: return "GAINED_FOCUS";
    case APP_CMD_LOST_FOCUS: return "LOST_FOCUS";
    case APP_CMD_START: return "START";
    case APP_CMD_RESUME: return "RESUME";
    case APP_CMD_PAUSE: return "PAUSE";
    case APP_CMD_STOP: return "STOP";
    case APP_CMD_DESTROY: return "DESTROY";
    case APP_CMD_LOW_MEMORY: return "LOW_MEMORY";
    case APP_CMD_SAVE_STATE: return "SAVE_STATE";
    default: return nullptr;
    }
}

// ---- The refresh rate ---------------------------------------------------------

// ANativeWindow_setFrameRate is API 30's; the library is built for API 26, so
// it is looked up rather than linked, and a null answer is an older system.
using SetFrameRateFunction = int32_t (*)(ANativeWindow*, float, int8_t);

SetFrameRateFunction setFrameRateFunction() {
    static const SetFrameRateFunction function = []() -> SetFrameRateFunction {
        void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (!library) return nullptr;
        return reinterpret_cast<SetFrameRateFunction>(dlsym(library, "ANativeWindow_setFrameRate"));
    }();
    return function;
}

// The rate RequestRefreshRate settled on, told to the window there is now.
void applyFrameRate() {
    if (!g.window || !g.frameRateAsked) return;
    const SetFrameRateFunction function = setFrameRateFunction();
    if (!function) {
        SUPERSONIC_LOG_INFO("Android") << "Frame rate: ANativeWindow_setFrameRate needs API 30; only the display "
                                          "mode was asked for.";
        return;
    }
    // ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT: a game's own frames,
    // not a video's fixed rate the display would have to match exactly.
    constexpr int8_t kCompatibilityDefault = 0;
    const int32_t result = function(g.window, g.frameRate, kCompatibilityDefault);
    SUPERSONIC_LOG_INFO("Android") << "Frame rate: " << g.frameRate << " Hz set on the window "
                                   << (g.frameRate > 0.0f ? "" : "(no preference) ")
                                   << "(ANativeWindow_setFrameRate returned " << result << ").";
}

// SupersonicActivity.requestRefreshRate(hz) through JNI, from the game's
// thread: the rate of the display mode it made the window's preferred one,
// 0 when it chose none. The activity by the object the glue holds, not by
// FindClass, which on a native thread sees only the system's classes.
float askActivityForRefreshRate(float hz) {
    if (!g.app || !g.app->activity || !g.app->activity->vm || !g.app->activity->clazz) return 0.0f;
    JavaVM* vm = g.app->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    const jint state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return 0.0f;
        attached = true;
    } else if (state != JNI_OK) {
        return 0.0f;
    }

    float chosen = 0.0f;
    jobject activity = g.app->activity->clazz;
    jclass type = env->GetObjectClass(activity);
    jmethodID method = type ? env->GetMethodID(type, "requestRefreshRate", "(F)F") : nullptr;
    if (!method) {
        // A plain NativeActivity: no such method, and a pending exception
        // that must not reach the next JNI call.
        env->ExceptionClear();
        SUPERSONIC_LOG_INFO("Android") << "Refresh rate: the activity is not SupersonicActivity; the display mode "
                                          "is left to the system.";
    } else {
        chosen = env->CallFloatMethod(activity, method, static_cast<jfloat>(hz));
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            chosen = 0.0f;
            SUPERSONIC_LOG_WARN("Android") << "Refresh rate: the activity threw; the display mode is left to the "
                                              "system.";
        }
    }
    if (type) env->DeleteLocalRef(type);
    if (attached) vm->DetachCurrentThread();
    return chosen;
}

void onAppCmd(android_app* app, int32_t cmd) {
    if (const char* name = commandName(cmd)) {
        SUPERSONIC_LOG_INFO("Android") << "Lifecycle: " << name << ".";
    }
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        g.window = app->window;
        g.width = 0;
        g.height = 0;
        refreshWindowSize();
        // A new surface knows nothing of the rate the last one was told.
        applyFrameRate();
        break;
    case APP_CMD_TERM_WINDOW:
        // Before the glue lets the window go: everything made from it goes
        // first, or the swapchain is presenting to freed memory.
        if (g.surfaceLost) g.surfaceLost();
        g.window = nullptr;
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONTENT_RECT_CHANGED:
    case APP_CMD_CONFIG_CHANGED:
    case APP_CMD_WINDOW_REDRAW_NEEDED:
        refreshWindowSize();
        break;
    case APP_CMD_GAINED_FOCUS:
        g.focused = true;
        break;
    case APP_CMD_LOST_FOCUS:
        g.focused = false;
        releaseAllInput();
        break;
    case APP_CMD_RESUME:
        g.resumed = true;
        if (g.audioSuspend) g.audioSuspend(false);
        break;
    case APP_CMD_PAUSE:
        g.resumed = false;
        // Unfocused from here, not only from LOST_FOCUS, which follows later:
        // the app is leaving the screen, and the frames it still runs (see
        // kLeavingGraceSeconds) are how the game finds out.
        g.focused = false;
        g.leavingUntil = Now() + kLeavingGraceSeconds;
        if (g.audioSuspend) g.audioSuspend(true);
        releaseAllInput();
        break;
    default:
        break;
    }
}

void pollOnce(int timeoutMs) {
    int events = 0;
    android_poll_source* source = nullptr;
    const int ident = ALooper_pollOnce(timeoutMs, nullptr, &events, reinterpret_cast<void**>(&source));
    if (ident >= 0 && source != nullptr) source->process(g.app, source);
}

// Everything already delivered, without waiting for more.
void drainPending() {
    // Bounded, so a looper that never runs dry cannot hold the frame forever.
    for (int handled = 0; handled < 512; ++handled) {
        int events = 0;
        android_poll_source* source = nullptr;
        const int ident = ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void**>(&source));
        if (ident == ALOOPER_POLL_TIMEOUT || ident == ALOOPER_POLL_ERROR) break;
        if (ident >= 0 && source != nullptr) source->process(g.app, source);
        if (g.app->destroyRequested) break;
    }
}

} // namespace

android_app* App() { return g.app; }

std::string InternalDataPath() {
    if (!g.app || !g.app->activity || !g.app->activity->internalDataPath) return {};
    return g.app->activity->internalDataPath;
}

std::string ExternalDataPath() {
    if (!g.app || !g.app->activity || !g.app->activity->externalDataPath) return {};
    return g.app->activity->externalDataPath;
}

bool PumpEvents() {
    if (!g.app) return false;
    drainPending();
    return g.app->destroyRequested == 0;
}

// JNI's NewStringUTF takes MODIFIED UTF-8: a standard four-byte sequence (outside the
// BMP) or an embedded NUL is not valid in it, and CheckJNI - on for every debuggable
// app and every emulator - aborts the process on them. Device names and exception
// texts are ASCII in practice; anything else of that kind becomes '?'.
static std::string forNewStringUTF(const std::string& utf8) {
    std::string out;
    out.reserve(utf8.size());
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c == 0) {
            out += '?';
            ++i;
        } else if (c >= 0xF0) {
            out += '?';
            ++i;
            for (int skipped = 0; skipped < 3 && i < utf8.size() && (static_cast<unsigned char>(utf8[i]) & 0xC0) == 0x80;
                 ++skipped) {
                ++i;
            }
        } else {
            out += static_cast<char>(c);
            ++i;
        }
    }
    return out;
}

bool ShowMessage(const std::string& title, const std::string& utf8Text) {
    SUPERSONIC_LOG_INFO("Android") << "Message for the player: " << title << ": " << utf8Text;
    if (!g.app || !g.app->activity || !g.app->activity->vm || !g.app->activity->clazz) return false;
    JavaVM* vm = g.app->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    const jint state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        attached = true;
    } else if (state != JNI_OK) {
        return false;
    }

    bool shown = false;
    jobject activity = g.app->activity->clazz;
    jclass type = env->GetObjectClass(activity);
    // Each lookup on its own, the exception of a missing one cleared before the next call:
    // CheckJNI aborts on any JNI call made with an exception pending.
    jmethodID show = type ? env->GetMethodID(type, "showMessage", "(Ljava/lang/String;Ljava/lang/String;)V") : nullptr;
    env->ExceptionClear();
    jmethodID isOpen = type ? env->GetMethodID(type, "isMessageOpen", "()Z") : nullptr;
    env->ExceptionClear();
    if (!show || !isOpen) {
        // A plain NativeActivity: no such methods.
        SUPERSONIC_LOG_INFO("Android") << "Message: the activity is not SupersonicActivity; nothing is shown.";
    } else {
        jstring jTitle = env->NewStringUTF(forNewStringUTF(title).c_str());
        jstring jText = env->NewStringUTF(forNewStringUTF(utf8Text).c_str());
        bool threw = jTitle == nullptr || jText == nullptr;   // out of memory: nothing to pass on
        if (threw) {
            env->ExceptionClear();
        } else {
            env->CallVoidMethod(activity, show, jTitle, jText);
            threw = env->ExceptionCheck();
            if (threw) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        }
        if (jTitle) env->DeleteLocalRef(jTitle);
        if (jText) env->DeleteLocalRef(jText);
        if (!threw) {
            shown = true;
            // The dialog is the UI thread's; this thread only has to keep
            // reading its own looper (an activity that stops reading for five
            // seconds is "not responding") until the player closes it.
            const auto start = std::chrono::steady_clock::now();
            while (PumpEvents() && std::chrono::steady_clock::now() - start < std::chrono::minutes(5)) {
                const jboolean open = env->CallBooleanMethod(activity, isOpen);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    break;
                }
                if (open == JNI_FALSE) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }
    if (type) env->DeleteLocalRef(type);
    if (attached) vm->DetachCurrentThread();
    return shown;
}

ANativeWindow* CurrentWindow() { return g.window; }

bool DestroyRequested() { return !g.app || g.app->destroyRequested != 0; }

bool IsFocused() { return g.focused; }

void WaitUntilDrawable() {
    if (!g.app) return;
    drainPending();
    // Paused, but not yet for long: a few more frames, unfocused, while the
    // window lasts (kLeavingGraceSeconds says why).
    if (!g.app->destroyRequested && !g.resumed && g.window && Now() < g.leavingUntil) return;
    bool waited = false;
    while (!g.app->destroyRequested && !(g.resumed && g.window)) {
        if (!waited) SUPERSONIC_LOG_INFO("Android") << "In the background: the frame waits.";
        waited = true;
        pollOnce(-1);
    }
    if (waited) {
        g.resumedFromSuspend = true;
        if (!g.app->destroyRequested) SUPERSONIC_LOG_INFO("Android") << "Back in the foreground.";
    }
    // The window's size, asked every frame rather than only when a command
    // says it changed - a guard, not a measured fix. Seen once, on an emulator
    // launched into a boot storm: the window was 1x1 at INIT_WINDOW, the
    // WINDOW_RESIZED that followed still read 1x1, and nothing said so again,
    // so the swapchain stayed 1x1 (the system was wedged at the time as well,
    // for reasons that were not this app's). Asking every frame means a size
    // that changes without a command is still picked up. Two cheap local
    // queries.
    refreshWindowSize();
}

void WindowSize(int& width, int& height) {
    width = g.window ? g.width : 0;
    height = g.window ? g.height : 0;
}

bool TakeWindowSizeChanged() {
    const bool changed = g.sizeChanged;
    g.sizeChanged = false;
    return changed;
}

bool TakeResumedFromSuspend() {
    const bool resumed = g.resumedFromSuspend;
    g.resumedFromSuspend = false;
    return resumed;
}

void SetSurfaceLostCallback(std::function<void()> callback) { g.surfaceLost = std::move(callback); }

void SetAudioSuspendHandler(std::function<void(bool)> handler) {
    g.audioSuspend = std::move(handler);
    // A handler installed while the app is already in the background starts
    // suspended, rather than playing until the next pause.
    if (g.audioSuspend && g.app && !g.resumed) g.audioSuspend(true);
}

void FillRawInput(RawInputState& state) {
    for (std::size_t key = 0; key < g.keysDown.size(); ++key) {
        state.keys[key] = g.keysDown[key] || g.keysPressed[key];
    }
    g.keysPressed.fill(false);

    // Every finger that is down, or went down since the last snapshot, in the
    // order they landed. The mouse is the first finger of the gesture: its
    // button is down for exactly as long as that finger is in a snapshot, which
    // is what makes a tap on a menu a click. Input::SynthesiseMouseContact is
    // NOT called - its own comment forbids it once a platform has real contacts.
    int count = 0;
    bool primaryDown = false;
    for (Finger& touch : g.touches) {
        if (touch.id == g.primaryId) primaryDown = true;
        if (count < Touch::kMaxContacts) {
            state.contacts[count].id = touch.id;
            state.contacts[count].position = touch.position;
            ++count;
        }
        touch.reported = true;
    }
    state.contactCount = count;
    state.mouseButtons[MouseButton::Left] = primaryDown;
    state.mousePosition = g.mousePosition;

    // Lifted fingers have had their frame.
    for (const Finger& touch : g.touches) {
        if (touch.lifted && touch.id == g.primaryId) g.primaryId = -1;
    }
    g.touches.erase(std::remove_if(g.touches.begin(), g.touches.end(), [](const Finger& t) { return t.lifted; }),
                    g.touches.end());

    const int typed = std::min(static_cast<int>(g.typed.size()), Text::kMaxCharacters);
    for (int i = 0; i < typed; ++i) state.textCharacters[i] = g.typed[static_cast<std::size_t>(i)];
    state.textCharacterCount = typed;
    g.typed.clear();

    // This frame's pad presses: what went down since the last snapshot, held
    // for every read until the next one.
    for (PadSlot& pad : g.pads) {
        std::copy(std::begin(pad.pressed), std::end(pad.pressed), std::begin(pad.latched));
        std::fill(std::begin(pad.pressed), std::end(pad.pressed), false);
    }

    GamepadState first{};
    if (GetGamepad(0, first)) {
        state.padConnected = true;
        for (int button = 0; button < Pad::ButtonCount; ++button) state.padButtons[button] = first.buttons[button];
        for (int a = 0; a < Pad::AxisCount; ++a) state.padAxes[a] = first.axes[a];
    }
}

void RequestRefreshRate(uint32_t refreshRate) {
    // The activity's terms: below 0 no preference, 0 the highest, else Hz.
    float ask = -1.0f;
    if (refreshRate == WindowControl::kHighestRefreshRate) {
        ask = 0.0f;
    } else if (refreshRate != WindowControl::kDesktopRefreshRate) {
        ask = static_cast<float>(refreshRate);
    }
    const float chosen = askActivityForRefreshRate(ask);

    // The surface is told the mode's own rate, which is what the display will
    // run at; the number asked for when the activity could not say.
    g.frameRateAsked = true;
    g.frameRate = ask < 0.0f ? 0.0f : chosen > 0.0f ? chosen : ask;
    SUPERSONIC_LOG_INFO("Android") << "Refresh rate: asked for "
                                   << (ask < 0.0f ? std::string("no preference")
                                       : ask == 0.0f ? std::string("the highest")
                                                     : std::to_string(refreshRate) + " Hz")
                                   << "; the display mode chosen runs at " << chosen << " Hz.";
    applyFrameRate();
}

int GamepadCount() { return g.padCount; }

bool GetGamepad(int index, GamepadState& out) {
    if (index < 0 || index >= g.padCount) return false;
    const PadSlot& pad = g.pads[static_cast<std::size_t>(index)];
    out = pad.state;
    for (int button = 0; button < Pad::ButtonCount; ++button) {
        out.buttons[button] = out.buttons[button] || pad.latched[button];
    }
    out.buttons[Pad::DpadUp] = out.buttons[Pad::DpadUp] || pad.hat[0];
    out.buttons[Pad::DpadRight] = out.buttons[Pad::DpadRight] || pad.hat[1];
    out.buttons[Pad::DpadDown] = out.buttons[Pad::DpadDown] || pad.hat[2];
    out.buttons[Pad::DpadLeft] = out.buttons[Pad::DpadLeft] || pad.hat[3];
    return true;
}

double Now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

} // namespace Supersonic::Android

// ---- The safe area --------------------------------------------------------------

namespace {
// Written by the activity's UI thread (SupersonicActivity.java, whenever the
// window's insets change) and read by the game's, so each edge is an atomic.
// Four separate stores can be seen half-updated for a frame; the next frame
// reads the whole of it, which is as soon as anything could act on it anyway.
std::atomic<int> g_safeLeft{0};
std::atomic<int> g_safeTop{0};
std::atomic<int> g_safeRight{0};
std::atomic<int> g_safeBottom{0};
} // namespace

namespace Supersonic::SafeArea {

SafeAreaInsets Get() {
    SafeAreaInsets insets;
    insets.left = static_cast<float>(g_safeLeft.load(std::memory_order_relaxed));
    insets.top = static_cast<float>(g_safeTop.load(std::memory_order_relaxed));
    insets.right = static_cast<float>(g_safeRight.load(std::memory_order_relaxed));
    insets.bottom = static_cast<float>(g_safeBottom.load(std::memory_order_relaxed));
    return insets;
}

} // namespace Supersonic::SafeArea

// Called by SupersonicActivity (src/platform/android/java/) on the UI thread,
// in the decor view's pixels, which are the window's: the ANativeWindow is the
// activity's whole window.
extern "C" JNIEXPORT void JNICALL Java_com_ivancvetanovic_supersonic_SupersonicActivity_nativeSetSafeArea(
    JNIEnv* /*env*/, jclass /*type*/, jint left, jint top, jint right, jint bottom) {
    const auto clean = [](jint value) { return value > 0 ? static_cast<int>(value) : 0; };
    const int l = clean(left);
    const int t = clean(top);
    const int r = clean(right);
    const int b = clean(bottom);
    if (l == g_safeLeft.load() && t == g_safeTop.load() && r == g_safeRight.load() && b == g_safeBottom.load()) return;
    g_safeLeft.store(l);
    g_safeTop.store(t);
    g_safeRight.store(r);
    g_safeBottom.store(b);
    SUPERSONIC_LOG_INFO("Android") << "Safe area: left " << l << ", top " << t << ", right " << r << ", bottom " << b
                                   << " (window pixels).";
}

// ---- The process's entry ------------------------------------------------------

extern "C" void android_main(android_app* app) {
    using namespace Supersonic::Android;
    g.app = app;
    app->onAppCmd = onAppCmd;
    app->onInputEvent = onInputEvent;

    redirectStdioToLogcat();

    // The status bar hidden and the screen kept on while the game runs. Both
    // are window flags, which the activity applies on its own thread. The
    // navigation bar is the UI thread's to hide (called from here through JNI,
    // it raised a Java exception), so an activity that is
    // SupersonicActivity (java/ beside this file) hides both bars itself and
    // keeps them hidden; a plain NativeActivity keeps its navigation bar.
    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON, 0);

    SUPERSONIC_LOG_INFO("Android") << "android_main: waiting for a window.";

    // The game is entered with a window to draw on: the first thing the
    // renderer does is make a surface from it.
    while (!app->destroyRequested && !(g.resumed && g.window)) pollOnce(-1);

    int status = EXIT_SUCCESS;
    if (!app->destroyRequested) {
        char name[] = "supersonic";
        char* argv[] = {name, nullptr};
        status = SupersonicMain(1, argv);
        SUPERSONIC_LOG_INFO("Android") << "The game returned " << status << "; finishing the activity.";
    }

    // Finish the activity (nothing, when the platform is already destroying
    // it) and keep reading its commands until the glue says it is gone.
    ANativeActivity_finish(app->activity);
    while (!app->destroyRequested) pollOnce(-1);

    // A new activity would call android_main again in this same process, over
    // whatever the engine's statics were left holding. Ending the process is
    // the clean start the next launch needs. _exit, not exit: static
    // destructors running while the UI thread is still inside onDestroy are a
    // crash with nobody to report it to.
    _exit(status);
}
