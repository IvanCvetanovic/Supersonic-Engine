#pragma once

// Keys, keyholes and the locked doors they open - chapter 3's largest mechanism,
// and one the remake pairs by colour rather than by proximity.
//
// Three roles, and only one of them is solid. door_locked.ent is shape=1,
// static=1, a 30 x 126 collider: the thing the player cannot walk through.
// keyhole.ent has no collision block at all and key.ent has density 0 and custom
// data holding nothing but `color`. So a key is not a body in this port either -
// it is state with a position and an owner - and unlocking is the deletion of the
// door, which is the only part of the three the player can touch.
//
// What the original does, decoded:
//
// - PICKUP is a distance poll, not a contact. The key's init writes
//   squaredRange = scale(26)^2 and each frame scans getSurroundingEntities for
//   anything that isCharacter OR isMinion and does not shallLeave, taking the
//   first within that range as its owner. So a MINION can carry a key. The
//   remake's actors.json guesses 40 px and marks it _guess.
// - CARRYING re-finds the owner by id, trails it through an interpolator
//   (followUp, re-aimed every 20 ms and interpolated over 60), and snaps with
//   forceFollowUpPosition when it falls further than scale(40) behind. Nothing
//   in that path touches velocity, which is why the port does not build a body
//   and then fight gravity for it.
// - It DROPS when shallLeave(owner): the carrier is a dead character or a
//   destroyed minion. Here that is the owner ceasing to be a valid entity.
// - UNLOCKING reuses the SAME 26 px. Within it of a colour-matching keyhole the
//   key sets foundKeyhole on itself and `unlocked` on the keyhole - and from then
//   on it is spent, because the original's top-level branch sends a key with
//   foundKeyhole down the fly-in animation and never past the pickup path again.
// - The KEYHOLE then waits 1000 ms, fades over 500, and deletes BOTH its door and
//   itself. `unlocked` appears exactly twice in the whole binary - that write and
//   this read - so it is one-way and nothing ever re-locks a door.
//
// Pairing. Colour is an explicit metadata/color string on all three roles and is
// never absent in any of the 128 levels. The original finds the door with
// seekNeighbourEntity over a 3x3 block of buckets and falls back to a global
// seekEntity, so the bucket walk is an optimisation rather than a rule; the port
// takes the NEAREST colour-matching door in the level. Across all 41 pairs that
// is the same answer: they average 75 px apart, and level20b - the only level
// holding two pairs of one colour - puts each keyhole 57 px from its own door and
// 216 px or more from the other.
//
// NOT built, and recorded in data/keys.json: the fly-in animation the key plays
// once it is spent (a WaypointManager of timed waypoints with smoothEnd filters -
// presentation, in a renderer-free port), fixKeyAngle, the idle linearMotion bob,
// the three vanish entities, the earthquake, the pick and unlock sounds, and the
// red key's achievement.

#include "sim/Carry.hpp"
#include "sim/LevelBuilder.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Keys {

// The port's keys.json. range/leash/reaim/stride are the carry path's and are
// inherited from Carry::Rules, where a shock diamond reads the same four under
// its own numbers; the range is the one a key shares between being picked up and
// opening a keyhole. The last two are the keyhole's alone.
struct Rules : Carry::Rules {
    double fadeStartMs = 0.0; // an unlocked keyhole holds still this long
    double fadeMs = 0.0;      // and then fades away over this
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

// Its position, trail and owner are Carried's, so a key is still a thing with an
// atPx and an owner and reads of either are unchanged.
struct Key : Carry::Carried {
    std::string name;   // the node's name
    std::string colour; // metadata/color, which every one of them carries
    bool spent = false; // it has opened its keyhole and is done
};

struct Keyhole {
    std::string name;
    std::string colour;
    glm::dvec2 atPx{0.0, 0.0};
    std::string doorName;         // the nearest door of its colour
    entt::entity door = entt::null;
    bool unlocked = false;        // one-way: nothing in the original clears it
    double sinceUnlockMs = 0.0;
    double alpha = 1.0;           // what the fade has left
    bool gone = false;            // faded out, and its door deleted with it
};

struct State {
    Rules rules;
    std::vector<Key> keys;
    std::vector<Keyhole> keyholes;

    int picked = 0;   // keys taken up
    int unlocked = 0; // keyholes a key reached
    int opened = 0;   // doors deleted

    // One tick, after the physics step - a key follows where the step left its
    // owner, as the goals and hazards judge where it left the player.
    //
    // `carriers` is everything that can pick a key up: the player, and every
    // minion. Returns the door bodies it deleted, which the caller must pass
    // through Forget.
    std::vector<entt::entity> Tick(entt::registry& registry, const std::vector<entt::entity>& carriers, float dt);

    const Key* FindKey(const std::string& name) const;
    const Keyhole* FindKeyhole(const std::string& name) const;
};

// A level's keys, keyholes and locked doors, each keyhole paired with the nearest
// door of its own colour. False, with `error`, for any of the three carrying no
// colour - none does - or for a keyhole whose colour no door in the level has.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, const Rules& rules,
          State& out, std::string& error);

} // namespace MagicPortals::Keys
