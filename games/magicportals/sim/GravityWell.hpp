#pragma once

// Gravity wells: entity `gravity_agent` / `gravity_agent.ent`, role
// `gravity_well`. Ten placements across five levels, and the last mechanic in
// the game that is not a boss.
//
// A well is THREE things at once, and the port already had the first:
//   - a SOLID CIRCLE. The node is a StaticBody2D with a CircleShape2D, so it is
//     an obstacle and a portal shot already dies on it (Shot::FirstBody walks
//     sphere colliders and skips only triggers). Nothing here has to build that.
//   - an ATTRACTOR, which is this module: every DYNAMIC body inside its radius
//     is pulled toward the centre. The original's DynamicBodyChooser rejects
//     statics and anything with no physics controller, so platforms and walls
//     stand still inside a well and only loose bodies move.
//   - a NO-PORTAL ZONE. ETHCallback_gravity_agent adds an antiportal.ent at its
//     own position on the first tick, so no portal may open inside its reach.
//     Nothing in the level file says `antiportal`; it exists only in the
//     callback, which is why a port reading the .tscn alone would never find it.
//     Game::Start hands one to Portals::State::zones for each well.
//
// THE FORCE, decoded (ETHCallback_gravity_area, bytes 461443..462571):
//
//     forceDir  = normalize(centre - body)      // toward the centre: it ATTRACTS
//     forceBias = smoothEnd(1 - d^2/r^2)        // sin(x * PI/2), an ease-out
//     force     = forceDir * forceBias * forceLength * (dt_ms / 16.6666)
//     velocity += force                         // ADDED, never set
//
// forceLength is 0.18 where the world has no gravity and 0.5 where it has, and
// that splits these five levels exactly: level16c, level17c and level18c are
// no_gravity levels, level20c and level27c are not. gravitywell.json holds the
// decode, the units, and the one number in it that is inferred rather than
// decoded (PIb).

#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::GravityWell {

struct Rules {
    double forceZeroGravity = 0.0; // 0.18, where the world's gravity is V2_ZERO
    double forceWithGravity = 0.0; // 0.5, where it is not
    double referenceFrameMs = 0.0; // 16.6666: the frame the force is quoted per
    double zoneShrinkPx = 0.0;     // 24: the antiportal's SIZE is radius*2 - this
};

// False, with `error`, for a file that does not read, and for one whose numbers
// would make a well that does nothing or a zone bigger than its own well.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

struct Well {
    std::string name;
    glm::dvec2 atPx{0.0};
    double radiusPx = 0.0;

    // The radius INSIDE WHICH NO PORTAL MAY OPEN, which is not the well's own.
    // The agent sizes its antiportal to radius*2 - 24 and an antiportal refuses
    // a tap within half its size, so this is radius - 12. gravitywell.json shows
    // the arithmetic; getting it wrong is 12 px of silently wrong zone at every
    // placement in the game.
    double ZoneRadiusPx(const Rules& rules) const { return radiusPx - rules.zoneShrinkPx * 0.5; }
};

// The velocity a single well adds to a body this tick, in the engine's plane.
// Zero for a body outside the well, and for one exactly at the centre - which
// is the case normalize cannot answer.
//
// `noGravity` picks the strength, and it is the LEVEL's flag rather than a
// question put to the registry: the original branches on GetGravity() ==
// V2_ZERO, and Game::Level::noGravity is what set that in the first place.
glm::vec3 Pull(const Well& well, const Rules& rules, const glm::dvec2& bodyPx, bool noGravity, float dt);

struct State {
    Rules rules;
    std::vector<Well> wells;
    bool noGravity = false; // the level's, set by Game::Start

    // One step, before the physics step, over every DYNAMIC body: the ones with
    // a RigidBodyComponent that are not kinematic. A kinematic body is moved by
    // code and would fight whatever moves it; a static one has no velocity at
    // all. That is the port's reading of DynamicBodyChooser.
    void Tick(entt::registry& registry, float dt);

    const Well* Find(const std::string& name) const;
};

// A level's gravity wells, by role. The radius is the node's own
// metadata/radius, which runs from 64 to 260 px across the game - so unlike a
// bouncer, this IS level-authored data.
//
// False, with `error`, for a well with no position or no radius: the original
// reads GetFloat('radius') and a well without one would pull nothing anywhere.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error);

} // namespace MagicPortals::GravityWell
