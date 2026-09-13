#pragma once

// Level 4-32's dark dragon: the game's fourth boss, and the last level of it.
//
// THE WHOLE LEVEL IS ONE MECHANISM, and every piece of it is something this port
// already owns:
//
//   - level31c is `darkest` and holds an unlit torch. Shooting it lights it,
//     which takes the light wall away (Torch) and - in the same act - arms this
//     boss. The original watches for the `light_from_projectile.ent` that a lit
//     torch adds; Torch already counts exactly that event, so this reads its
//     count rather than modelling an entity whose only purpose is to be found.
//   - Three seconds later the marker arms, four seconds after that the dragon
//     arrives and `breakable_wall` comes down. That wall is already a
//     Demolish::Breakable, so the removal goes through Demolish as the torch's
//     own wall does - this level is the only one holding both kinds.
//   - It shoots fireballs at the player, on the same sight gate as level31a's
//     dragon, and they are Turrets::Fireball as every other fireball is.
//   - IT IS HURT ONLY BY FIRE, as the ghost is. level31c places no fire agent and
//     no fire diamond: the only fire in the level is the BARREL BOMB, which is
//     teleportable, and the only thing that can light it is the dragon's own
//     fireball. It restocks that bomb after every shot. The boss supplies its own
//     ammunition and its own detonator, and darkdragon.json has the decode.
//   - Each wound takes your portals away and shoots out the torch - so wounding
//     it costs the room's light and the wall you cleared with it. That is the one
//     place in the game where `darkest` is a mechanic rather than a colour.
//   - At hp 0 it drops a YELLOW KEY, and level31c places the only yellow keyhole
//     and locked door and no key of its own. The fight is the lock, as it is for
//     the ghost.
//
// IT IS A SENSOR WITH NO SOLID BODY (dark_dragon.ent: sensor 1, density 0,
// gravityScale 0), so it cannot touch the player and keeps a position rather than
// an entity - the ghost's shape. That is also why Game tests the blast against it
// by hand: Fire::Blast matches what it grabs by body, and this boss has none.
//
// NOT BUILT, and recorded in data/darkdragon.json: every sound, the animation and
// its particles, the emissive flash, the scattered explosions, the earthquakes,
// the neck bob, and the ghost_utility_spawn.ent dropped beside the key.

#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::DarkDragon {

// The callback's own four, and 1 is never used.
enum class Phase : int { Appearing = 0, Fighting = 2, Damage = 3, Dying = 4 };

// The port's darkdragon.json.
struct Rules {
    std::string spawnName;    // dark_dragon_spawn
    std::string entityName;   // dark_dragon.ent, which no level file places
    std::string wallName;     // breakable_wall
    std::string shoutName;    // shout
    std::string fightingName; // fighting
    std::string keyPosName;      // key_pos
    std::string platformPosName; // platform_pos
    std::string keyColour;       // yellow
    std::string platformEntity;  // single_block_plat_no_emissive.ent
    std::string torchEntity;     // light_off.ent, which a wound shoots out

    double armMs = 0.0;    // 3000: the light stands this long before the marker arms
    double summonMs = 0.0; // 4000: and this long again before the dragon arrives

    double radiusPx = 0.0; // 110, half of dark_dragon.ent's 220 x 220 Collision
    int maxHp = 0;         // 3

    double fireIntervalMs = 0.0;     // 4200
    double fireIntervalStepMs = 0.0; // 600 shorter for each wound taken
    double muzzlePx = 0.0;           // 95

    std::vector<double> appearHoldMs; // seven, summing to 7400
    glm::dvec2 shoutMovePx{0.0};      // (8, -32)

    double damageFirstMs = 0.0;   // 1000
    double damageFlashMs = 0.0;   // 300
    int damageFlashes = 0;        // 6
    glm::dvec2 damageMovePx{0.0}; // (32, -32)

    double dyingHoverMs = 0.0;   // 3000
    double dyingDriftMs = 0.0;   // 6000
    glm::dvec2 fallMovePx{0.0};  // (32, -64)
    double fallPx = 0.0;         // 256
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

// Whether a boss_spawn node is THIS boss. By entity name, as Ghost::Plays is:
// `dark_dragon_spawn` is a marker and carries no `entity` field for Boss::Plays
// to read.
bool Plays(const Rules& rules, const Tscn::Node& node);

struct State {
    Rules rules;

    bool present = false; // the level places a dark_dragon_spawn
    std::string name;
    // The NODE the wall stands as, resolved from the entity name the rules give:
    // Demolish holds its breakables by node name, and `breakable_wall` is what
    // the level calls the entity.
    std::string wallNode;
    glm::dvec2 spawnPx{0.0};
    glm::dvec2 shoutPx{0.0};
    glm::dvec2 fightingPx{0.0};
    bool hasKeyPos = false;
    glm::dvec2 keyPosPx{0.0};
    bool hasPlatformPos = false;
    glm::dvec2 platformPosPx{0.0};

    // THE SUMMON. `armed` is the marker's own `destroy` flag, which deletes
    // nothing: it only says the light has been seen.
    bool armed = false;
    double armingMs = 0.0;
    bool summoned = false;
    double summoningMs = 0.0;

    // THE DRAGON, once it is there.
    bool alive = false;
    bool gone = false;
    glm::dvec2 atPx{0.0};
    Phase phase = Phase::Appearing;
    int hp = 0;
    double elapsedMs = 0.0; // its firing clock
    double phaseMs = 0.0;   // how far through the current arm's path

    // Set from OUTSIDE, by the same Game block that burns a crate: a blast within
    // reach sets this, and Tick reads and clears it exactly as healBurn does. The
    // dragon is not a scene entity and so is never a Fire::Burnable.
    bool burned = false;

    int fired = 0;
    int hits = 0;

    // How long it holds the interval now: 4200, then 3600, then 3000.
    double FireIntervalMs() const;

    // What a tick asks Game to do on its behalf, as Ghost::Turn does.
    struct Turn {
        bool brokeWall = false;   // Demolish should take `breakable_wall` away
        bool killedPortals = false; // PortalManager::killAll
        bool firedAtTorch = false;  // a fireball aimed at the level's torch
        bool fired = false;         // and one aimed at the player
        glm::dvec2 firePx{0.0};
        glm::dvec2 aimPx{0.0}; // a UNIT vector; Game gives it the fireball's speed
        bool droppedKey = false;
        glm::dvec2 keyPx{0.0};
        // NO PLATFORM HERE, and its absence is deliberate. The original also adds
        // a single_block_plat_no_emissive.ent at `platform_pos` as it dies, and
        // this port cannot: LevelBuilder::BuildEntity builds a NODE the scene
        // holds, and there is no node at that marker - the original makes one
        // from the .ent. A flag Game could not act on would be a hole shaped like
        // a check, so there is none. darkdragon.json records the gap and the risk
        // it carries.
    };

    // One tick, after the physics step - it judges where the step left the
    // player, as the ghost does. `torchLit` is Torch::State::lit > 0, which is the
    // light the original's marker looks for; `torchPx` is where that torch stands,
    // for the shot a wound sends at it; `completed` is isFinished.
    Turn Tick(entt::registry& registry, entt::entity player, bool torchLit, const glm::dvec2& torchPx,
              bool hasTorch, bool completed, float dt);

    bool Alive() const { return present && alive && !gone; }
};

// The level's dark_dragon_spawn and the four markers it reads by name. A level
// with no spawn gets an empty State, which is every level but one.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error);

} // namespace MagicPortals::DarkDragon
