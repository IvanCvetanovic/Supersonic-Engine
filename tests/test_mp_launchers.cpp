// Launchers: what they throw, when, and what takes it back.
//
// Pinned exactly, because the files say it: chapter 1's three launchers, each
// with what it throws, its stride and its cull box, and the despawners under
// them. launchers.json's first delay is a guess, carried and not pinned: the
// first throw is judged against whatever it holds. What happens is judged on
// the tick:
//   - a thrown stone is a body at its launcher, and at once a stone that breaks
//     walls and a traveller that goes through portals;
//   - one comes every stride;
//   - one that leaves the cull box, or touches a destroyier, is taken back, and
//     nothing keeps hold of it;
//   - the walls of levels 23 and 24 are broken by a thrown stone routed through
//     a portal pair.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Game.hpp"
#include "sim/Launchers.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;
using Supersonic::RigidBodyComponent;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-launchers", run.data,
                                   error) &&
                    Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + ": " + error);
    return ok;
}

void Tick(Run& run, float direction = 0.0f) { Game::Tick(run.data, run.registry, run.level, direction, kStep); }

void Put(Run& run, entt::entity body, const glm::dvec2& atPx) {
    auto& transform = run.registry.get<TransformComponent>(body);
    const glm::vec3 at = Units::ToWorld(atPx.x, atPx.y);
    transform.position = glm::vec3(at.x, at.y, transform.position.z);
    run.registry.get<RigidBodyComponent>(body).velocity = glm::vec3(0.0f);
}

glm::dvec2 PxOf(const Run& run, entt::entity body) {
    return Units::ToPixels(run.registry.get<TransformComponent>(body).position);
}

std::string Px(const glm::dvec2& at) {
    char text[48];
    std::snprintf(text, sizeof text, "(%.1f, %.1f)", at.x, at.y);
    return text;
}

bool Travels(const Run& run, entt::entity body) {
    const auto& travellers = run.level.portals.travellers;
    return std::find(travellers.begin(), travellers.end(), body) != travellers.end();
}

bool Demolishes(const Run& run, entt::entity body) {
    const auto& stones = run.level.demolish.stones;
    return std::any_of(stones.begin(), stones.end(), [body](const Demolish::Stone& s) { return s.body == body; });
}

// Ticks until the first launcher has thrown `count` in all, or `limit` ticks.
int TickUntilThrown(Run& run, int count, int limit) {
    int tick = 0;
    while (tick < limit && run.level.launchers.launchers.front().thrown < count) {
        Tick(run);
        ++tick;
    }
    return tick;
}

// ---- What the levels say ------------------------------------------------------

void ChapterOnesLaunchers() {
    // Read off the level files with grep: one entity_launcher in each of levels
    // 23, 24 and 25, every one throwing rolling_stone.ent, and the destroyiers.
    struct Expected {
        const char* level;
        glm::dvec2 at;
        double strideS;
        glm::dvec2 min;
        glm::dvec2 max;
        std::vector<const char*> despawners;
    };
    const Expected expected[] = {
        {"level23", {74.0, -42.0}, 1.5, {0.0, -128.0}, {512.0, 400.0}, {"destroyier_ent_1051", "destroyier_ent_1074"}},
        {"level24", {454.0, -42.0}, 1.5, {0.0, -128.0}, {512.0, 512.0}, {"destroyier_ent_1195"}},
        {"level25", {102.0, -42.0}, 2.5, {0.0, -128.0}, {512.0, 338.0}, {}},
    };
    for (const Expected& e : expected) {
        Run run;
        if (!Begin(e.level, run)) continue;
        const Launchers::State& launchers = run.level.launchers;
        CHECK_EQ(launchers.launchers.size(), std::size_t{1});
        if (launchers.launchers.size() != 1) continue;
        const Launchers::Launcher& launcher = launchers.launchers.front();
        CHECK_MSG(launcher.name == "entity_launcher_923", launcher.name);
        CHECK(launcher.entity == "rolling_stone.ent");
        CHECK(launcher.atPx == e.at);
        CHECK(std::fabs(launcher.strideS - e.strideS) < 1e-12);
        CHECK(launcher.minPx == e.min);
        CHECK(launcher.maxPx == e.max);
        // What launchers.json says rolling_stone.ent is, from its .ent.
        CHECK(launcher.throws.radiusPx == 30.0);
        CHECK(launcher.throws.demolisher);
        CHECK(launcher.throws.teleportable);
        CHECK_MSG(launcher.throws.sprite == "rolling_stone.png", "drawn as rolling_stone.ent is: " + launcher.throws.sprite);
        CHECK_EQ(launchers.despawners.size(), e.despawners.size());
        for (std::size_t i = 0; i < launchers.despawners.size() && i < e.despawners.size(); ++i) {
            CHECK_MSG(launchers.despawners[i].name == e.despawners[i], launchers.despawners[i].name);
        }
        CHECK(launchers.live.empty());
    }
}

