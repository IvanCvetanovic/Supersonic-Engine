#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
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

namespace Touch {
// How many contacts one frame can carry.
//
// Eight, which is more than any gesture worth writing and fewer than a screen
// can physically report. Android's NativeActivity will hand over as many
// pointers as the panel tracks; a machine that reports a tenth finger loses it
// here rather than growing the snapshot for a case nobody has.
//
// The number that actually matters is two. A one-finger drag and a two-finger
// drag are different gestures on every touch device ever made, and telling
// them apart is the whole reason a contact COUNT exists rather than a single
// position - which is what this engine had.
constexpr int kMaxContacts = 8;
} // namespace Touch

// One finger, as the device reports it.
//
// No phase here, deliberately. This is a snapshot of what is touching the
// screen right now, exactly as `keys` is a snapshot of what is held right now -
// and "began" and "ended" are not things a snapshot contains, they are things a
// comparison between two snapshots produces. Input::Update does that comparison,
// which keeps the part with the logic in it testable without a device.
struct RawContact {
    // Stable for as long as the finger stays down, and reused freely after it
    // lifts. Godot spells it `index` on InputEventScreenTouch and so does
    // Android; a gesture machine keyed on it is keyed on the same thing there.
    //
    // -1 means the slot is empty, which only matters for the ones past
    // `contactCount`.
    int id{-1};
    glm::vec2 position{0.0f};
};

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

    // What is touching the screen this frame, in no particular order.
    //
    // A count of zero is the desktop case and must stay indistinguishable from
    // this field not existing: nothing above changes meaning, and a caller that
    // never fills these in gets exactly the engine it had.
    //
    // Unlike `textCharacters`, which the polling layer DRAINS into each frame,
    // these are compared against the previous frame's - so this snapshot is
    // kept, not consumed. They are closer to `keys` than to typing.
    RawContact contacts[Touch::kMaxContacts]{};
    int contactCount{0};
};

// What a contact is doing, which only a pair of frames can say.
enum class ContactPhase {
    // Down this frame and not last. Its delta is zero - there is nothing to
    // subtract from - and a gesture machine that measures movement from the
    // first frame of a touch measures it from here.
    Began,

    // Still down. Includes not having moved, because "stationary" is a
    // threshold question and the threshold belongs to whoever is asking: a 12px
    // hold test and a 2px one disagree, and neither belongs in the engine.
    Moved,

    // Lifted. Reported for exactly one frame, at the last position it was seen
    // at, and then gone. A gesture that ends on release has to see this frame
    // or it never ends at all.
    Ended,
};

// One contact, with the part a snapshot cannot carry filled in.
struct Contact {
    int id{-1};
    glm::vec2 position{0.0f};

    // Since the previous frame. Zero on Began and on Ended: a finger that has
    // just landed has not moved, and one that has lifted did not move to get
    // there.
    glm::vec2 delta{0.0f};

    ContactPhase phase{ContactPhase::Began};
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

    // --- Edges for the FIXED TICK, which is not the frame -----------------
    //
    // WasPressed below answers "did this action go down since the last frame",
    // which is the right question for anything that runs once per frame - the
    // camera, the editor, the UI. It is the wrong question for anything running
    // on the simulation tick, and wrong in two directions at once.
    //
    // A tick rate below the frame rate means some frames run NO tick, and the
    // press that happened during one of them is gone before anything on the
    // tick could see it - a dropped jump, on a fast machine, occasionally.
    // A frame that runs SEVERAL ticks reports the same press to every one of
    // them, so one keystroke fires three shots.
    //
    // So a press is LATCHED when it happens and handed to the first tick that
    // asks. Held until a tick consumes it, seen by exactly one tick, and no
    // tick ever sees a press that did not happen.
    static void BeginTickInput();
    static bool TickWasPressed(const std::string& action);
    static bool TickWasReleased(const std::string& action);

    // Throw away edges nothing is going to consume.
    //
    // The latch above holds a press until a tick asks for it, and the only
    // thing that asks is the tick loop - which runs only while the scene is
    // simulating. Everything else about input keeps running: the devices are
    // polled every frame, in the editor, while paused, on the menu. So the
    // presses made in all that time did not go anywhere. They QUEUED, and the
    // first tick after Play was handed the lot.
    //
    // The symptom is a player pressing keys in the editor, hitting Play, and
    // the character jumping and firing on frame one from input given a minute
    // earlier - and, less visibly, a recorded run and a replay that entered
    // Play by different routes starting from different input on tick zero.
    //
    // Called every frame the simulation is not running, which is the honest
    // shape of it: a latch is only correct if something is definitely coming to
    // empty it, and while nothing is ticking nothing is.
    static void DiscardPendingTickInput();

