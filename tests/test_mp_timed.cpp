// Timed crystals: the inline `crystal` with a `time`, which goes that long after
// the level starts (the remake's TimedCollectible, behaviours.gd:209-230).
//
// What the files say is pinned: which crystals are timed and for how long,
// chapter 1's eight of them. What happens is judged by threshold, a few ticks
// either side of the time, since a tick is 1/60 s. The exit switch the remake
// marks UNVERIFIED is run both ways, because a gone crystal counts against it.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Game.hpp"
#include "sim/Goals.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace MagicPortals;
using Supersonic::RigidBodyComponent;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

std::filesystem::path Prisms() {
    return std::filesystem::temp_directory_path() / "supersonic-test-mp-timed";
}

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData, Prisms(), run.data, error) &&
                    Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + ": " + error);
    return ok;
}

void Tick(Run& run) {
    Game::Tick(run.data, run.registry, run.level, 0.0f, kStep);
}

void PutPlayerOn(Run& run, const Trigger::Box& box) {
    auto& transform = run.registry.get<TransformComponent>(run.level.player);
    transform.position = glm::vec3(box.centre.x, box.centre.y, transform.position.z);
    run.registry.get<RigidBodyComponent>(run.level.player).velocity = glm::vec3(0.0f);
}

// ---- What the levels say ------------------------------------------------------

void ChapterOnesEightTimedCrystals() {
    // Read off the level files with grep: `metadata/time` on an inline crystal,
    // in each file's order.
    const std::pair<const char*, std::vector<double>> expected[] = {
        {"level14", {12.0}},
        {"level23", {5.0, 12.0, 6.0}},
        {"level24", {11.5, 11.1}},
        {"level26", {10.0}},
        {"level28", {9.0}},
    };
    int total = 0;
    for (const auto& [name, lives] : expected) {
        Game::Data data;
        Goals::State goals;
        std::string error;
        const bool ok = Game::LoadData(kLevels + "/" + name + ".tscn", kData, Prisms(), data, error) &&
                        Goals::Find(data.scene, data.roles, data.goals, goals, error);
        CHECK_MSG(ok, std::string(name) + ": " + error);
        if (!ok) continue;
        std::vector<double> found;
        for (const Goals::Crystal& crystal : goals.crystals) {
            if (crystal.timed) found.push_back(crystal.lifeS);
            CHECK(crystal.timed ? crystal.leftS == crystal.lifeS : crystal.lifeS == 0.0);
        }
        CHECK_MSG(found == lives, std::string(name) + "'s timed crystals");
        total += static_cast<int>(found.size());
    }
    CHECK_EQ(total, 8);
}

// ---- Running out --------------------------------------------------------------

const Goals::Crystal* Level14sTimedCrystal(const Run& run) {
    const Goals::Crystal* crystal = run.level.goals.FindCrystal("crystal_861");
    CHECK_MSG(crystal != nullptr && crystal->timed && crystal->lifeS == 12.0, "level14's crystal_861, 12 s");
    return crystal;
}

void ItGoesAtItsTime() {
    Run run;
    if (!Begin("level14", run)) return;
    const Goals::Crystal* crystal = Level14sTimedCrystal(run);
    if (crystal == nullptr) return;
    int tick = 0;
    for (; tick < 714; ++tick) Tick(run); // 11.9 s
    CHECK_MSG(!crystal->expired, "still there at 11.9 s, with " + std::to_string(crystal->leftS) + " s left");
    for (; tick < 723; ++tick) Tick(run); // 12.05 s
    CHECK_MSG(crystal->expired, "gone by 12.05 s");
    // Gone, it cannot be collected.
    PutPlayerOn(run, crystal->box);
    Tick(run);
    CHECK(!crystal->collected);
}

void ThePlayerCollectsItFirst() {
    Run run;
    if (!Begin("level14", run)) return;
    const Goals::Crystal* crystal = Level14sTimedCrystal(run);
    if (crystal == nullptr) return;
    for (int tick = 0; tick < 60; ++tick) Tick(run);
    PutPlayerOn(run, crystal->box);
    Tick(run);
    CHECK_MSG(crystal->collected, "collected at 1 s");
    // Collected, it does not go.
    for (int tick = 0; tick < 720; ++tick) Tick(run);
    CHECK(crystal->collected);
    CHECK(!crystal->expired);
}

void AGoneCrystalStillCountsAgainstTheExit() {
    // With the UNVERIFIED switch off, the exit opens anyway. With it on, a
    // crystal that went can never be collected, so the exit stays shut: the
    // remake's crystals_remaining never drops for one. The switch is portals.json's,
    // so this is what it would mean, not a claim about the original.
    for (const bool gated : {false, true}) {
        Run run;
        if (!Begin("level14", run)) return;
        run.level.goals.rules.exitRequiresAllCrystals = gated;
        for (Goals::Crystal& crystal : run.level.goals.crystals) {
            if (!crystal.timed) crystal.collected = true;
        }
        for (int tick = 0; tick < 723; ++tick) Tick(run);
        const Goals::Crystal* crystal = run.level.goals.FindCrystal("crystal_861");
        CHECK(crystal != nullptr && crystal->expired);
        CHECK_EQ(run.level.goals.Remaining(), 1);
        CHECK_MSG(run.level.goals.ExitOpen() == !gated,
                  std::string("exit_requires_all_crystals ") + (gated ? "on" : "off"));
    }
}

void runTests() {
    ChapterOnesEightTimedCrystals();
    ItGoesAtItsTime();
    ThePlayerCollectsItFirst();
    AGoneCrystalStillCountsAgainstTheExit();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_timed: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_timed", 30);
}
