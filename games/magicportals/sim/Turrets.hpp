#pragma once

// Carrancas: the wall-mounted stone gargoyles that spit fireballs along their
// own (dirX, dirY), which the remake's role table calls `turret`.
//
// WHY THIS EXISTS. Nineteen of chapter 2's levels carry one, and ten of them -
// level0a through level8a and level24a - carry nothing else the port cannot
// play. So this is the single role that turns the most levels from "starts" into
// "plays", which is what test_mp_start counts.
//
// The original is ETHCallback_carranca.angelscript. Two things live in its
// callback and they are unrelated:
//   - FIRING, which is what this is: a stride clock, and addFireball.
//   - BEING DESTROYED, which spawns carranca_destroy.ent, shakes the screen
//     (startEarthquake) and deletes the entity. That is the burnable side, it
//     belongs with fire_source and explosive, and it is not built here.
//
// What a fireball is, from its own fireball.ent: a 16 x 16 sensor with density 0
// travelling at 130 px/s, with gravityScale set to 0 at spawn. A SENSOR, so the
// world does not stop it - it flies through walls, crates and stones and only
// registers what it overlaps. That is the entity's own semantics rather than a
// simplification of them.
//
// NOT BUILT, and named here rather than found later:
//   - Portal transit. fireball.ent carries teleportable 1, and the original's
//     PortalManager tests for fireball.ent by name in its teleport path, so a
//     fireball goes through portals. Portals::travellers takes entities, and a
//     fireball here is a bare point with no body, as the beholder's spikes are.
//     Giving it one is its own step.
//   - The picture. fireball.ent has no <Sprite> at all: what is seen is its
//     ParticleSystem and its Light, so there is no image for the layer to draw.

#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Turrets {

// The port's turrets.json. Decoded but for the cull margin.
struct Rules {
    double speedPx = 0.0;  // fireball.ent's own speed, 130
    glm::dvec2 hitPx{0.0}; // and its Collision, 16 x 16
    double defaultStrideMs = 0.0;
    glm::dvec2 cullMarginPx{0.0}; // _guess
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

struct Turret {
    std::string name;
    glm::dvec2 atPx{0.0};
    glm::dvec2 directionPx{0.0}; // a unit vector, +y down

    double strideMs = 0.0;
    // Seeded from the node's startStride, as carrancaCallback seeds elapsedTime,
    // so the first shot comes stride - startStride ms in rather than at once.
    double elapsedMs = 0.0;
    int fired = 0;
};

struct Fireball {
    std::string name; // "<carranca>#<n>", counting from 1
    glm::dvec2 atPx{0.0};
    glm::dvec2 velocityPx{0.0};
};

struct State {
    Rules rules;
    std::vector<Turret> turrets;
    std::vector<Fireball> fireballs;
    glm::dvec2 boundsPx{0.0}; // the level's, for the cull

    bool playerKilled = false;
    std::string killedBy; // the fireball the player died in

    // Before the step: every carranca whose stride has passed spits one.
    //
    // Returns what was fired this tick, which is also in `fireballs`. The caller
    // takes that to play the sound - AudioManager::playFireballSound, which
    // sounds.json already names - because a sound is the layer's and never the
    // tick's.
    std::vector<Fireball> Fire(float dt);

    // After the step: the fireballs fly, leave by the level's edge, and kill.
    void Tick(entt::registry& registry, entt::entity player, float dt);
};

// Every carranca of a level, and the level's bounds to cull against. False, with
// `error`, for one whose stride does not read or is not positive.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error);

} // namespace MagicPortals::Turrets
