#include "core/Input.hpp"

#include <algorithm>
#include <cmath>

namespace Supersonic {

namespace {

struct ActionBindingStorage {
    std::vector<int> keys;
    std::vector<int> mouseButtons;
    std::vector<int> padButtons;
};

struct AxisBindingStorage {
    int positiveKey{-1};
    int negativeKey{-1};
    int padAxis{-1};
    float padScale{1.0f};
};

std::unordered_map<std::string, ActionBindingStorage> g_actions;
std::unordered_map<std::string, AxisBindingStorage> g_axes;

std::vector<std::string> g_actionNames;
std::vector<std::string> g_axisNames;

RawInputState g_current;
RawInputState g_previous;
bool g_hasPrevious = false;

std::unordered_map<std::string, bool> g_actionCurrent;
std::unordered_map<std::string, bool> g_actionPrevious;

glm::vec2 g_mouseDelta{0.0f};

CursorMode g_requestedCursor = CursorMode::Normal;
bool g_cursorSuppressed = false;
bool g_windowFocused = true;

// What the last Update ran under. Locking or releasing the pointer moves it -
// GLFW switches between screen coordinates and unbounded virtual ones, and puts
// it back where it was on the way out - so the first frame under a new mode has
// a previous position from the old coordinate space.
CursorMode g_cursorAtLastUpdate = CursorMode::Normal;

bool g_textCaptureActive = false;


bool keyInRange(int key) { return key >= 0 && key <= Key::Last; }

// Rescales the live region so a control leaving the deadzone starts at zero
// rather than jumping to the deadzone value. Without this a stick feels like it
// snaps into motion.
float applyDeadzone(float value) {
    const float magnitude = std::fabs(value);
    if (magnitude <= Input::kStickDeadzone) return 0.0f;
    const float scaled = (magnitude - Input::kStickDeadzone) / (1.0f - Input::kStickDeadzone);
    return value < 0.0f ? -scaled : scaled;
}

bool evaluateBinding(const ActionBindingStorage& binding, const RawInputState& state) {
    // Any source satisfies the action, which is what lets a keyboard and a
    // gamepad drive the same game without either knowing about the other.
    //
    // Unless something is being typed into: then the keys belong to whatever is
    // accepting the name, and the same letters would otherwise walk the player
    // across the level while they spell it. The pad and the mouse below are not
    // gated, because nobody types with a thumbstick.
    if (!g_textCaptureActive) {
        for (const int key : binding.keys) {
            if (keyInRange(key) && state.keys[key]) return true;
        }
    }
    for (const int button : binding.mouseButtons) {
        if (button >= 0 && button < MouseButton::Count && state.mouseButtons[button]) return true;
    }
    if (state.padConnected) {
        for (const int button : binding.padButtons) {
            if (button >= 0 && button < Pad::ButtonCount && state.padButtons[button]) return true;
        }
    }
    return false;
}

} // namespace

void Input::ClearBindings() {
    g_actions.clear();
    g_axes.clear();
    g_actionNames.clear();
    g_axisNames.clear();
    g_actionCurrent.clear();
    g_actionPrevious.clear();
    g_hasPrevious = false;
    g_current = RawInputState{};
    g_previous = RawInputState{};
    g_mouseDelta = glm::vec2(0.0f);

    // The pointer too, even though it is not a binding. This is the only reset
    // there is - LoadDefaultBindings goes through it, and so does every test -
    // and a cursor mode left locked by whatever ran last is exactly the kind of
    // state that makes a suite pass in one order and fail in another.
    g_requestedCursor = CursorMode::Normal;
    g_cursorSuppressed = false;
    g_windowFocused = true;
    g_cursorAtLastUpdate = CursorMode::Normal;
    g_textCaptureActive = false;
}

const unsigned int* Input::TypedCharacters() { return g_current.textCharacters; }
int Input::TypedCharacterCount() { return g_current.textCharacterCount; }

void Input::SetTextCaptureActive(bool active) { g_textCaptureActive = active; }
bool Input::TextCaptureActive() { return g_textCaptureActive; }

void Input::SetCursorMode(CursorMode mode) { g_requestedCursor = mode; }
CursorMode Input::RequestedCursorMode() { return g_requestedCursor; }

void Input::SuppressCursorCapture(bool suppressed) { g_cursorSuppressed = suppressed; }
bool Input::CursorCaptureSuppressed() { return g_cursorSuppressed; }

void Input::SetWindowFocused(bool focused) { g_windowFocused = focused; }
bool Input::WindowFocused() { return g_windowFocused; }

CursorMode Input::EffectiveCursorMode() {
    // Either veto wins, and neither forgets the request: un-suppressing or
    // coming back to the window restores exactly what the game last asked for.
    if (g_cursorSuppressed || !g_windowFocused) return CursorMode::Normal;
    return g_requestedCursor;
}

void Input::BindActionKey(const std::string& action, int key) {
    if (!keyInRange(key)) return;
    auto [it, inserted] = g_actions.try_emplace(action);
    if (inserted) g_actionNames.push_back(action);
    it->second.keys.push_back(key);
}

void Input::BindActionMouseButton(const std::string& action, int button) {
    if (button < 0 || button >= MouseButton::Count) return;
    auto [it, inserted] = g_actions.try_emplace(action);
    if (inserted) g_actionNames.push_back(action);
    it->second.mouseButtons.push_back(button);
}

void Input::BindActionPadButton(const std::string& action, int button) {
    if (button < 0 || button >= Pad::ButtonCount) return;
    auto [it, inserted] = g_actions.try_emplace(action);
    if (inserted) g_actionNames.push_back(action);
    it->second.padButtons.push_back(button);
}

void Input::BindAxisKeys(const std::string& axis, int positiveKey, int negativeKey) {
    auto [it, inserted] = g_axes.try_emplace(axis);
    if (inserted) g_axisNames.push_back(axis);
    it->second.positiveKey = positiveKey;
    it->second.negativeKey = negativeKey;
}

void Input::BindAxisPad(const std::string& axis, int padAxis, float scale) {
    if (padAxis < 0 || padAxis >= Pad::AxisCount) return;
    auto [it, inserted] = g_axes.try_emplace(axis);
    if (inserted) g_axisNames.push_back(axis);
    it->second.padAxis = padAxis;
    it->second.padScale = scale;
}

void Input::LoadDefaultBindings() {
    ClearBindings();

    // Movement: WASD and arrows, plus the left stick. Y is inverted on the pad
    // because GLFW reports stick up as negative.
    BindAxisKeys("MoveX", Key::D, Key::A);
    BindAxisKeys("MoveY", Key::W, Key::S);
    BindAxisPad("MoveX", Pad::LeftX, 1.0f);
    BindAxisPad("MoveY", Pad::LeftY, -1.0f);

    BindAxisKeys("LookX", Key::Right, Key::Left);
    BindAxisKeys("LookY", Key::Up, Key::Down);
    BindAxisPad("LookX", Pad::RightX, 1.0f);
    BindAxisPad("LookY", Pad::RightY, -1.0f);

    BindActionKey("Jump", Key::Space);
    BindActionPadButton("Jump", Pad::A);

    BindActionMouseButton("Fire", MouseButton::Left);
    BindActionPadButton("Fire", Pad::RightBumper);

    BindActionMouseButton("AltFire", MouseButton::Right);
    BindActionPadButton("AltFire", Pad::LeftBumper);

    BindActionKey("Sprint", Key::LeftShift);
    BindActionPadButton("Sprint", Pad::LeftThumb);

    BindActionKey("Interact", Key::E);
    BindActionPadButton("Interact", Pad::X);

    BindActionKey("Crouch", Key::LeftControl);
    BindActionPadButton("Crouch", Pad::B);

    BindActionKey("Pause", Key::Escape);
    BindActionPadButton("Pause", Pad::Start);
}

void Input::Update(const RawInputState& state) {
    g_previous = g_current;
    g_current = state;

    // The first frame has no previous state, so treat it as identical: without
    // this every key already held when the window opens reports as pressed on
    // frame one.
    if (!g_hasPrevious) {
        g_previous = state;
        g_hasPrevious = true;
    }

    // Locking or releasing the pointer teleports it, and a teleport differenced
    // against last frame is a delta of several hundred pixels: the frame a
    // mouse-look game captures the mouse, the camera snaps to face somewhere
    // else entirely. Corrected by moving the BASELINE rather than by zeroing
    // the result, so the delta below stays one subtraction with no second path
    // that could disagree with it.
    //
    // Deliberately not `g_hasPrevious = false`, which would do this and also
    // swallow every key edge for a frame - and the frame a game locks the
    // pointer is usually the frame someone pressed something to make it happen.
    if (const CursorMode effective = EffectiveCursorMode(); effective != g_cursorAtLastUpdate) {
        g_previous.mousePosition = g_current.mousePosition;
        g_cursorAtLastUpdate = effective;
    }

    g_mouseDelta = g_current.mousePosition - g_previous.mousePosition;

    g_actionPrevious = g_actionCurrent;
    for (const auto& [name, binding] : g_actions) {
        const bool down = evaluateBinding(binding, g_current);
        g_actionCurrent[name] = down;

        // An action observed for the very first time - the first frame of the
        // program, or the frame after it was bound - has no previous value to
        // compare against. Seeding it with the current one means an edge always
        // requires a transition that was actually observed, rather than every
        // key already held at startup firing its action on frame one.
        g_actionPrevious.try_emplace(name, down);
    }

    // Nothing special happens when the keyboard changes hands, and that is the
    // decision rather than the omission.
    //
    // The first draft suppressed the edges either side of it, reasoning that a
    // release nobody performed is not a release. But the LEVEL changes - every
    // key-driven action reads false the moment the veto goes up - and an edge
    // that does not fire when the level changes is worse than one that fires
    // without a finger behind it: a game tracking movement by edges would still
    // believe the player was walking, and the run animation would stay stuck
    // mid-stride for as long as the name took to type. The edge and the level
    // agreeing is the property worth keeping.
}

bool Input::IsDown(const std::string& action) {
    const auto it = g_actionCurrent.find(action);
    return it != g_actionCurrent.end() && it->second;
}

bool Input::WasPressed(const std::string& action) {
    const auto current = g_actionCurrent.find(action);
    if (current == g_actionCurrent.end() || !current->second) return false;
    const auto previous = g_actionPrevious.find(action);
    return previous == g_actionPrevious.end() || !previous->second;
}

bool Input::WasReleased(const std::string& action) {
    const auto current = g_actionCurrent.find(action);
    if (current != g_actionCurrent.end() && current->second) return false;
    const auto previous = g_actionPrevious.find(action);
    return previous != g_actionPrevious.end() && previous->second;
}

float Input::GetAxis(const std::string& axis) {
    const auto it = g_axes.find(axis);
    if (it == g_axes.end()) return 0.0f;
    const AxisBindingStorage& binding = it->second;

    // Gated for the same reason the action bindings are: while a name is being
    // typed, A and D are letters.
    float keyboard = 0.0f;
    if (!g_textCaptureActive) {
        if (keyInRange(binding.positiveKey) && g_current.keys[binding.positiveKey]) keyboard += 1.0f;
        if (keyInRange(binding.negativeKey) && g_current.keys[binding.negativeKey]) keyboard -= 1.0f;
    }

    float pad = 0.0f;
    if (g_current.padConnected && binding.padAxis >= 0 && binding.padAxis < Pad::AxisCount) {
        pad = applyDeadzone(g_current.padAxes[binding.padAxis]) * binding.padScale;
    }

    // Whichever is being pushed harder wins, rather than summing: holding a key
    // while a stick is half deflected should not produce 1.5.
    const float result = std::fabs(pad) > std::fabs(keyboard) ? pad : keyboard;
    return std::clamp(result, -1.0f, 1.0f);
}

glm::vec2 Input::MousePosition() { return g_current.mousePosition; }
glm::vec2 Input::MouseDelta() { return g_mouseDelta; }
float Input::Scroll() { return g_current.scroll; }
bool Input::IsGamepadConnected() { return g_current.padConnected; }

bool Input::IsKeyDown(int key) {
    return keyInRange(key) && g_current.keys[key];
}

bool Input::WasKeyPressed(int key) {
    if (!keyInRange(key)) return false;
    return g_current.keys[key] && !g_previous.keys[key];
}

bool Input::IsMouseButtonDown(int button) {
    return button >= 0 && button < MouseButton::Count && g_current.mouseButtons[button];
}

const std::vector<std::string>& Input::ActionNames() { return g_actionNames; }
const std::vector<std::string>& Input::AxisNames() { return g_axisNames; }

} // namespace Supersonic
