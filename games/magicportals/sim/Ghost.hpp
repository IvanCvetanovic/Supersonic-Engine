#pragma once

// Chapter 3's boss: the ghost of level 3-32, and the fight the fire diamond
// exists for.
//
// The game's SECOND boss, and it shares nothing with the first. The beholder
// (Boss.hpp) glides, throws rolling stones, is hurt by a stone rising through it,
// fires rings of spikes and raises a button when it dies. The ghost flies a
// waypoint path, shoots the player, is hurt by FIRE, summons a minion when hurt,
// and drops a KEY when it dies. Nothing in Boss::State - no rocks, no spikes, no
// button - serves any of that, so this is its own module and test_mp_boss's 126
// checks are left alone.
//
// FIVE states, and their numbers are the callback's own first six instructions:
// APPEARING 0, SHOOTING 1, RAGE_MODE 2, DAMAGE 3, DEATH 4, GHOST_RADIUS 64. The
// init writes hp 3.
//
// HOW IT IS HURT, which is why this step follows the fire diamond. The SHOOTING
// arm ends with isBurned(this): burned, it goes to DAMAGE, takes hp += -1, calls
// healBurn(this) and sounds. Nothing else in level31b can burn anything, so the
// fire diamond IS the weapon - the fireballs a converted portal shot becomes. A
// port that built this boss without the fire diamond would have built an
// invulnerable one.
//
// `burned` here is set from OUTSIDE, by the same Game block that lights a crate
// and detonates a bomb when a fireball reaches one. The ghost is not a scene
// entity and so is never in Fire::Burnable, which Fire::Find fills from nodes;
// rather than force a mid-run insert into Fire, the flag is this module's and
// Tick reads and clears it exactly as healBurn does.
//
// WHERE IT STARTS, and this is a consequence rather than a choice. In the
// original a ghost.ent is created by a dying ghost_minion and flies TO ghost_pos;
// the port does not build that transformation - minions.json records that a
// marker's entityName is never honoured - so the ghost begins at ghost_pos. Its
// four appear waypoints are currentPos, the quarter-point, and ghost_pos twice,
// which with currentPos == ghost_pos all coincide. So the appearance keeps its
// DURATION (1200 + 2500 + 50 + 0 ms) and loses its path, and what the waypoints
// drove besides position - the colour, the alpha, the swelling scale - is
// presentation this port has no renderer for anyway.
//
// WHAT IT REPORTS RATHER THAN DOES. Tick returns a Turn: summon a minion here,
// drop a key here, put a fire diamond back there. Minions owns minion bodies,
// Keys owns keys and Diamonds owns diamonds, and each walks its own list every
// frame - so Game funnels, as it does for every other module that reaches across.
//
// Not built, and recorded in data/ghost.json: the pictures and every sound, the
// ghost_minion's transformation into a ghost.ent, the barrel bomb restock (a
// mid-run Fire::Bomb, which is the step minions.json already names), the death
// blast's 2.0, and the scattered explosions.

#include "sim/Minions.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Ghost {

// BEHOLDER-style state constants, but the ghost's own five.
enum class Phase : int { Appearing = 0, Shooting = 1, Rage = 2, Damage = 3, Death = 4 };

// The port's ghost.json.
struct Rules {
    std::string anchorName; // ghost_pos: the boss_spawn node, matched by NAME
    int maxHp = 0;
    double radiusPx = 0.0;

    double appearMs = 0.0;      // the four legs summed; the path is degenerate here
    double seedElapsedMs = 0.0; // SHOOTING starts with its clock already at the interval

    double intervalMs = 0.0; // between shots, and between restocks
    glm::dvec2 muzzlePx{0.0};

    double shotSpeedPxS = 0.0;
    double shotHitPx = 0.0;

    double toRageMs = 0.0;      // DAMAGE holds this long before RAGE_MODE
    double summonWindowMs = 0.0; // and RAGE_MODE summons inside this

    std::string waypointPrefix;  // wayA
    std::string summonAnchorName; // minion_spawn_point, which carries no role
    std::string redLineName;      // red_line: the x the player must be past
    std::string diamondSpawnName; // fire_diamond_spawn

    double burstMs = 0.0; // DEATH waits this long, then drops the key
    std::string keyColour;
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

// Whether a boss_spawn node is THIS boss. Unlike Boss::Plays - which reads an
// adder's metadata/entity - ghost_pos carries no `entity` field at all and the
// entity NAME is the whole of it. Mirroring Boss::Plays literally would answer
// false for every level in the game.
bool Plays(const Rules& rules, const Tscn::Node& node);

// One of the ghost's shots: enemy_shoot.ent, which only ever kills the player.
struct Shot {
    glm::dvec2 atPx{0.0};
    glm::dvec2 velocityPx{0.0};
    double flownPx = 0.0;
    double rangePx = 0.0;
};

// The patrol the summoned escort walks is Minions::Waypoint and not a type of
// this module's own. It is an ORDINARY minion patrol - the original's rage arm
// calls the same spawnMinion a marker does, with a prefix a marker could have
// carried - so a second identical struct would be two names for one thing, and
// Minions::Summon would need a conversion to say so.

struct State {
    Rules rules;

    bool present = false; // the level places a ghost_pos
    std::string name;     // the node's name
    glm::dvec2 atPx{0.0}; // where it is, which is where it was placed

    bool hasRedLine = false;
    double redLineX = 0.0;
    bool hasSummonAnchor = false;
    glm::dvec2 summonPx{0.0};
    bool hasDiamondSpawn = false;
    glm::dvec2 diamondSpawnPx{0.0};
    std::vector<Minions::Waypoint> patrol; // what the summoned escort walks

    Phase phase = Phase::Appearing;
    int hp = 0;
    double elapsedMs = 0.0;
    bool burned = false; // set by Game when a fireball reaches it; cleared here
    bool summonedThisRage = false;
    bool gone = false;

    int shotsFired = 0;
    int hits = 0;    // times it was burned
    int summons = 0; // minions it called
    std::vector<Shot> shots;

    // Reported, not acted on: Game::AfterStep folds this into Hazards, as the
    // boss's, the carrancas', the fire's and the rings' are.
    bool playerKilled = false;
    std::string killedBy;

    // What a tick asks Game to do on its behalf.
    struct Turn {
        bool summon = false; // a minion, at summonPx, on the patrol above
        bool droppedKey = false;
        glm::dvec2 keyPx{0.0};
        bool restockDiamond = false; // a fire diamond back at its spawn
    };

    // One tick, after the physics step. `minionsStanding` is how many minions the
    // level still has - RAGE_MODE waits on its escort being killed.
    Turn Tick(entt::registry& registry, entt::entity player, std::size_t minionsStanding, float dt);

    bool Alive() const { return present && !gone; }
};

// The level's ghost, the markers it reads by name, and the patrol its escort
// walks. A level with no ghost_pos gets an empty State, which is every level but
// one. False, with `error`, for a ghost_pos with no position.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error);

} // namespace MagicPortals::Ghost
