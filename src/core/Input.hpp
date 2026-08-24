#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace Supersonic {

// Named input actions and axes.
//
// Before this the engine had no way for gameplay to read input at all: exactly
// two files polled GLFW directly, both of them cameras, and a script could not
// ask whether a key was down without reaching past the engine into the windowing
// library. Anything a game needs - "jump", "fire", "move forward" - had nowhere
// to live.
//
// Deliberately free of GLFW. Key codes are plain ints whose values mirror
// GLFW's, so the polling layer can pass them straight through, but nothing here
// includes a windowing header. That keeps the mapping and edge detection - the
// part with the actual logic in it - testable without a window.

namespace Key {
// Values mirror GLFW's, so the polling layer passes them through unchanged.
constexpr int Space = 32;
constexpr int Apostrophe = 39;
constexpr int Comma = 44;
constexpr int Minus = 45;
constexpr int Period = 46;
constexpr int Slash = 47;
constexpr int Num0 = 48, Num1 = 49, Num2 = 50, Num3 = 51, Num4 = 52;
constexpr int Num5 = 53, Num6 = 54, Num7 = 55, Num8 = 56, Num9 = 57;
constexpr int A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72;
constexpr int I = 73, J = 74, K = 75, L = 76, M = 77, N = 78, O = 79, P = 80;
constexpr int Q = 81, R = 82, S = 83, T = 84, U = 85, V = 86, W = 87, X = 88;
constexpr int Y = 89, Z = 90;
constexpr int Escape = 256;
constexpr int Enter = 257;
constexpr int Tab = 258;
constexpr int Backspace = 259;
constexpr int Right = 262, Left = 263, Down = 264, Up = 265;
constexpr int LeftShift = 340, LeftControl = 341, LeftAlt = 342;
constexpr int RightShift = 344, RightControl = 345, RightAlt = 346;
constexpr int Last = 348;
} // namespace Key

namespace MouseButton {
constexpr int Left = 0, Right = 1, Middle = 2;
constexpr int Count = 8;
} // namespace MouseButton

namespace Pad {
// Gamepad buttons, mirroring GLFW's standard mapping.
constexpr int A = 0, B = 1, X = 2, Y = 3;
constexpr int LeftBumper = 4, RightBumper = 5;
constexpr int Back = 6, Start = 7, Guide = 8;
constexpr int LeftThumb = 9, RightThumb = 10;
constexpr int DpadUp = 11, DpadRight = 12, DpadDown = 13, DpadLeft = 14;
constexpr int ButtonCount = 15;

// Axes. Triggers rest at -1 and travel to +1.
constexpr int LeftX = 0, LeftY = 1, RightX = 2, RightY = 3;
constexpr int LeftTrigger = 4, RightTrigger = 5;
constexpr int AxisCount = 6;
} // namespace Pad

namespace Text {
// How many characters one frame can carry.
//
// A cap, not a queue. Typing produces a handful per frame and a frame that
// takes two seconds - a level load - drops what will not fit, which is the
// right trade for a name box and the wrong one the day a paste path is added.
constexpr int kMaxCharacters = 32;
} // namespace Text

// One frame of raw device state, filled by the polling layer.
//
// A plain snapshot rather than a set of callbacks: edge detection needs the
// previous frame to compare against, and a snapshot per frame is the simplest
// thing that gives it. It is also what makes this testable - a test hands over
// a struct instead of driving a window.
struct RawInputState {
    bool keys[Key::Last + 1]{};
    bool mouseButtons[MouseButton::Count]{};
    glm::vec2 mousePosition{0.0f};
    float scroll{0.0f};

    bool padConnected{false};
    bool padButtons[Pad::ButtonCount]{};
    float padAxes[Pad::AxisCount]{};

    // What was TYPED this frame, as Unicode codepoints in the order they
    // arrived. Almost always empty.
    //
    // The one part of this snapshot that is not polled, because text cannot be:
    // shift, dead keys, Caps Lock and the keyboard layout all sit between a key
    // going down and a character existing, and none of that is recoverable from
    // an array of booleans. GLFW delivers characters through a callback, so the
    // polling layer accumulates them and drains them into here - after which
    // they are an ordinary field a test fills in by hand.
    unsigned int textCharacters[Text::kMaxCharacters]{};
    int textCharacterCount{0};
};

// What the pointer is doing, in the terms a game means them.
//
// Deliberately NOT GLFW's names, which are a trap in both directions: GLFW's
// `CURSOR_DISABLED` is what everyone else calls locked, and its
// `CURSOR_CAPTURED` only confines a still-VISIBLE pointer to the window. Mapping
// one onto the other lives in InputPolling, which is the only file allowed to
// know how GLFW spells anything.
enum class CursorMode {
    // Visible, and free to leave the window. What an editor wants.
    Normal,

    // Invisible over the window and otherwise unchanged - still bounded by the
    // screen, still meaningful as a position. For a game drawing its own
    // pointer.
    Hidden,

    // Invisible and held. The pointer stops having a position anyone should
    // read: it cannot reach the screen edge, so `MouseDelta` keeps working
    // through a turn that would otherwise have run out of desk. This is the one
    // a mouse-look game wants, and the reason this enum exists.
    Locked,
};

