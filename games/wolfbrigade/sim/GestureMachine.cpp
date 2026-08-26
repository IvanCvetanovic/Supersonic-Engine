#include "sim/GestureMachine.hpp"

namespace WolfBrigade {

namespace {

// Godot's Rect2(top_left, (a - b).abs()), which is a rectangle from two corners
// in whichever order they arrived. Dragging up and left has to produce the same
// box as dragging down and right, or half the marquees a player draws select
// nothing.
GestureMachine::Rect rectFromPoints(const glm::vec2& a, const glm::vec2& b) {
    GestureMachine::Rect rect;
    rect.min = glm::min(a, b);
    rect.max = glm::max(a, b);
    return rect;
}

} // namespace

void GestureMachine::SetPlacementMode(bool on) {
    m_placementMode = on;

    // Everything, not just the mode. A gesture left half-latched resumes the
    // moment placement ends, which reads as the game marquee-selecting on its
    // own after the player put a building down.
    m_finger = -1;
    m_mode = Mode::None;
}

GestureMachine::Intents GestureMachine::Step(const Supersonic::Contact* contacts, int count,
                                             float now) {
    Intents intents;

    // Placement mode owns the pointer. The GDScript returns early from
    // _unhandled_input for exactly this reason, and the placement intents live
    // with the placement system rather than here.
    if (m_placementMode) return intents;

    for (int i = 0; i < count; ++i) {
        const Supersonic::Contact& contact = contacts[i];

        switch (contact.phase) {
        case Supersonic::ContactPhase::Began: {
            // Only if nothing is being tracked. A second finger arriving
            // mid-gesture is IGNORED rather than taken - two fingers fighting
            // over one marquee is worse than one finger finishing it, and a
            // player resting a thumb on the screen would otherwise cancel
            // every drag.
            if (m_finger != -1) break;

            m_finger = contact.id;
            m_pressPosition = contact.position;
            m_pressTime = now;
            m_mode = Mode::Undecided;
            break;
        }

        case Supersonic::ContactPhase::Moved: {
            if (contact.id != m_finger) break;

            if (m_mode == Mode::Undecided) {
                const float moved = glm::length(contact.position - m_pressPosition);

                // Strictly past, matching the GDScript's `>`. A real tap ships
                // several drags of a few pixels, and treating those as a
                // gesture makes tapping a unit pan the camera instead.
                if (moved > dragThresholdPx) {
                    // HELD first is a band-select; moved straight away is a
                    // pan. The distinction is time, not distance - which is
                    // why a threshold on movement alone cannot express it.
                    const bool held = (now - m_pressTime) >= holdSelectSeconds;
                    m_mode = held ? Mode::Marquee : Mode::Pan;
                }
            }

            // Latched from here until the finger lifts: a marquee that turned
            // into a pan because the player slowed down would be unusable.
            if (m_mode == Mode::Pan) {
                intents.panned = true;
                intents.panDelta = -contact.delta;
            } else if (m_mode == Mode::Marquee) {
                intents.marqueeVisible = true;
                intents.marquee = rectFromPoints(m_pressPosition, contact.position);
            }
            break;
        }

        case Supersonic::ContactPhase::Ended: {
            if (contact.id != m_finger) break;

            if (m_mode == Mode::Marquee) {
                intents.boxSelect = true;
                intents.box = rectFromPoints(m_pressPosition, contact.position);
                intents.marqueeHidden = true;
            } else if (m_mode == Mode::Undecided) {
                // Down and up without ever committing. Not "a tap is a short
                // press" - a press held for a minute and lifted without moving
                // is still a tap, because nothing else ever claimed it.
                intents.contextTap = true;
                intents.tapPosition = contact.position;
            }
            // A pan has already been applied, frame by frame, and has nothing
            // left to do on release.

            m_finger = -1;
            m_mode = Mode::None;
            break;
        }
        }
    }

    return intents;
}

} // namespace WolfBrigade
