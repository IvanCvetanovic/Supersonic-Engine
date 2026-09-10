#pragma once

// Scripted scenarios: the `.scn` files in games/husk/golden.
//
// The Rust oracle and the C++ suites read the same files, so a scenario's
// orders have one source of truth. A file is a few setup lines, then
// `<tick> <order> key=value ...` lines pushed onto the order queue before that
// tick runs - the shape of tests/determinism.rs's `script()`:
//
//   seed 7
//   ticks 10000
//   setup m0                      (or: setup macro / mission heartwood /
//                                  squad team=0 x=.. y=.. comp=stalker:10,golem:6)
//   0 point units=0-29 attack=1 x=38.0 y=38.0 queued=0
//
// Catalog names in orders use `_` for a space (`kind=riot_shield`).

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
    std::vector<std::vector<std::string>> setup;
    std::vector<std::pair<uint64_t, std::vector<std::string>>> script;
};

// Throws std::runtime_error on a file it cannot read or a line it cannot parse.
Scenario parseScenario(const std::filesystem::path& path);

// A world with the scenario's seed, difficulty and setup applied.
std::unique_ptr<World> startScenario(const Scenario& s, std::shared_ptr<const Catalogs> catalogs);

// One script line's order.
OrderMsg scenarioOrder(const World& w, const std::vector<std::string>& words);

// Push every order scripted for `tick`, in file order. Call before step().
void pushScriptedOrders(World& w, const Scenario& s, uint64_t tick);

// The directory holding the scenarios and their goldens (games/husk/golden).
std::filesystem::path goldenDir();

} // namespace husk