    // --- One tick's input, as a value ---------------------------------------
    //
    // Everything above answers a question about global state that only exists
    // while the devices are being polled. A recording needs the same answers as
    // something it can keep, write to a file, and hand back to a tick that runs
    // a week later on a different machine.
    //
    // RESOLVED, not raw. The obvious alternative is to record RawInputState -
    // the keys, the buttons, the stick positions - and let replay derive the
    // rest, and it cannot work, for a reason that is easy to miss: some of what
    // a tick reads is derived per FRAME rather than per tick. MouseDelta is the
    // clear case. A frame that runs three ticks hands the same delta to all
    // three; a frame that runs none hands it to nobody. Re-deriving that on a
    // machine whose frames fall differently produces different numbers for the
    // same recording, which is a divergence nobody caused.
    //
    // So this is what the tick was actually handed, frozen. It is also why a
    // replay survives a rebind: the recording says the player moved, not that
    // they held W.
    //
    // Keyed by NAME rather than by index, because that is how the rest of this
    // file works and because an index would be a second, silent contract about
    // the order of ActionNames(). InputRecording turns names into indices when
    // it writes a file, where the saving is worth the bookkeeping.
    struct TickInput {
        std::vector<std::string> down;      // actions held for the whole tick
        std::vector<std::string> pressed;   // the latched edges this tick owns
        std::vector<std::string> released;
        std::vector<std::pair<std::string, float>> axes;
        glm::vec2 mouseDelta{0.0f};

        // UI buttons this tick was handed, as plain entity ids.
        //
        // Plain integers rather than an EnTT type, for the reason the key codes
        // at the top of this file are plain ints whose values mirror GLFW's:
        // the value passes straight through, and nothing here has to include a
        // header describing what it means. UIInput fills this and reads it
        // back, because a click belongs to an entity and this file does not
        // know what one is.
        //
        // Here at all because a script reads clicks from inside the tick, so
        // they are part of what a tick was handed and a recording that left
        // them out would not reproduce a menu.
        std::vector<uint32_t> clicked;

        bool operator==(const TickInput& other) const;
        bool operator!=(const TickInput& other) const { return !(*this == other); }
    };

    // What the tick about to run would read, as a value. Call after
    // BeginTickInput, which is what decides which edges this tick owns.
    //
    // Every action and axis that has a binding, including the ones that are not
    // doing anything: an absent name and a name reading zero have to be the
    // same thing on the way back in, or a replay would leave the previous
    // tick's value standing.
    static TickInput CaptureTickInput();

    // Replace the devices with a recorded tick, for the duration of that tick.
    //
    // Only the queries a simulation makes are diverted: IsDown, the two tick
    // edges, GetAxis and MouseDelta. The raw key and button queries are NOT,
    // and that is deliberate - the editor camera, the UI canvas and ImGui all
    // read input once per frame, from OUTSIDE the tick, and a replay that fed
    // them recorded values would stop the person watching it from being able to
    // move the camera or press stop.
    //
    // Bracketed rather than latched for the same reason: live between Begin and
    // End, which is exactly the span the tick occupies, so nothing that runs
    // per frame can see a replayed value even by accident.
    static void BeginReplayedTick(const TickInput& input);
    static void EndReplayedTick();
    static bool ReplayingTick();

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

    // ---- Contacts ---------------------------------------------------------
    //
    // What the engine could not express before: `mousePosition` is ONE point,
    // so a two-finger gesture arrived as a single jittering pointer somewhere
    // between the fingers, and a machine keyed on finger index had no index to
    // key on.
    //
    // These are DERIVED, not passed through. Every frame the current snapshot
    // is compared against the previous one to work out which contacts are new,
    // which are still down, and which have just lifted - the last of which does
    // not appear in the current snapshot at all and would otherwise vanish
    // without ever reporting an end.
    //
    // Ordering: everything still down first, in the order the device reported
    // it, then everything that lifted this frame. Stable enough to iterate,
    // and not something to index into by position - use the id.

    // What a desktop reports instead of fingers: the mouse, as contact 0,
    // while its left button is held.
    //
    // Lives here rather than in the polling layer so it can be tested. Without
    // it a gesture machine written against contacts is dead code until Android
    // exists, which is the same as untested - and the day it stops being dead
    // code is the worst possible day to find out it was wrong.
    //
    // Held, not hovering. A finger that is not touching the screen is not
    // reported at all, and a contact that existed whenever the pointer was over
    // the window would make every gesture begin the moment the mouse entered
    // it. Overwrites whatever was in slot 0, so a platform that reports real
    // contacts must not call this.
    //
    // Silent while CursorCaptureSuppressed() is raised, which is the same veto
    // the cursor mode obeys and is here for the same reason: while the host
    // owns the pointer - the editor between plays, a viewport that is not
    // focused, a player who pressed Escape - a drag across an inspector field
    // is not a gesture anybody made. The mouse has that veto already; contacts
    // are a second thing derived from the mouse and would otherwise be the one
    // path around it.
    static void SynthesiseMouseContact(RawInputState& state);

    static int ContactCount();

    // By position in that list. Returns a contact with an id of -1 for an index
    // nobody has, rather than failing, so a loop that runs one past the end is
    // a contact that is not there instead of a crash.
    static Contact GetContact(int index);

    // By id, which is what a gesture machine actually holds: it decided PAN on
    // finger 1 and needs finger 1 again next frame, not "whatever is second in
    // the list now".
    static bool TryGetContact(int id, Contact& out);

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
