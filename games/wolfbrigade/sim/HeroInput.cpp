#include "sim/HeroInput.hpp"

#include <cmath>

#include "core/Input.hpp"
#include "sim/ControlScheme.hpp"

namespace WolfBrigade {

void HeroInput::SetHeroMode(bool on, const std::string& scheme) {
    m_heroMode = on;
    m_touchScheme = scheme == ControlScheme::kTouch;
}

HeroInput::Intents HeroInput::Route(const Event& event) const {
    Intents intents;

    // Placement first: it outranks hero mode, as it outranks everything.
    if (m_placementMode) {
        if (event.kind == Event::Kind::MouseButton && event.pressed) {
            if (event.code == Supersonic::MouseButton::Left) {
                intents.placementConfirm = true;
                intents.placementPosition = event.position;
            } else if (event.code == Supersonic::MouseButton::Right) {
                intents.placementCancel = true;
            }
        } else if (event.kind == Event::Kind::Key && event.pressed &&
                   event.code == Supersonic::Key::Escape) {
            intents.placementCancel = true;
        } else if (event.kind == Event::Kind::Touch) {
            intents.passThrough = true;
        }
        return intents;
    }

    if (m_heroMode) {
        if (event.kind == Event::Kind::Key && event.pressed) {
            if (event.code == Supersonic::Key::Q) {
                intents.ability = 0;
            } else if (event.code == Supersonic::Key::E) {
                intents.ability = 1;
            } else if (event.code == Supersonic::Key::Escape) {
                intents.deselect = true;
            }
        } else if (event.kind == Event::Kind::MouseButton && event.pressed &&
                   event.code == Supersonic::MouseButton::Left && !m_touchScheme) {
            intents.attackAt = true;
            intents.attackPosition = event.position;
        }

        // Anything else - a right click, a touch, a release - is inert.
        return intents;
    }

    // The ordinary pointer. Escape deselects here too, which is the one key
    // the original's normal mode reads itself.
    if (event.kind == Event::Kind::Key && event.pressed && event.code == Supersonic::Key::Escape) {
        intents.deselect = true;
        return intents;
    }
    intents.passThrough = true;
    return intents;
}

glm::vec2 HeroInput::SteerFromKeys(bool left, bool right, bool up, bool down) {
    glm::vec2 dir(0.0f);
    if (left) dir.x -= 1.0f;
    if (right) dir.x += 1.0f;
    if (up) dir.y -= 1.0f;
    if (down) dir.y += 1.0f;

    // `length() > 1.0` is `lengthSquared > 1` for these vectors, and neither
    // takes a root it does not need.
    const float lengthSquared = dir.x * dir.x + dir.y * dir.y;
    if (lengthSquared > 1.0f) {
        const float length = std::sqrt(lengthSquared);
        dir.x /= length;
        dir.y /= length;
    }
    return dir;
}

} // namespace WolfBrigade
