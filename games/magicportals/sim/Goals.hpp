#pragma once

// A level's crystals and its exit: LevelRuntime's _on_trigger_entered, _collect
// and exit_is_open (level_runtime.gd:270-304), and what main.gd does with an exit
// (:161-165).
//
// - Only the player triggers them. Crates get pushed through crystals, and in the
//   remake they must neither collect them nor finish the level.
// - Both fire on entry, as an Area2D's body_entered does, not while the player is
//   inside. A crystal is collected once, the tick the player first overlaps it,
//   and the exit reports every time the player comes into it.
// - An entry completes the level only if the exit is open. It is open unless
//   portals.json's level.exit_requires_all_crystals asks for every crystal first.
//   So with that switch on, a player already standing in the exit when the last
//   crystal goes does not finish. It has to leave and come back in, as in the
//   remake.

#include "sim/Roles.hpp"
#include "sim/Trigger.hpp"
#include "sim/Tscn.hpp"

#include <string>
#include <vector>

#include <entt/entt.hpp>

namespace MagicPortals::Goals {

// The "level" object of the remake's portals.json. Its one switch is marked
// UNVERIFIED there, not _guess: nobody knows whether the original gates its exit
// on the crystals at all. So it is carried as data, and the tests run it both
// ways. A missing key is an error, where the remake falls back to false.
struct Rules {
    bool exitRequiresAllCrystals = false;
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

struct Crystal {
    std::string name;
    Trigger::Box box;
    bool collected = false;
};

struct State {
    std::vector<Crystal> crystals;
    Trigger::Box exit;
    Rules rules;
    int exitEntries = 0;        // how many times the player has come into the exit
    bool completed = false;     // an entry found the exit open
    bool playerInExit = false;  // as of the last tick, to find the next entry

    int Remaining() const;
    bool ExitOpen() const;

    // One tick, after the physics step, covering what the player has come into.
    // The remake does not define the order of two triggers in one frame. Crystals
    // go first here, so an entry on the tick the last crystal goes finds the exit
    // open.
    void Tick(entt::registry& registry, entt::entity player);

    const Crystal* FindCrystal(const std::string& name) const;
};

// Every collectible and the exit_door of a level. False, with `error`, when there
// is not exactly one exit, or when a crystal carries a `time`. The remake's timed
// crystals expire, those are not ported, and level30 has none.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error);

} // namespace MagicPortals::Goals
