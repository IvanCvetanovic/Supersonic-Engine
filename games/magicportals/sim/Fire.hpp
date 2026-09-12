#pragma once

// Fire agents, burning, and barrel bombs - the three that chapter 2 leaves inert,
// built together because the original writes them as one mechanism: a flag on an
// entity, set by whatever reaches it, read back by that entity's own callback.
//
// Decoded from the original's AngelScript rather than taken from the remake, and
// the two differ in every number that matters. Byte offsets into android_game.bin
// are given so the reading can be checked; no disassembly is reproduced here.
//
// WHAT REACHES WHAT
//
// - A fire agent (role fire_source, fire_agent.png) gathers its own spatial
//   bucket and the four beside it and tests, per entity,
//       squaredDistance(other, self) < size.x * size.x
//   so size.x is a RADIUS and the test is CENTRE TO CENTRE, not shape against
//   shape. GetSize() is the sprite's frame times its scale - the engine hands it
//   to IsPointOnSprite as the sprite's extent (ETHBucketManager.cpp:289) and to
//   ComputeAbsoluteOrigin as its origin (ETHScene.cpp:375), and GetCollisionBox
//   is a separate method - so for fire_agent.png at 32 x 32, SpriteCut 1 x 1 and
//   Scale 1.0 the reach is 32. The remake's hazards.json reads the entity's
//   38 x 38 <Collision> instead and halves it to 24, marked _guess; that box is
//   not what GetSize returns, and the .ent's shape is 0 - no body at all.
//   (ETHCallback_fire_agent, bytes 350254..351485.)
// - It then runs FOUR INDEPENDENT TESTS in this order, not a chain of else:
//   explosive requests a blast, burnable-and-not-yet-burned burns, a character
//   dies and is given flames, and projectile.ent is put out. A minion is burnable
//   AND a character, so it takes the second and the third both.
// - A blast (explode, MiscCallbacks, bytes 342023..342895) is the other shape: an
//   EntityGrabber of radius 80 over the entity's REAL collider, circle against
//   circle or sphere against box, and it skips anything with no physics body at
//   all (EntityGrabber.angelscript, bytes 23924..24990). Its filter is
//   isBreakableOrExplosiveOrBurnable, which does NOT include characters, and no
//   main_char placement carries any of those three flags - so a bomb cannot touch
//   the player, while fire kills them. That asymmetry is the original's, and
//   test_mp_fire pins it in both directions.
// - The blast also checks LINE OF SIGHT: it casts from the blast to each entity
//   and drops it unless the first thing hit is that entity, so a wall shields
//   what stands behind it.
//
// WHAT IS NOT BUILT, SAID HERE RATHER THAN LEFT TO BE NOTICED
//
// - The fourth branch, burnProjectile, puts out a flying projectile.ent. The port
//   has no such body: its shot is a SEGMENT that Portals resolves within a tick
//   (Shot.hpp), not an entity a bucket sweep could find, so there is nothing here
//   for a fire agent to reach. The consequence is real - a shot fired THROUGH a
//   flame opens its portal in the port where the original would have put it out -
//   and the fix belongs in Shot's segment test, not in this file.
// - A blast also sets `destroy` on every breakable it grabs. A breakable wall is
//   Demolish's body to take away, and one body with two owners is how a level ends
//   up half broken, so it is left out. Nine chapter-2 levels place both a bomb and
//   a breakable wall (11a, 12a, 13a, 15a, 16a, 18a, 20a, 23a, 28a), which makes
//   this a follow-up rather than a footnote.
//
// WHAT BURNING DOES, AND TO WHAT
//
// burn() only sets `burned` and plays a sound. The fade to black, and the removal
// that makes burning a PUZZLE rather than a decoration, live in manageBurnable
// (bytes 420664..421145), which has exactly ONE caller: crateCallback. So only
// crates fade over BURN_TIME and are taken away; a shock_agent carries the
// burnable flag too and merely holds `burned` when lit. Building "everything
// burnable dies" would delete level 2-12's shock agent and change its puzzle, so
// fire.json lists the names whose callback is crateCallback and nothing else
// fades.
//
// THE FLAG AND THE POLL ARE DELIBERATELY A TICK APART
//
// Fire and a blast do not detonate a bomb; they set `explode` on it. The bomb's
// own per-frame callback (ETHCallback_barrel_bomb, bytes 339851..340212) reads
// that flag, removes itself, and only then blasts. A chain of bombs therefore
// ripples one tick at a time rather than collapsing into a single frame, and the
// port keeps that split rather than recursing.
//
// WHERE IT RUNS, AND WHO CLEARS UP
//
// After the step, with the hazards, and before the portals - what it removes is
// handed back so Game::AfterStep can pass it through Forget, because a crate is
// teleportable and may be in portals.travellers when it burns. Killing the player
// is reported here and folded into Hazards by Game::AfterStep, as the boss's and
// the carrancas' kills are, so there is one death in the port and not three.

#include "sim/LevelBuilder.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Fire {

// The port's fire.json.
struct Rules {
    double reachPx = 0.0;   // a fire agent's GetSize().x, centre to centre
    double burnMs = 0.0;    // BURN_TIME: how long a crate burns before it goes
    double blastPx = 0.0;   // explode's EntityGrabber radius, against real colliders
    // The entity names whose callback is crateCallback, and so the only ones that
    // fade and are removed when burned. Everything else flagged burnable just
    // holds the flag.
    std::vector<std::string> fadeNames;
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

// A fire_source placement. It never moves and has no body, so its position is all
// there is of it.
struct Agent {
    std::string name;
    glm::dvec2 atPx{0.0};
};

// Anything carrying metadata/burnable.
struct Burnable {
    std::string name;
    entt::entity body = entt::null;
    bool fades = false;   // its name is in Rules::fadeNames
    bool burned = false;  // the original's `burned`
    double burningMs = 0.0;
    bool gone = false;
};

// Anything carrying metadata/explosive.
struct Bomb {
    std::string name;
    entt::entity body = entt::null;
    bool requested = false; // the original's `explode` flag, set this tick
    bool blown = false;
};

struct State {
    Rules rules;
    std::vector<Agent> agents;
    std::vector<Burnable> burnables;
    std::vector<Bomb> bombs;

    // Reported, not acted on: Game::AfterStep folds this into Hazards, as it does
    // the boss's and the carrancas'.
    bool playerKilled = false;
    std::string killedBy;

    int lit = 0;      // burnables set alight
    int blasts = 0;   // bombs that went off

    // One tick, after the physics step. Returns the bodies it took away, which
    // the caller must pass through Forget: a burnt crate can be a portal
    // traveller, and a blasted one a demolisher's target.
    std::vector<entt::entity> Tick(entt::registry& registry, entt::entity player, float dt);

    const Burnable* FindBurnable(const std::string& name) const;
    const Bomb* FindBomb(const std::string& name) const;
};

// A level's fire agents, burnables and bombs, each with the body LevelBuilder
// built for it. A flagged entity with no body built is passed over, which is what
// a level started without its statics leaves.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, const Rules& rules,
          State& out, std::string& error);

} // namespace MagicPortals::Fire