class Input {
public:
    // Installs a default set of bindings: WASD/arrows and the left stick for
    // movement, the arrow keys and the right stick for look, Space/A to jump,
    // and so on.
    //
    // The mouse is NOT bound to the look axes, and that is not an omission. An
    // axis is bipolar and clamped to -1..1, which is what a stick reports and
    // exactly what a mouse does not: a mouse reports how far it moved, the
    // magnitude IS the movement, and there is no maximum to clamp to. Mouse
    // look reads MouseDelta below - or the script ABI's mouseDelta - and this
    // comment used to claim otherwise, which is the wrong thing to believe
    // while writing a first-person controller.
    static void LoadDefaultBindings();

    // Binds an additional source to an action. Any bound source being down
    // satisfies the action, so keyboard and gamepad coexist without either
    // knowing about the other.
    static void BindActionKey(const std::string& action, int key);
    static void BindActionMouseButton(const std::string& action, int button);
    static void BindActionPadButton(const std::string& action, int button);

    // An axis reads from a positive/negative key pair, a gamepad axis, or both.
    static void BindAxisKeys(const std::string& axis, int positiveKey, int negativeKey);
    static void BindAxisPad(const std::string& axis, int padAxis, float scale = 1.0f);

    static void ClearBindings();

    // Advances one frame: the current state becomes the previous state and the
    // snapshot becomes current. Must be called exactly once per frame, before
    // anything queries, or edge detection reports the same press twice.
    static void Update(const RawInputState& state);

    static bool IsDown(const std::string& action);
    static bool WasPressed(const std::string& action);
    static bool WasReleased(const std::string& action);

    // -1..1 for a key pair, deadzoned for a stick. Returns 0 for an unbound
    // name rather than failing, so a typo is a dead control rather than a crash.
    static float GetAxis(const std::string& axis);

    static glm::vec2 MousePosition();
    static glm::vec2 MouseDelta();
    static float Scroll();
    static bool IsGamepadConnected();

    // ---- The pointer ------------------------------------------------------
    //
    // A REQUEST, not an action: this file cannot call GLFW and would not know
    // whether a window exists. `InputPolling` reads the effective mode once a
    // frame and applies it. Kept here anyway, rather than on the window,
    // because a script has to be able to ask - and because the arbitration
    // below is the part with a decision in it, which makes it the part worth
    // testing without a device.

    static void SetCursorMode(CursorMode mode);
    static CursorMode RequestedCursorMode();

    // The host's veto, for when something other than the game owns the mouse:
    // the editor between plays, a viewport that is not focused, a window that
    // is not focused at all. Suppressed means Normal no matter what the game
    // asked for, and un-suppressing restores the request rather than losing it,
    // so alt-tabbing away and back does not leave a game unable to look.
    //
    // A veto rather than the host calling SetCursorMode(Normal) itself: that
    // would overwrite what the game asked for, and there would be nothing left
    // to restore.
    static void SuppressCursorCapture(bool suppressed);
    static bool CursorCaptureSuppressed();

    // Whether the window has focus, reported by the polling layer each frame.
    //
    // The second veto, and the one that is a safety mechanism rather than a
    // policy: a locked pointer is invisible and cannot leave the window, so a
    // game that locks it and offers no way out would trap whoever ran it.
    // Alt-tab has to work. Defaults to true so a headless or test caller that
    // never reports focus is not silently unable to capture.
    static void SetWindowFocused(bool focused);
    static bool WindowFocused();

    // What should actually be applied to the window this frame: the request,
    // unless either veto is in force.
    static CursorMode EffectiveCursorMode();

    // ---- Typing -----------------------------------------------------------

    // The characters typed this frame, valid until the next Update. Empty on
    // almost every frame.
    static const unsigned int* TypedCharacters();
    static int TypedCharacterCount();

    // Something is accepting a typed name, so the keyboard is not the game's.
    //
    // The mirror of the cursor veto, and needed for the same reason at the
    // other end: ImGui's own io.WantTextInput only knows about ImGui's widgets,
    // so a field drawn by the engine's UI canvas is invisible to it and typing
    // "Walter" would also walk the player forward and trip every editor
    // shortcut on the way.
    //
    // While raised, KEY-derived sources contribute nothing to actions and axes.
    // Pad and mouse sources are untouched - nobody types with a thumbstick -
    // and the raw IsKeyDown/WasKeyPressed queries stay truthful, because the
    // field itself has to read Backspace through something.
    static void SetTextCaptureActive(bool active);
    static bool TextCaptureActive();

    // Raw access, for the few places that legitimately want a specific key
    // rather than an action.
    static bool IsKeyDown(int key);
    static bool WasKeyPressed(int key);
    static bool IsMouseButtonDown(int button);

    // Sticks rest slightly off centre and drift with age; without a deadzone a
    // resting controller walks the player across the level.
    static constexpr float kStickDeadzone = 0.18f;

    static const std::vector<std::string>& ActionNames();
    static const std::vector<std::string>& AxisNames();

    // The binding tables themselves live entirely in the .cpp. Nothing outside
    // needs their layout, and keeping them out means adding a new source type
    // does not recompile everything that reads input.
};

} // namespace Supersonic
