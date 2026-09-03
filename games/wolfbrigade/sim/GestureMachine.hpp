#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "core/Input.hpp"

namespace WolfBrigade {

// The four-state gesture machine from `scripts/input/input_controller.gd`.
//
// A PORT, not a redesign. Wolf Brigade decides tap vs pan vs band-select from
// one finger, and the rules are not obvious ones: a real tap ships several
// sub-threshold jitter drags and has to stay a tap; a second finger arriving
// mid-gesture has to be ignored rather than fight the first; and whether a drag
// is a pan or a marquee is decided by whether the finger was HELD before it
// moved, not by where it moved to. Those rules came out of a real device, and
// changing any of them here would make the port a different game.
//
// Verified against the original. `tools/verify_touch.gd` in the game repo drives
// exactly these cases through the GDScript and prints what fired; the test beside
// this file asserts the same answers.
//
// Two deliberate differences from the GDScript, both forced and both harmless:
//
//   1. Godot pushes InputEventScreenTouch/ScreenDrag; this engine polls a list
//      of contacts once a frame. The correspondence is exact - a press is a
//      Began, a drag is a Moved carrying its own delta, a lift is an Ended - so
//      the FSM is the same machine reading the same transitions. The only
//      visible change is that a stationary finger produces a Moved frame with a
//      zero delta where Godot would send nothing, and every branch below that
//      could care is guarded by a threshold rather than by the arrival of an
//      event.
//
//   2. Everything here is SCREEN space. The GDScript converts taps and the
//      committed box to world coordinates through the viewport's canvas
//      transform, which needs a viewport. Doing that here would drag a camera
//      into a file whose entire value is being testable without one, so the
//      caller converts - it is the thing that knows which camera drew the frame.
class GestureMachine {
public:
    // A rectangle from two corners in any order, which is Godot's
    // Rect2(top_left, (a - b).abs()).
    struct Rect {
        glm::vec2 min{0.0f};
        glm::vec2 max{0.0f};

        glm::vec2 size() const { return max - min; }
    };

    // What happened this frame. Every field is false or empty on a frame where
    // nothing did, which is almost all of them.
    //
    // A struct of flags rather than signals because this engine polls: a caller
    // reads the result of the frame it just stepped, in one place, instead of
    // being called back from inside the machine while its state is half
    // updated.
    struct Intents {
        // Down and up without ever committing to a gesture. The mobile
        // select-or-command path, which is a different one from the desktop
        // click - see Commands::ContextTap.
        bool contextTap{false};
        glm::vec2 tapPosition{0.0f};

        // The camera should move by this. Already negated: the camera follows
        // the finger, so dragging right shows what is to the left.
        bool panned{false};
        glm::vec2 panDelta{0.0f};

        // The live band-select box, for drawing. Screen space, and it is
        // screen space in the original too: the box is drawn from the point the
        // finger pressed, so a camera that moved mid-drag would make the drawn
        // box and the selected region disagree.
        bool marqueeVisible{false};
        Rect marquee;

        // Stop drawing it. Fires on release and on a mode change, so a gesture
        // interrupted by the build menu cannot leave a box on screen.
        bool marqueeHidden{false};

        // The band-select is committed. Whatever is inside this rectangle is
        // what the player selected.
        bool boxSelect{false};
        Rect box;
    };

    // From `data`-adjacent @export values in the GDScript, and they are tuned
    // numbers rather than round ones: 12 pixels is the distance a finger moves
    // while tapping, and 300ms is how long a deliberate hold takes.
    float dragThresholdPx{12.0f};
    float holdSelectSeconds{0.300f};

    // One frame. `contacts` is what Input reports this frame and `now` is a
    // monotonic clock in seconds - the simulation's, not the wall's, so a
    // gesture behaves the same in a replay as it did live.
    Intents Step(const Supersonic::Contact* contacts, int count, float now);

    // Building placement takes the pointer, and any gesture in progress is
    // abandoned rather than left latched: a mode change during a drag would
    // otherwise resume a marquee when placement ended.
    void SetPlacementMode(bool on);
    bool PlacementMode() const { return m_placementMode; }

    // Drop whatever gesture is in progress, without ending it.
    //
    // NEEDED BECAUSE STEPPING WITH NO CONTACTS IS NOT THE SAME THING. The
    // machine ends a gesture when it sees an Ended contact, so a frame with an
    // empty contact list leaves the finger tracked exactly as it was - which
    // is right for a finger that is still down and wrong for one the game
    // stopped watching. Call this when something else takes the pointer.
    void Abandon();

    // Which finger is being tracked, or -1. Exposed because "the second finger
    // is ignored" is a claim worth asserting directly rather than inferring
    // from what did not fire.
    int TrackedFinger() const { return m_finger; }

    enum class Mode : uint8_t { None, Undecided, Pan, Marquee };
    Mode CurrentMode() const { return m_mode; }

private:
    bool m_placementMode{false};

    int m_finger{-1};
    glm::vec2 m_pressPosition{0.0f};
    float m_pressTime{0.0f};
    Mode m_mode{Mode::None};
};

} // namespace WolfBrigade