// ---- Throwing -----------------------------------------------------------------

void AThrowIsAStoneAtItsLauncher() {
    Run run;
    if (!Begin("level23", run)) return;
    const Launchers::Launcher& launcher = run.level.launchers.launchers.front();
    // When the first comes is launchers.json's guess, read rather than pinned.
    const int expected = static_cast<int>(std::ceil(run.data.launchers.firstThrowStrides * launcher.strideS / kStep));
    const int tick = TickUntilThrown(run, 1, 600);
    std::printf("  level23's first stone after %d tick(s); launchers.json says %d\n", tick, expected);
    CHECK_MSG(std::abs(tick - expected) <= 1, "thrown on tick " + std::to_string(tick));
    CHECK_EQ(run.level.launchers.live.size(), std::size_t{1});
    if (run.level.launchers.live.empty()) return;
    const Launchers::Thrown& stone = run.level.launchers.live.front();
    CHECK_MSG(stone.name == "entity_launcher_923#1", stone.name);
    // One step of falling since it was put at the launcher.
    CHECK_MSG(glm::distance(PxOf(run, stone.body), launcher.atPx) < 2.0, "it is at " + Px(PxOf(run, stone.body)));
    CHECK(run.registry.all_of<RigidBodyComponent>(stone.body));
    CHECK(!run.registry.get<RigidBodyComponent>(stone.body).isKinematic);
    CHECK(Travels(run, stone.body));
    CHECK(Demolishes(run, stone.body));

    // And one every stride after.
    const int stride = static_cast<int>(std::lround(launcher.strideS / kStep));
    const int next = TickUntilThrown(run, 2, 600);
    CHECK_MSG(std::abs(next - stride) <= 1, "the second came " + std::to_string(next) + " ticks later");
    const int third = TickUntilThrown(run, 3, 600);
    CHECK_MSG(std::abs(third - stride) <= 1, "the third came " + std::to_string(third) + " ticks later");
}

// ---- Taking back --------------------------------------------------------------

void WhatFallsOutIsTakenBack() {
    // level23's launcher drops its stones down the open left side, over
    // destroyier_ent_1074: each is gone within a stride, and nothing keeps it.
    Run run;
    if (!Begin("level23", run)) return;
    TickUntilThrown(run, 1, 600);
    if (run.level.launchers.live.empty()) return;
    const entt::entity first = run.level.launchers.live.front().body;
    int tick = 0;
    while (tick < 180 && run.registry.valid(first)) {
        Tick(run);
        ++tick;
    }
    CHECK_MSG(!run.registry.valid(first), "the first stone is still there after " + std::to_string(tick) + " ticks");
    CHECK_EQ(run.level.launchers.removed, 1);
    CHECK(!Travels(run, first));
    CHECK(!Demolishes(run, first));
    CHECK(std::none_of(run.level.launchers.live.begin(), run.level.launchers.live.end(),
                       [first](const Launchers::Thrown& t) { return t.body == first; }));
    // A minute on, the level holds no more than a stride's worth.
    for (int more = 0; more < 3600; ++more) Tick(run);
    std::printf("  level23 after a minute: %d thrown, %d taken back, %zu live\n",
                run.level.launchers.launchers.front().thrown, run.level.launchers.removed,
                run.level.launchers.live.size());
    CHECK(run.level.launchers.live.size() <= 2);
    CHECK_EQ(run.level.launchers.removed + static_cast<int>(run.level.launchers.live.size()),
             run.level.launchers.launchers.front().thrown);
}

void ADestroyierTakesAStoneBack() {
    // Put down in destroyier_ent_1074's box, which lies inside the cull box: it
    // goes on that tick. With the despawners taken away it stays, so it was the
    // destroyier.
    for (const bool withDespawners : {true, false}) {
        Run run;
        if (!Begin("level23", run)) return;
        TickUntilThrown(run, 1, 600);
        if (run.level.launchers.live.empty()) return;
        if (!withDespawners) run.level.launchers.despawners.clear();
        const entt::entity stone = run.level.launchers.live.front().body;
        Put(run, stone, glm::dvec2(64.0, 380.0));
        Tick(run);
        CHECK_MSG(run.registry.valid(stone) != withDespawners,
                  std::string(withDespawners ? "with" : "without") + " the despawners, the stone " +
                      (run.registry.valid(stone) ? "stays" : "goes"));
    }
}

