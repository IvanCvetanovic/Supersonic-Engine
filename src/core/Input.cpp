#include "core/Input.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

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

// Edges that have happened but no tick has consumed yet, and the set the tick
// currently running is allowed to see. Two sets rather than one because a frame
// may run several ticks, and only the first of them may see a given press.
std::unordered_set<std::string> g_pendingPress;
std::unordered_set<std::string> g_pendingRelease;
std::unordered_set<std::string> g_tickPress;
std::unordered_set<std::string> g_tickRelease;

// A recorded tick, standing in for the devices while it runs.
//
// Separate storage rather than writing over g_actionCurrent and g_tickPress,
// which would have been fewer lines and one bug: those are also what the
// editor, the UI and the camera read once a frame, and overwriting them would
// mean a replay stole the mouse from the person watching it. Everything here
// is consulted only while g_replayingTick is raised, which is only inside a
// tick.
bool g_replayingTick = false;
std::unordered_set<std::string> g_replayDown;
std::unordered_set<std::string> g_replayPress;
std::unordered_set<std::string> g_replayRelease;
std::unordered_map<std::string, float> g_replayAxes;
glm::vec2 g_replayMouseDelta{0.0f};

glm::vec2 g_mouseDelta{0.0f};

// This frame's contacts, with the phase and delta a snapshot cannot carry.
//
// Rebuilt every Update rather than stored on the snapshot, because one of these
// - the one that just lifted - is not in the current snapshot at all.
std::vector<Contact> g_contacts;

// Where a contact was in a snapshot, or nothing.
const RawContact* findRaw(const RawInputState& state, int id) {
    const int count = std::min(state.contactCount, Touch::kMaxContacts);
    for (int i = 0; i < count; ++i) {
        if (state.contacts[i].id == id) return &state.contacts[i];
    }
    return nullptr;
}

// Compares the two snapshots and works out what each finger is doing.
//
// Everything still down first, in the order the device reported it, then
// everything that lifted. An id of -1 is skipped: it is what an unfilled slot
// holds, and a device that reports a count larger than the array it filled
// would otherwise produce a phantom finger at the origin.
void rebuildContacts() {
    g_contacts.clear();

    const int count = std::min(g_current.contactCount, Touch::kMaxContacts);
    for (int i = 0; i < count; ++i) {
        const RawContact& now = g_current.contacts[i];
        if (now.id < 0) continue;

        Contact contact;
        contact.id = now.id;
        contact.position = now.position;

        if (const RawContact* before = findRaw(g_previous, now.id)) {
            contact.phase = ContactPhase::Moved;
            contact.delta = now.position - before->position;
        } else {
            // Nothing to subtract from. A gesture that measures movement from
            // the first frame of a touch measures it from here, and a delta
            // against a finger that did not exist last frame is the distance
            // from wherever the previous one happened to lift.
            contact.phase = ContactPhase::Began;
            contact.delta = glm::vec2(0.0f);
        }
        g_contacts.push_back(contact);
    }

    // And the ones that are gone. Reported once, at the last position they were
    // seen at: a gesture that ends on release has to see this frame or it never
    // ends at all.
    const int before = std::min(g_previous.contactCount, Touch::kMaxContacts);
    for (int i = 0; i < before; ++i) {
        const RawContact& gone = g_previous.contacts[i];
        if (gone.id < 0) continue;
        if (findRaw(g_current, gone.id)) continue;

        Contact contact;
        contact.id = gone.id;
        contact.position = gone.position;
        contact.delta = glm::vec2(0.0f);
        contact.phase = ContactPhase::Ended;
        g_contacts.push_back(contact);
    }
}

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
    g_pendingPress.clear();
    g_pendingRelease.clear();
    g_tickPress.clear();
    g_tickRelease.clear();
    g_hasPrevious = false;
    g_current = RawInputState{};
    g_previous = RawInputState{};
    g_mouseDelta = glm::vec2(0.0f);
    g_contacts.clear();

    // A replay in progress is abandoned with everything else. Rebinding while
    // one runs is not a thing anybody should do, but leaving the flag raised
    // over a cleared binding table would make every action read from a
    // recording that no longer describes the actions that exist.
    EndReplayedTick();

    // The pointer and the keyboard are NOT reset here, and the first version of
    // this did reset them.
    //
    // It looked harmless because LoadDefaultBindings is the only caller and it
    // runs once at startup. It stops being harmless the day a settings screen
    // rebinds the controls: reloading the defaults mid-game would silently
    // release a locked pointer and hand the keyboard back, in the middle of a
    // first-person game, from a function whose name says it clears bindings.
    //
    // Who owns the mouse is not a binding. A test that needs it back where it
    // started says so with the ordinary setters, which are public precisely
    // because a game sets them too.
}

void Input::SynthesiseMouseContact(RawInputState& state) {
    if (state.mouseButtons[MouseButton::Left] && !CursorCaptureSuppressed()) {
        state.contacts[0].id = 0;
        state.contacts[0].position = state.mousePosition;
        state.contactCount = 1;
    } else {
        state.contactCount = 0;
    }
}

int Input::ContactCount() { return static_cast<int>(g_contacts.size()); }

Contact Input::GetContact(int index) {
    if (index < 0 || index >= static_cast<int>(g_contacts.size())) return Contact{};
    return g_contacts[static_cast<size_t>(index)];
}

