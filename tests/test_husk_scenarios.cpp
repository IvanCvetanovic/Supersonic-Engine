// HUSK's port, whole ticks: every scripted scenario in games/husk/golden runs
// here and in the Rust original, and the SimHash after every tick must agree
// bit for bit. The hash covers every unit, building, source and item, the
// economy, the RNG stream and the mission state, so one tick in agreement is
// the whole sim in agreement.
//
// The scenarios mirror the setups of HUSK's own 96 Rust tests - the leash, the
// taunt, the chain arc, the balance doctrine, loot, research, the tower, the
// hero kit, missions and their triggers, terrain, and saves restored mid-run -
// so each of those becomes a bit-exact comparison rather than a rephrased
// assertion. Adding a scenario is adding a .scn file and its golden: this
// suite runs whatever it finds.
//
// The goldens come from games/husk/oracle (see test_husk_foundation.cpp):
//
//   for s in ../golden/*.scn; do target/release/husk-oracle run $s > ${s%.scn}.hashes; done
//
// When one diverges, this suite prints the port's per-domain hashes and
// entities at the first bad tick, and
//
//   husk-oracle run ../golden/<name>.scn --domains --entities <tick>
//
// prints the Rust side's for the same tick. The first differing domain names
// the system to read.

#include "TestHarness.hpp"

#include "sim/Scenario.hpp"
#include "sim/World.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace husk;

namespace {

std::vector<std::string> readLines(const std::filesystem::path& p) {
    std::vector<std::string> out;
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

std::string hex16(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

struct Run {
    size_t matched = 0;
    size_t expected = 0;
    std::string divergence;
    std::unique_ptr<World> world;
};

Run runAgainstGolden(const std::string& name) {
    Run run;
    const Scenario s = parseScenario(goldenDir() / (name + ".scn"));
    const std::vector<std::string> golden = readLines(goldenDir() / (name + ".hashes"));
    run.expected = golden.size();
    run.world = startScenario(s, sharedCatalogs());
    for (uint64_t t = 0; t < s.ticks; ++t) {
        applyScriptTick(run.world, s, t);
        World& w = *run.world;
        step(w);
        const std::string line = std::to_string(w.tick) + " " + hex16(w.hash);
        if (t >= golden.size() || golden[t] != line) {
            const auto d = domainHashes(w);
            run.divergence = name + " diverged at tick " + std::to_string(w.tick) + ": rust '" +
                             (t < golden.size() ? golden[t] : std::string("<end>")) + "', port '" + line +
                             "'\n    port domains (state units buildings sources items): " + hex16(d[0]) + " " +
                             hex16(d[1]) + " " + hex16(d[2]) + " " + hex16(d[3]) + " " + hex16(d[4]);
            std::printf("%s\n%s", run.divergence.c_str(), dumpEntities(w).c_str());
            break;
        }
        ++run.matched;
    }
    return run;
}

void checkScenario(const std::string& name) {
    // A scenario the port cannot even start is one failure, not the end of
    // the suite: the rest still say what they say.
    Run run;
    try {
        run = runAgainstGolden(name);
    } catch (const std::exception& e) {
        CHECK_MSG(false, name + " threw: " + e.what());
        return;
    }
    CHECK_MSG(run.divergence.empty(), run.divergence);
    CHECK_MSG(run.expected > 0 && run.matched == run.expected,
              name + ": " + std::to_string(run.matched) + " of " + std::to_string(run.expected) + " ticks agree");
}

// THE GATE (docs/planning/2026-09-10-husk-port.md): tests/determinism.rs's
// scripted M0 march, tick for tick over 10,000 ticks. Then its
// scripted_orders_complete_and_units_gather, asked of the port's own state.
void testTheM0MarchMatchesTickForTick() {
    const Run run = runAgainstGolden("m0");
    CHECK_MSG(run.divergence.empty(), run.divergence);
    CHECK_EQ(run.matched, static_cast<size_t>(10000));

    const World& w = *run.world;
    size_t units = 0;
    size_t pending = 0;
    float maxDist = 0.0f;
    for (const auto& [id, e] : w.entities) {
        if (!e.isUnit()) continue;
        ++units;
        pending += e.orders.size();
        maxDist = fmaxr(maxDist, e.pos.cur.distance(Vec2(30.0f, -35.0f)));
    }
    CHECK_EQ(units, static_cast<size_t>(kM0UnitCount));
    CHECK_MSG(pending == 0, "orders still pending after 10,000 ticks");
    CHECK_MSG(maxDist < 12.0f, "units did not gather at the last waypoint (max " + std::to_string(maxDist) + ")");
}

// Same seed, same hash; another seed, another hash - determinism.rs's first
// two cases, which the golden comparison implies but does not state.
void testTheSeedAndOnlyTheSeedDecides() {
    auto run = [](uint64_t seed) {
        World w(sharedCatalogs(), seed);
        spawnM0Scenario(w);
        std::vector<uint32_t> all;
        for (uint32_t i = 0; i < kM0UnitCount; ++i) all.push_back(i);
        w.orderQueue.push_back(OrderMsg::point(all, true, Vec2(38.0f, 38.0f), false));
        for (int t = 0; t < 1000; ++t) step(w);
        return w.hash;
    };
    CHECK(run(7) == run(7));
    CHECK(run(7) != run(8));
}

std::vector<std::string> allScenarios() {
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(goldenDir())) {
        if (entry.path().extension() == ".scn") names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

void runTests() {
    testTheM0MarchMatchesTickForTick();
    testTheSeedAndOnlyTheSeedDecides();

    const std::vector<std::string> names = allScenarios();
    // A floor, so a golden directory that went missing is a failure rather
    // than a suite that quietly checked nothing.
    CHECK_MSG(names.size() >= 50, "only " + std::to_string(names.size()) + " scenarios found");
    for (const std::string& name : names) {
        if (name != "m0") checkScenario(name); // the gate ran above
    }
}

TEST_MAIN("test_husk_scenarios", 100)