void OutOfTheCullBoxIsTakenBack() {
    Run run;
    if (!Begin("level23", run)) return;
    TickUntilThrown(run, 1, 600);
    if (run.level.launchers.live.empty()) return;
    run.level.launchers.despawners.clear();
    const entt::entity stone = run.level.launchers.live.front().body;
    // Across the box's bottom edge, y = 400, where the level is open: just inside
    // it stays, just past it it goes. (The right edge, x = 512, is the face of
    // collision_128x768_ent_1134, which would push a stone put past it back in.)
    Put(run, stone, glm::dvec2(250.0, 390.0));
    Tick(run);
    CHECK_MSG(run.registry.valid(stone), "inside the box the stone went");
    if (!run.registry.valid(stone)) return;
    Put(run, stone, glm::dvec2(250.0, 406.0));
    Tick(run);
    // The message is built whether or not the check passes, so it reads the
    // stone only while it is there.
    const bool gone = !run.registry.valid(stone);
    CHECK_MSG(gone, gone ? std::string() : "past the box the stone is at " + Px(PxOf(run, stone)));
}

// ---- Through a portal pair, onto a wall -------------------------------------

void AThrownStoneBreaksTheWall(const char* levelName, const char* wallName, const glm::dvec2& inPx,
                               const glm::dvec2& outPx) {
    // One portal in the launcher's drop, one over the wall: the first stone falls
    // in, comes out still falling, and lands on the wall.
    Run run;
    if (!Begin(levelName, run)) return;
    CHECK(run.level.portals.TryPlace(inPx));
    CHECK(run.level.portals.TryPlace(outPx));
    int tick = 0;
    std::string trace;
    for (; tick < 600 && run.level.demolish.Broken() == 0; ++tick) {
        Tick(run);
        if (!run.level.launchers.live.empty() && tick % 10 == 0) {
            trace += " " + std::to_string(tick) + ":" + Px(PxOf(run, run.level.launchers.live.front().body));
        }
    }
    const Demolish::Breakable* wall = run.level.demolish.FindBreakable(wallName);
    CHECK(wall != nullptr);
    if (wall == nullptr) return;
    std::printf("  %s: %s %s after %.2f s, %d traversal(s)\n", levelName, wallName,
                wall->broken ? ("broken by " + wall->brokenBy).c_str() : "STANDING", tick * kStep,
                run.level.portals.traversals);
    CHECK_MSG(wall->broken, "the first stone, tick:(x, y)" + trace);
    CHECK_MSG(wall->brokenBy == "entity_launcher_923#1", "broken by " + wall->brokenBy);
    CHECK_EQ(run.level.portals.traversals, 1);
    CHECK(run.level.portals.placed.empty());
}

void Level25sStoneOnTheButton() {
    // level25 has no wall: its launcher's stones are for button_1044, on
    // single_block_plat_ent_1075, which raises door_lift_1043. A portal in the
    // drop and one over the button: the first stone lands on it and the door goes
    // up.
    Run run;
    if (!Begin("level25", run)) return;
    const auto door = run.level.built.entities.find("door_lift_1043");
    CHECK(door != run.level.built.entities.end() && run.level.channels.buttons.size() == 1);
    if (door == run.level.built.entities.end() || run.level.channels.buttons.size() != 1) return;
    const double closedY = PxOf(run, door->second).y;
    CHECK(run.level.portals.TryPlace(glm::dvec2(102.0, 60.0)));
    CHECK(run.level.portals.TryPlace(glm::dvec2(480.0, 160.0)));
    int tick = 0;
    for (; tick < 600 && !run.level.channels.buttons.front().pressed; ++tick) Tick(run);
    std::printf("  level25: button_1044 %s after %.2f s\n",
                run.level.channels.buttons.front().pressed ? "pressed" : "NOT PRESSED", tick * kStep);
    CHECK(run.level.channels.buttons.front().pressed);
    for (int more = 0; more < 60; ++more) Tick(run);
    const double risen = closedY - PxOf(run, door->second).y;
    CHECK_MSG(risen > 100.0, "the door rose " + std::to_string(risen) + " px");
}

void runTests() {
    ChapterOnesLaunchers();
    AThrowIsAStoneAtItsLauncher();
    WhatFallsOutIsTakenBack();
    ADestroyierTakesAStoneBack();
    OutOfTheCullBoxIsTakenBack();
    AThrownStoneBreaksTheWall("level23", "breakable_wall_922", glm::dvec2(74.0, 90.0), glm::dvec2(448.0, 110.0));
    AThrownStoneBreaksTheWall("level24", "breakable_wall_922", glm::dvec2(454.0, 100.0), glm::dvec2(190.0, 80.0));
    Level25sStoneOnTheButton();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_launchers: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_launchers", 40);
}
