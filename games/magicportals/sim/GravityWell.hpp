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
//   - and A GREEN RING, which is that same added entity SEEN: white_ring.png at
//     the zone's own diameter, added, painted (0.25, 0.5, 0.25) by the callback
//     that adds it. It is the one part of a well that is drawn rather than felt,
//     and it is drawn by the layer from `Rules::ring` (gravitywell.json).
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

// THE RING the agent adds with that zone, and the one thing about a well that is
// seen rather than felt: an antiportal.ent, sized to the zone and painted green
// by `area.SetColor(vector3(0.5, 1, 0.5) * 0.5)`. It is renamed `gravity_area` at
// AddEntity, so the callback it gets is the force above and not
// ETHCallback_antiportal: no blink, no turn, one size. gravitywell.json `ring`
// carries the instructions, and the rest of the row is antiportal.ent's own file.
struct Ring {
    std::string sprite;       // its <Sprite>: white_ring.png
    bool additive = false;    // its blendMode 1
    glm::dvec3 colour{0.0};   // what the callback paints it: (0.25, 0.5, 0.25)
    glm::dvec3 emissive{0.0}; // its <EmissiveColor>, rgb: (1, 1, 1)
    bool applyLight = false;  // its applyLight
    bool isStatic = false;    // its static
    double z = 0.0;           // AddEntity's vector3(pos, -5): its depth, and its lighting height
};

struct Rules {
    double forceZeroGravity = 0.0; // 0.18, where the world's gravity is V2_ZERO
    double forceWithGravity = 0.0; // 0.5, where it is not
    double referenceFrameMs = 0.0; // 16.6666: the frame the force is quoted per
    double zoneShrinkPx = 0.0;     // 24: the antiportal's SIZE is radius*2 - this
    Ring ring;                     // and what that antiportal looks like
};

// False, with `error`, for a file that does not read, and for one whose numbers
// would make a well that does nothing or a zone bigger than its own well.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

struct Well {
    std::string name;
    glm::dvec2 atPx{0.0};
    double radiusPx = 0.0;

    // THE SIZE the agent's scaleToSize gives its antiportal, across: radius*2 -
    // 24, which is what is DRAWN. 188 units at level16c's radius of 106.
    double RingSizePx(const Rules& rules) const { return radiusPx * 2.0 - rules.zoneShrinkPx; }

    // The radius INSIDE WHICH NO PORTAL MAY OPEN, which is not the well's own:
    // an antiportal refuses a tap within GetSize().x * 0.5, so it is half the
    // size above - radius - 12, at every one of the ten placements. Derived from
    // the ring in that order, as the original derives it, so that a ruling about
    // the refusal (00_order R2) cannot move the picture; getting it wrong is 12
    // px of silently wrong zone at every placement in the game.
    double ZoneRadiusPx(const Rules& rules) const { return RingSizePx(rules) * 0.5; }
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
