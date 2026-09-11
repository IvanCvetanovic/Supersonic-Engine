#pragma once

// Trigger boxes, and what is in them.
//
// The port's own overlap test rather than an engine query. The port asks about
// its triggers in its own tick (the planning doc says why), and PhysicsSystem's
// queries test anything but a sphere by its bounding box, which would count a
// crate tipped on one corner as covering the whole box around it. This is exact
// in the plane for the colliders a level's bodies have: a box turned about z, a
// sphere, and a capsule along its local y. It reads an entity's transform as
// world space and unscaled, which is how LevelBuilder places them.

#include "sim/Tscn.hpp"

#include <string>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Trigger {

// The remake's box for a role entity with no metadata/trigger_size
// (level_runtime.gd:29-31). level30's buttons have none, so theirs is this. The
// 26x16 Area2D the converter gives them is never read by the remake.
inline constexpr double kFallbackSizePx = 16.0;

// An axis-aligned box in the plane, in world metres.
struct Box {
    glm::vec2 centre{0.0f};
    glm::vec2 half{0.0f};
};

// The trigger LevelRuntime._attach_trigger gives a role node
// (level_runtime.gd:243-265): metadata/trigger_size at metadata/trigger_offset,
// or kFallbackSizePx at the node. False, with `error`, for a rotated node, whose
// box would be turned with it. No level30 role node is rotated.
bool FromNode(const Tscn::Node& node, Box& out, std::string& error);

// Whether an entity's collider overlaps the box. Touching is not overlapping. False
// for an entity with no box, sphere or capsule.
bool Overlaps(entt::registry& registry, entt::entity entity, const Box& box);

// How many dynamic bodies overlap the box: rigid bodies that are not kinematic.
// This is what presses a button. The remake was probed, and its triggers report a
// crate dropped on a button but not the floor under each of level30's three. The
// original's Box2D pairs a sensor only with a dynamic body.
int DynamicBodiesIn(entt::registry& registry, const Box& box);

} // namespace MagicPortals::Trigger
