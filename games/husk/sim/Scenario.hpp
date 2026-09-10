#pragma once

// Scripted scenarios: the `.scn` files in games/husk/golden.
//
// The Rust oracle and the C++ suites read the same files, so a scenario has
// one source of truth. A file is header lines, setup lines, then `<tick> ...`
// lines applied, in file order, before that tick runs:
//
//   seed 7                        ticks 10000              difficulty hard
//
//   setup m0 | setup macro        a built-in scenario
//   mission heartwood             a shipped mission, from its RON
//   install missions/raid.ron     a scenario's own MissionDef (golden-relative)
//   map 60                        an empty map of that half-size, then any of
//   plateau x0 y0 x1 y1 level     ramp x0 y0 x1 y1     obstacle x0 y0 x1 y1
//   poly level x,y x,y ...
//   squad team=0 x=.. y=.. comp=stalker:10,golem:6
//   unit <kind> <team> <x> <y>    building <kind> <team> <x> <y> [site]
//   source <kind> <x> <y> [husk]  item <kind> <x> <y>      hero <x> <y>
//   essence <v>  anima <v>  affinity <Name> <v>  researched <upgrade> <level>
//   hp <id> <v>  herolevel <id> <level> <xp> <points>
//   inventory <id> <slot> <item>  queue <building id> <unit kind>
//   hook essence_drip             tests/mission.rs's +1 essence per tick
//
//   <tick> point units=0-29 attack=1 x=38.0 y=38.0 queued=0     (any order)
//   <tick> <any setup verb above>                              (a world edit)
//   <tick> snapshot               save, then restore into a fresh world
//
// Catalog names use `_` for a space (`riot_shield`), and a kind may be `@N`
// for a raw id. `#` starts a comment anywhere on a line.

#include "World.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace husk {

struct Scenario {
    uint64_t seed = kDefaultSeed;
    uint64_t ticks = 1000;
    Difficulty difficulty = Difficulty::Normal;
    std::filesystem::path dir; // where `install` paths are relative to
    std::vector<std::vector<std::string>> setup;
    std::vector<std::pair<uint64_t, std::vector<std::string>>> script;
};

// Throws std::runtime_error on a file it cannot read or a line it cannot parse.
Scenario parseScenario(const std::filesystem::path& path);

// A world with the scenario's seed, difficulty and setup applied.
std::unique_ptr<World> startScenario(const Scenario& s, std::shared_ptr<const Catalogs> catalogs);

// Everything scripted for `tick`, in file order: orders are queued, world edits
// land at once, and a snapshot REPLACES `w`. Call before step().
void applyScriptTick(std::unique_ptr<World>& w, const Scenario& s, uint64_t tick);

// One order line.
OrderMsg scenarioOrder(const World& w, const std::vector<std::string>& words);

// The directory holding the scenarios and their goldens (games/husk/golden).
std::filesystem::path goldenDir();

} // namespace husk
