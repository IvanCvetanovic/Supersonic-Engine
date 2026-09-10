#pragma once

#include <string>

#include <glm/glm.hpp>

namespace WolfBrigade {

// What a press means while the player is the hero, from the hero-mode half of
// `scripts/input/input_controller.gd`.
//
// A ROUTER, with no gesture state, and it sits ABOVE GestureMachine rather
// than inside it. The original's `_handle_hero_input` is a three-branch
// dispatch that remembers nothing, and GestureMachine's whole value is being a
// touch FSM that is testable without a viewport; folding a mouse and a
// keyboard into it would spend that for no gain. So the caller asks this
// first, and only an event it passes through reaches the gesture machine.
//
// Hero mode is the PERMANENT in-match state in the original (direction B):
//   - Q and E cast ability slots 0 and 1, and Escape backs out of the
//     innermost menu - the deselect signal, which the match turns into
//     "close the Build menu";
//   - on the DESKTOP scheme a primary click strikes toward the point;
//   - everything else is inert: a right click, a touch, a release. Buildings
//     answer to proximity and the army to its squads, so there is nothing for
//     a stray tap to select, and on the TOUCH scheme the on-screen controls
//     consume their own touches before anything reaches here.
//
// PLACEMENT OUTRANKS IT, as it outranks everything in the original: a primary
// click confirms, a secondary click or Escape cancels.
//
// Everything is SCREEN space, as it is in GestureMachine, and for the same
// reason: converting to the world needs the camera that drew the frame, and
// the caller is the thing that has one.
class HeroInput {
public:
    // One press or key, in the engine's own codes (Supersonic::Key and
    // Supersonic::MouseButton values).
    struct Event {
        enum class Kind { Key, MouseButton, Touch };

        Kind kind{Kind::Key};
        int code{0};
        glm::vec2 position{0.0f};
        bool pressed{true};
    };

    // What the event meant. At most one of these is set.
    struct Intents {
        bool attackAt{false};
        glm::vec2 attackPosition{0.0f};

        // The ability slot, or -1.
        int ability{-1};

        // Escape: back out of the innermost thing.
        bool deselect{false};

        bool placementConfirm{false};
        glm::vec2 placementPosition{0.0f};
        bool placementCancel{false};

        // Not this router's to answer: outside hero mode, the ordinary pointer
        // - the gesture machine, a click that selects, a click that orders -
        // takes it. A touch during placement also passes, because one-finger
        // placement is the gesture path's already.
        bool passThrough{false};
    };

    // Entering hero mode caches the scheme, as the original does at mode
    // entry: Settings is only reachable outside a match, so it cannot change
    // underneath a mode. `scheme` is a resolved one - ControlScheme::Resolved.
    //
    // A caller that also owns a GestureMachine abandons its gesture on every
    // mode change, which is the original's `_reset_gesture`; this holds none.
    void SetHeroMode(bool on, const std::string& scheme);
    bool HeroMode() const { return m_heroMode; }
    bool TouchScheme() const { return m_touchScheme; }

    void SetPlacementMode(bool on) { m_placementMode = on; }
    bool PlacementMode() const { return m_placementMode; }

    Intents Route(const Event& event) const;

    // `_keyboard_dir`, and the one line of `_process` that shapes it for the
    // hero: A/Left, D/Right, W/Up and S/Down, y down the screen, and a
    // diagonal NORMALISED so it is not faster than a straight line - but only
    // when it is longer than one, so a single key stays exactly one. Godot's
    // normalise, which divides by the length rather than multiplying by an
    // inverse square root, in single precision because Vector2 is.
    static glm::vec2 SteerFromKeys(bool left, bool right, bool up, bool down);

private:
    bool m_heroMode{false};
    bool m_touchScheme{false};
    bool m_placementMode{false};
};

} // namespace WolfBrigade
