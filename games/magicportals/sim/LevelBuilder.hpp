#pragma once

// A converted level, as bodies in an engine registry.
//
// What the remake's scene tree gives Godot's 2D physics, given to the engine's
// 3D solver instead, one entity per body:
//
//   StaticBody2D + rectangle   a box collider and no rigid body
//   StaticBody2D + polygon     a convex hull, from an OBJ prism (Prism.hpp)
//   StaticBody2D + circle      a sphere collider
//   RigidBody2D  + any of them the same collider, with a rigid body
//   Area2D       + any of them the same collider, as a trigger
//
// The entity is placed at its node's position and turned by its rotation, and a
// shape's own offset (30 of them, all on carranca and wood_piece_small) goes
// into the collider - a box's or sphere's centre, or the prism's points.
//
// Given the role table, it also does the one thing the remake's LevelRuntime
// does to a body: a StaticBody2D whose role moves (Roles::Moves) becomes a
// kinematic body, as the remake makes it an AnimatableBody2D, so it can carry
// what stands on it. Without the table - the spike - every StaticBody2D stays
// static. The rest of LevelRuntime (triggers, behaviours) is the caller's.

#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::LevelBuilder {

// How far a body reaches along z, the axis a 2D level does not have. Static
// geometry is deeper than what moves over it, so a crate's footprint lies
// wholly inside the face it rests on: edge-on-edge contact is the case most
// likely to push a body out of the plane, and the drift measurement should see
// the solver rather than a coincidence of extents. Chosen for the spike, and
// recorded in its doc - nothing in the original says what these should be.
inline constexpr double kStaticDepthMetres = 2.0;
inline constexpr double kBodyDepthMetres = 1.0;

// Rigid bodies get Godot's PhysicsMaterial defaults, which is what the remake's
// RigidBody2D crates run with: friction 1, bounce 0. Its movers, AnimatableBody2D
// with no material either, get the same.
inline constexpr float kBodyFriction = 1.0f;
inline constexpr float kBodyRestitution = 0.0f;

// Statics get the same, through PhysicsMaterialComponent: the remake's
// StaticBody2D floors carry no material either, so they run on Godot's defaults.
// Left without one they would take the engine's fallback for a collider with no
// rigid body - bounce 0.3, grip 0.4 - and every landing would come back up.
inline constexpr float kStaticFriction = 1.0f;
inline constexpr float kStaticRestitution = 0.0f;

// The 2D port's locks: every rigid body stays in the plane - no motion along z -
// and turns about z only. Unlocked, the spike measured a resting crate 30 px out
// of the plane in a minute and a pushed one leaving the level. S1, recorded in
// the spike doc.
inline const glm::bvec3 kPlaneLockPosition{false, false, true};
inline const glm::bvec3 kPlaneLockRotation{true, true, false};

struct Options {
    std::filesystem::path prismDirectory;
    // False leaves out every StaticBody2D, movers included: the mutation a
    // landing check must fail, since a body over no geometry has nothing to
    // land on.
    bool withStatics = true;
    // Every rigid body locked to the plane (kPlaneLock*). Off only to measure
    // what the solver does without the locks: MagicPortalsSpike --unlocked.
    bool lockToPlane = true;
    // The role table, when the level is to be played rather than measured: its
    // movers are then built kinematic. Not owned.
    const Roles::Table* roles = nullptr;
};

struct Built {
    std::unordered_map<std::string, entt::entity> entities; // by the entity node's name
    int statics = 0;
    int rigids = 0;
    int areas = 0;
    int movers = 0;
    int hulls = 0;
};

bool Build(const Tscn::Scene& scene, entt::registry& registry, const Options& options, Built& out,
           std::string& error);

// One entity node, by name, added to `out` - a level's piece on its own, for a
// rig that must not have the rest of the level in the way. entt::null, with
// `error` set, when the node is not an entity with a body.
entt::entity BuildEntity(const Tscn::Scene& scene, const std::string& nodeName, entt::registry& registry,
                         const Options& options, Built& out, std::string& error);

// The box an entity's body takes up, in the remake's pixels, relative to its
// node and before the node's rotation: a rectangle's size, a circle's diameter,
// or a polygon's bounds, centred where the shape's own offset puts it. False for
// a node with no Body/Shape.
bool ShapeBoundsPx(const Tscn::Scene& scene, const Tscn::Node& node, glm::dvec2& offsetPx, glm::dvec2& sizePx);

// A rigid circle with no node behind it, built as a RigidBody2D with a
// CircleShape2D is: what a launcher throws (Launchers.hpp). Not added to any
// Built; the caller owns it.
entt::entity BuildRigidCircle(entt::registry& registry, const std::string& tag, const glm::dvec2& atPx,
                              double radiusPx, const Options& options);

// A trigger box from an entity's metadata/trigger_size and trigger_offset, the
// way LevelRuntime._attach_trigger builds one (level_runtime.gd:243-265).
// entt::null, with `error` set, when the entity has no trigger_size.
entt::entity AddTriggerFromMetadata(const Tscn::Scene& scene, const std::string& entityName,
                                    entt::registry& registry, std::string& error);

} // namespace MagicPortals::LevelBuilder
