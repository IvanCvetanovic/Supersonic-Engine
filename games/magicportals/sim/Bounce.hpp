#pragma once

// The bouncers of chapter 4's weightless rooms: entity `bounce`, role `bouncer`.
//
// A slab of solid geometry that bobs gently up and down. NOT a trampoline - the
// role table calls it "a solid polygon surface that throws things off it" and
// that is wrong about the mechanism: ETHCallback_bounce imparts no impulse to
// anything. It calls linearMotion, which moves the ENTITY. What gets carried is
// only what any moving solid carries.
//
//     angle  += speed * min(dt, 200 ms)      // 1.2 rad/s, a bob every 5.24 s
//     offset  = cos(angle) * stride          // stride 1.0 px
//     at      = origin + (0, offset)         // vertical: ETHCallback_bounce's own argument
//
// A SIBLING OF Mover::Oscillation RATHER THAN A USE OF IT, and deliberately so.
// That one is sin(rateScale * speed * t) * stride * 0.5 along an axis, reads its
// speed and stride from the NODE, and refuses a node that carries neither. A
// bounce node carries neither, because the original injects them from code
// (setNoGravityLinearMotionProperties). Four differences - cosine not sine, no
// half-stride, a rate in radians per second with a frame clamp, and constants
// from the binary - so bounce.json holds the decode and this holds the motion.
//
// What IS shared: Supersonic::DetMath, so a bob comes out the same on every C
// runtime as a swing does, and Mover::MoveKinematic, so the body carries what
// stands on it. The second needs `bouncer` in Roles::Moves - without it the
// builder makes the body static and a bob would move a slab that carries
// nothing, which is a mechanism that looks built and is not.

#include "sim/LevelBuilder.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Bounce {

// The port's own bounce.json. Every number in it comes from the binary rather
// than from a level, which is the reverse of the rest of this port's movers.
struct Rules {
    double speedRadPerSec = 0.0; // 1.2, the rate the angle advances at
    double stridePx = 0.0;       // 1.0: how far it travels either way
    double frameClampMs = 0.0;   // 200: the original's guard on a long frame
    bool vertical = true;        // ETHCallback_bounce's own argument to linearMotion
};

// False, with `error`, for a file that does not read - and for one whose speed,
// stride or clamp is not above zero. Each of those would be a bouncer that never
// moves, or one a single long frame could throw across the room.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

struct Bob {
    std::string name;
    entt::entity entity = entt::null;
    glm::dvec2 originPx{0.0}; // where the level places it: the middle of the bob
    double angle = 0.0;       // radians, wrapped at 2*PI as the original wraps it

    glm::dvec2 AtPx(const Rules& rules) const;
};

struct State {
    Rules rules;
    std::vector<Bob> bobs;

    // One step, before the physics step, each body moved with MoveKinematic so
    // that it carries what stands on it - as Mover::Movers::Tick does.
    void Tick(entt::registry& registry, float dt);

    const Bob* Find(const std::string& name) const;
};

// A built level's bouncers, selected BY ROLE. One mechanism under one role, as
// shock_field is, so there is no reason to go by entity name here - that is what
// Diamonds has to do, and only because two mechanisms share `pickup`.
//
// False, with `error`, for a bouncer with no position or one that was not built.
bool Wire(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, const Rules& rules,
          State& out, std::string& error);

} // namespace MagicPortals::Bounce
