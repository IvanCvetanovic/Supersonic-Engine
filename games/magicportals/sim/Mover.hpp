#pragma once

// What moves a level's movers.
//
// F1 (the engine's 89f48a8) made the contact solve read a kinematic body's
// velocity. A mover that moves its transform and leaves its velocity at zero is
// only a displacement again - it shoves what it meets and carries nothing - and
// it says nothing about it. So nothing in the port moves a kinematic body except
// MoveKinematic, which writes both.

#include "sim/Tscn.hpp"

#include <string>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Mover {

// Puts a kinematic body at `target` for the step about to be taken, with the
// velocity that gets it there in `dt`: (target - where it is) / dt.
void MoveKinematic(entt::registry& registry, entt::entity entity, const glm::vec3& target, float dt);

// A switched door, behaviours.gd's Mover.gated (:140-148, :170-176): it opens by
// rising its own height, 126 px, over `stride` milliseconds, and closes the same
// way. Reversed mid-travel it resumes from where it is rather than snapping.
inline constexpr double kDoorRisePx = 126.0;

struct Door {
    glm::vec3 closed{0.0f}; // world metres, where the level places it
    glm::vec3 open{0.0f};
    float durationS = 1.0f;
    float progress = 0.0f; // 0 closed, 1 open
    bool opening = false;

    // One step of travel, and the body moved there (MoveKinematic).
    void Tick(entt::registry& registry, entt::entity entity, float dt);
};

// A door as the level places it: closed where the node stands, opening upward by
// kDoorRisePx over its metadata/stride milliseconds - a bare number with no
// `speed`, so a duration, by behaviours.gd's stride rule. False, with `error`,
// when the node has no stride.
bool DoorFromNode(const Tscn::Node& node, Door& out, std::string& error);

} // namespace MagicPortals::Mover