bool Input::TryGetContact(int id, Contact& out) {
    for (const Contact& contact : g_contacts) {
        if (contact.id != id) continue;
        out = contact;
        return true;
    }
    return false;
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

    rebuildContacts();

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

    // Latch this frame's edges for whichever tick asks next. Accumulated rather
    // than assigned: several frames can pass between two ticks, and a press in
    // any of them still happened.
    for (const auto& [name, down] : g_actionCurrent) {
        const auto previous = g_actionPrevious.find(name);
        const bool wasDown = previous != g_actionPrevious.end() && previous->second;
        if (down && !wasDown) g_pendingPress.insert(name);
        if (!down && wasDown) g_pendingRelease.insert(name);
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
    if (g_replayingTick) {
        return g_replayDown.find(action) != g_replayDown.end();
    }
    const auto it = g_actionCurrent.find(action);
    return it != g_actionCurrent.end() && it->second;
}

void Input::BeginTickInput() {
    // Hand the pending edges to this tick and take them off the queue. The
    // next tick in the same frame therefore sees an empty set, which is what
    // stops one keystroke firing three times when the frame is running late.
    g_tickPress = std::move(g_pendingPress);
    g_tickRelease = std::move(g_pendingRelease);
    g_pendingPress.clear();
    g_pendingRelease.clear();
}

void Input::DiscardPendingTickInput() {
    // The pending sets only, NOT g_tickPress and g_tickRelease. Those belong to
    // a tick that has already begun and may still be reading them; the pending
    // ones are edges waiting for a tick that is not coming.
    g_pendingPress.clear();
    g_pendingRelease.clear();
}

bool Input::TickWasPressed(const std::string& action) {
    if (g_replayingTick) {
        return g_replayPress.find(action) != g_replayPress.end();
    }
    return g_tickPress.find(action) != g_tickPress.end();
}

bool Input::TickWasReleased(const std::string& action) {
    if (g_replayingTick) {
        return g_replayRelease.find(action) != g_replayRelease.end();
    }
    return g_tickRelease.find(action) != g_tickRelease.end();
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
    if (g_replayingTick) {
        // An axis the recording does not name reads zero, exactly as an unbound
        // one does live. A recording carries every bound axis including the
        // ones sitting at rest, so a name missing from it is a name that did
        // not exist when the file was written.
        const auto replayed = g_replayAxes.find(axis);
        return replayed != g_replayAxes.end() ? replayed->second : 0.0f;
    }

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

// The one query whose replayed answer differs from its live one for a reason
// that is not about recording at all: a delta is a per-FRAME difference, and a
// tick is not a frame. See TickInput's comment - this is the value the tick was
// handed, not one recomputed from positions that no longer exist.
glm::vec2 Input::MouseDelta() { return g_replayingTick ? g_replayMouseDelta : g_mouseDelta; }
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

// --- One tick's input, as a value -----------------------------------------

bool Input::TickInput::operator==(const TickInput& other) const {
    // Member-wise, and the vectors compare in ORDER. Both sides are built by
    // CaptureTickInput or read back from a file that CaptureTickInput's output
    // produced, and both walk g_actionNames, so the order is the binding order
    // in each case. This is an equality for "is this the same tick as the last
    // one", which is what the delta encoding asks; it is not a set comparison
    // and would be wrong as one.
    return down == other.down && pressed == other.pressed && released == other.released &&
           axes == other.axes && mouseDelta == other.mouseDelta;
}

Input::TickInput Input::CaptureTickInput() {
    TickInput captured;

    // Every bound action, not only the ones doing something. An action that is
    // absent and an action that is present-and-false have to mean the same
    // thing on the way back in, and the cheapest way to guarantee that is for
    // the writer never to produce the ambiguous case: `down` lists what is
    // held, and anything bound and not listed is up.
    for (const std::string& action : g_actionNames) {
        if (IsDown(action)) captured.down.push_back(action);
        if (TickWasPressed(action)) captured.pressed.push_back(action);
        if (TickWasReleased(action)) captured.released.push_back(action);
    }

    // Axes carry a value rather than a flag, so every one of them is written
    // whatever it reads. A missing axis is zero on the way back in - see
    // GetAxis - which makes an axis at rest and an axis nobody bound the same
    // number, and they are.
    captured.axes.reserve(g_axisNames.size());
    for (const std::string& axis : g_axisNames) {
        captured.axes.emplace_back(axis, GetAxis(axis));
    }

    captured.mouseDelta = MouseDelta();
    return captured;
}

void Input::BeginReplayedTick(const TickInput& input) {
    g_replayDown.clear();
    g_replayPress.clear();
    g_replayRelease.clear();
    g_replayAxes.clear();

    g_replayDown.insert(input.down.begin(), input.down.end());
    g_replayPress.insert(input.pressed.begin(), input.pressed.end());
    g_replayRelease.insert(input.released.begin(), input.released.end());
    for (const auto& [name, value] : input.axes) g_replayAxes[name] = value;
    g_replayMouseDelta = input.mouseDelta;

    // Raised last, so a query that somehow ran during the copy above would read
    // the live devices rather than a half-filled recording.
    g_replayingTick = true;
}

void Input::EndReplayedTick() {
    // Lowered first, for the mirror of the reason it is raised last.
    g_replayingTick = false;

    // Cleared rather than left standing. A tick that ends and leaves its input
    // in place costs nothing while the flag is down, and costs an entire
    // afternoon on the day something reads it with the flag accidentally up.
    g_replayDown.clear();
    g_replayPress.clear();
    g_replayRelease.clear();
    g_replayAxes.clear();
    g_replayMouseDelta = glm::vec2(0.0f);
}

bool Input::ReplayingTick() { return g_replayingTick; }

const std::vector<std::string>& Input::ActionNames() { return g_actionNames; }
const std::vector<std::string>& Input::AxisNames() { return g_axisNames; }

} // namespace Supersonic
