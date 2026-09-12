// Rolling stones, and the walls they break.
//
// Pinned exactly, because the files say it: chapter 1's stones and breakable
// walls, which of the walls the level flags and which break by name, and each
// wall's box. Breaking by name is the owner's testimony, carried in
// demolish.json, so the remake's reading - the flag alone - is run too. What
// happens is judged on the tick:
//   - a stone thrown at a wall breaks it and takes its body away, and nothing
//     else;
//   - a stone where the level puts it, and the player walking into a wall,
//     break nothing.
// The contact margin is a guess carried as data, so the edge is found about
// whatever margin the data holds rather than at a number pinned here.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Demolish.hpp"
#include "sim/Game.hpp"
#include "sim/Units.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;
using Supersonic::RigidBodyComponent;
using Supersonic::SphereColliderComponent;
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

// byName false is the remake's reading: only what the level flags breaks.
bool Begin(const std::string& levelName, Run& run, bool byName = true) {
    std::string error;
    bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                             std::filesystem::temp_directory_path() / "supersonic-test-mp-demolish", run.data, error);
    if (ok && !byName) run.data.demolish.breakableNames.clear();
    ok = ok && Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + ": " + error);

    // THE FIRING RATE OFF, on purpose, and this suite is the one that most needs
    // saying so. The original refuses a tap for the first 300 ms of a level and
    // for 400 ms after the last one it took, and both clocks start at zero
    // (placement.json); ThePortalCooldownsHoldATapOff in test_mp_play pins that.
    //
    // ShootAndLand below returns false when Shoot is REFUSED, and its callers
    // read that as "the shot did not land". With the cooldown live, a play
    // through 1-9 would quietly report a failed shot instead of erroring - a
    // wrong answer that looks like a real one. This suite is about what a stone
    // breaks, so the rate limit is turned off where it can be seen.
    run.level.portals.rules.firstPortalMinMs = 0.0;
    run.level.portals.rules.nextPortalMinMs = 0.0;
    return ok;
}

void Tick(Run& run, float direction = 0.0f) { Game::Tick(run.data, run.registry, run.level, direction, kStep); }

// A body put down at a point in the remake's pixels, moving at a velocity in them.
void Put(Run& run, entt::entity body, const glm::dvec2& atPx, const glm::dvec2& velocityPx = glm::dvec2(0.0)) {
    auto& transform = run.registry.get<TransformComponent>(body);
    const glm::vec3 at = Units::ToWorld(atPx.x, atPx.y);
    transform.position = glm::vec3(at.x, at.y, transform.position.z);
    run.registry.get<RigidBodyComponent>(body).velocity =
        glm::vec3(Units::ToMetres(velocityPx.x), Units::ToMetres(-velocityPx.y), 0.0f);
}

glm::dvec2 PxOf(const Run& run, entt::entity body) {
    return Units::ToPixels(run.registry.get<TransformComponent>(body).position);
}

std::string Px(const glm::dvec2& at) {
    char text[48];
    std::snprintf(text, sizeof text, "(%.1f, %.1f)", at.x, at.y);
    return text;
}

bool Built(const Run& run, const char* name) {
    const auto found = run.level.built.entities.find(name);
    return found != run.level.built.entities.end() && run.registry.valid(found->second);
}

// ---- What the levels say ------------------------------------------------------

void ChapterOnesStonesAndWalls() {
    // Read off the level files by the census script: every rolling stone, and
    // every entity flagged breakable or named breakable_wall. A quarter-turned
    // wall lies on its side, 126 x 30.
    struct Wall {
        const char* name;
        glm::dvec2 centre;
        glm::dvec2 size;
        bool flagged;
    };
    struct Expected {
        const char* level;
        std::vector<const char*> stones;
        std::vector<Wall> walls;
    };
    const glm::dvec2 upright(30.0, 126.0);
    const glm::dvec2 onItsSide(126.0, 30.0);
    const Expected expected[] = {
        {"level8", {"rolling_stone_ent_597"}, {{"breakable_wall_625", {320.0, 124.0}, upright, false}}},
        {"level12", {"rolling_stone_619"}, {{"breakable_wall_577", {192.0, 282.0}, onItsSide, false}}},
        {"level18", {"rolling_stone_728"}, {{"breakable_wall_731", {447.0, 282.0}, onItsSide, false}}},
        {"level19",
         {"rolling_stone_728", "rolling_stone_1048"},
         {{"breakable_wall_ent_1061", {512.0, 176.0}, onItsSide, true},
          {"breakable_wall_ent_1056", {68.0, 192.0}, upright, true}}},
        {"level21",
         {"rolling_stone_727", "rolling_stone_753"},
         {{"breakable_wall_719", {300.0, 162.0}, upright, false}, {"breakable_wall_755", {332.0, 162.0}, upright, false}}},
        {"level22", {"rolling_stone_ent_877", "rolling_stone_ent_989"}, {}},
        // Walls with no stone: what breaks them is the launcher's, in step 7.
        {"level23", {}, {{"breakable_wall_922", {448.0, 177.0}, onItsSide, false}}},
        {"level24", {}, {{"breakable_wall_922", {190.0, 144.0}, onItsSide, false}}},
    };
    for (const Expected& level : expected) {
        Run run;
        if (!Begin(level.level, run)) continue;
        const Demolish::State& demolish = run.level.demolish;
        CHECK_MSG(demolish.stones.size() == level.stones.size(),
                  std::string(level.level) + " has " + std::to_string(demolish.stones.size()) + " stones");
        for (const char* name : level.stones) {
            CHECK_MSG(demolish.FindStone(name) != nullptr, std::string(level.level) + " has " + name);
        }
        CHECK_MSG(demolish.breakables.size() == level.walls.size(),
                  std::string(level.level) + " has " + std::to_string(demolish.breakables.size()) + " breakables");
        for (const Wall& wall : level.walls) {
            const Demolish::Breakable* found = demolish.FindBreakable(wall.name);
            CHECK_MSG(found != nullptr, std::string(level.level) + " has " + wall.name);
            if (found == nullptr) continue;
            const glm::dvec2 centre = Units::ToPixels(glm::vec3(found->box.centre, 0.0f));
            const glm::dvec2 size = glm::dvec2(found->box.half) * 2.0 * Units::kPixelsPerMetre;
            CHECK_MSG(glm::distance(centre, wall.centre) < 1e-3 && std::fabs(size.x - wall.size.x) < 1e-3 &&
                          std::fabs(size.y - wall.size.y) < 1e-3,
                      std::string(wall.name) + " is " + Px(size) + " at " + Px(centre));
            CHECK_MSG(found->flagged == wall.flagged, std::string(wall.name) + (found->flagged ? " is" : " is not") +
                                                          " flagged");
            CHECK(!found->broken);
        }
    }
}

void TheRemakesReadingBreaksOnlyWhatIsFlagged() {
    // The same levels with the name rule off. level12a's carranca is flagged, so a
    // stone would break it either way; level3a's wall carries no metadata at all.
    struct Count {
        const char* level;
        std::size_t byName;
        std::size_t flagOnly;
    };
    const Count counts[] = {
        {"level8", 1, 0},  {"level12", 1, 0}, {"level18", 1, 0}, {"level19", 2, 2},
        {"level21", 2, 0}, {"level3a", 1, 0}, {"level12a", 2, 2},
    };
    for (const Count& count : counts) {
        Run withName;
        Run flagOnly;
        if (!Begin(count.level, withName) || !Begin(count.level, flagOnly, false)) continue;
        CHECK_MSG(withName.level.demolish.breakables.size() == count.byName,
                  std::string(count.level) + ": " + std::to_string(withName.level.demolish.breakables.size()) +
                      " breakable by flag or name");
        CHECK_MSG(flagOnly.level.demolish.breakables.size() == count.flagOnly,
                  std::string(count.level) + ": " + std::to_string(flagOnly.level.demolish.breakables.size()) +
                      " breakable by flag alone");
    }
}

// ---- Breaking -----------------------------------------------------------------

// level8's stone, thrown right at its wall from 6 px short of touching it, high
// enough to miss the ramp below: the wall's face is at x = 305 and it stands
// from y = 61 to 187.
void ThrowLevel8sStone(Run& run) {
    const Demolish::Stone& stone = run.level.demolish.stones.front();
    Put(run, stone.body, glm::dvec2(305.0 - 30.0 - 6.0, 95.0), glm::dvec2(300.0, 0.0));
}

void AStoneThrownAtAWallBreaksIt() {
    Run run;
    if (!Begin("level8", run)) return;
    if (run.level.demolish.stones.size() != 1) return;
    const std::string stone = run.level.demolish.stones.front().name;
    ThrowLevel8sStone(run);
    int tick = 0;
    for (; tick < 30 && run.level.demolish.Broken() == 0; ++tick) Tick(run);
    std::printf("  level8's stone, thrown at 300 px/s from 6 px: the wall %s after %d tick(s)\n",
                run.level.demolish.Broken() == 1 ? "broke" : "DID NOT BREAK", tick);
    const Demolish::Breakable* wall = run.level.demolish.FindBreakable("breakable_wall_625");
    CHECK(wall != nullptr);
    if (wall == nullptr) return;
    CHECK(wall->broken);
    CHECK_MSG(wall->brokenBy == stone, "broken by " + wall->brokenBy);
    // The wall's body is gone, collision and all, and nothing else is.
    CHECK(!Built(run, "breakable_wall_625"));
    CHECK(Built(run, "block00_ent_624"));
    CHECK(Built(run, "ramp_left_ent_590"));
    CHECK(Built(run, "rolling_stone_ent_597"));
    // Broken once, and it stays broken.
    for (int more = 0; more < 60; ++more) Tick(run);
    CHECK_EQ(run.level.demolish.Broken(), 1);
    CHECK(!run.level.hazards.playerDied);
}

void WithTheFlagAloneTheStoneStops() {
    Run run;
    if (!Begin("level8", run, false)) return;
    CHECK(run.level.demolish.breakables.empty());
    if (run.level.demolish.stones.size() != 1) return;
    const entt::entity stone = run.level.demolish.stones.front().body;
    ThrowLevel8sStone(run);
    for (int tick = 0; tick < 60; ++tick) Tick(run);
    CHECK(Built(run, "breakable_wall_625"));
    CHECK_MSG(PxOf(run, stone).x < 305.0 - 30.0 + 2.0, "the stone is at " + Px(PxOf(run, stone)));
}

void AStoneWhereTheLevelPutsItBreaksNothing() {
    // level 1-9 spawns the player 58 px from its stone: standing beside one is safe,
    // and the stone left alone stays where it is.
    Run run;
    if (!Begin("level8", run)) return;
    const glm::dvec2 from = PxOf(run, run.level.demolish.stones.front().body);
    for (int tick = 0; tick < 180; ++tick) Tick(run);
    CHECK_EQ(run.level.demolish.Broken(), 0);
    CHECK(!run.level.hazards.playerDied);
    const glm::dvec2 to = PxOf(run, run.level.demolish.stones.front().body);
    CHECK_MSG(glm::distance(from, to) < 8.0, "the stone went from " + Px(from) + " to " + Px(to));
}

void ThePlayerBreaksNothing() {
    Run run;
    if (!Begin("level8", run)) return;
    const double half = run.data.tuning.widthPx * 0.5;
    Put(run, run.level.player, glm::dvec2(305.0 - half - 2.0, 110.0));
    for (int tick = 0; tick < 90; ++tick) Tick(run, 1.0f);
    CHECK_EQ(run.level.demolish.Broken(), 0);
    CHECK(Built(run, "breakable_wall_625"));
    CHECK_MSG(PxOf(run, run.level.player).x < 305.0, "the player is at " + Px(PxOf(run, run.level.player)));
}

void TheMarginIsTheEdge() {
    // On its own: a stone of the game's 30 px and a wall's box, a quarter pixel
    // either side of the margin.
    Demolish::Rules rules;
    std::string error;
    CHECK_MSG(Demolish::LoadRules(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/demolish.json", rules, error), error);
    CHECK(rules.contactMarginPx > 0.25);
    const double radiusPx = 30.0;
    for (const double gapPx : {rules.contactMarginPx - 0.25, rules.contactMarginPx + 0.25}) {
        entt::registry registry;
        Demolish::State state;
        state.rules = rules;
        const entt::entity stone = registry.create();
        registry.emplace<TransformComponent>(stone).position = Units::ToWorld(0.0, 0.0);
        registry.emplace<SphereColliderComponent>(stone).radius = Units::ToMetres(radiusPx);
        state.stones.push_back(Demolish::Stone{"stone", stone});
        const entt::entity wall = registry.create();
        Demolish::Breakable breakable;
        breakable.name = "wall";
        breakable.body = wall;
        const glm::vec3 centre = Units::ToWorld(radiusPx + gapPx + 15.0, 0.0);
        breakable.box.centre = glm::vec2(centre.x, centre.y);
        breakable.box.half = glm::vec2(Units::ToMetres(15.0), Units::ToMetres(63.0));
        state.breakables.push_back(breakable);

        state.Tick(registry);
        const bool within = gapPx < rules.contactMarginPx;
        CHECK_MSG(state.breakables.front().broken == within,
                  std::to_string(gapPx) + " px apart: " + (state.breakables.front().broken ? "broke" : "stood"));
        CHECK(registry.valid(wall) != within);
        CHECK(registry.valid(stone));
    }
}

// ---- level 1-9, played -------------------------------------------------------

// Fires a shot and waits for it to land or fail. True when a portal opened.
bool ShootAndLand(Run& run, const glm::dvec2& atPx) {
    const std::size_t before = run.level.portals.placed.size();
    if (!run.level.portals.Shoot(run.registry, atPx)) return false;
    for (int tick = 0; tick < 600 && run.level.portals.flight; ++tick) Tick(run);
    return run.level.portals.placed.size() > before;
}

// Holds right until the player is at `x` or past it, then lets go and lets it
// settle.
void WalkRightTo(Run& run, double x) {
    for (int tick = 0; tick < 180 && PxOf(run, run.level.player).x < x; ++tick) Tick(run, 1.0f);
    for (int tick = 0; tick < 30; ++tick) Tick(run);
}

void Level8WithTwoShots() {
    // The hand-drawn arrow at (30, 106) points behind the stone, but a shot from
    // the spawn at it meets the stone (test_mp_shot), and so does one from the
    // spawn to anywhere up there low enough for the player to come out of. So
    // the player walks right onto the slope first. From there a shot to (30, 30)
    // passes about 40 px from the stone's centre, over its 30 px. Then one more
    // portal just ahead: walking right through it comes out behind the stone,
    // into the gap between it and the level's edge, and walking on pushes it off
    // the ledge.
    Run run;
    if (!Begin("level8", run)) return;
    for (int tick = 0; tick < 30; ++tick) Tick(run);
    const entt::entity stone = run.level.demolish.stones.front().body;
    WalkRightTo(run, 195.0);
    // The results are taken before the messages are built: a check's message is
    // built before its condition is.
    const bool far = ShootAndLand(run, glm::dvec2(30.0, 30.0));
    CHECK_MSG(far, "the far shot, from " + Px(PxOf(run, run.level.player)) + ", stopped at " +
                       run.level.portals.lastFailure);
    const bool near = ShootAndLand(run, PxOf(run, run.level.player) + glm::dvec2(40.0, 0.0));
    CHECK_MSG(near, "the near shot stopped at " + run.level.portals.lastFailure);
    CHECK_EQ(run.level.portals.shotsFailed, 0);
    std::string trace;
    int tick = 0;
    for (; tick < 1800 && !run.level.goals.completed && !run.level.hazards.playerDied; ++tick) {
        Tick(run, 1.0f);
        if (tick % 30 == 0) {
            trace += " " + std::to_string(tick) + ":" + Px(PxOf(run, run.level.player)) + Px(PxOf(run, stone)) +
                     std::to_string(run.level.demolish.Broken());
        }
    }
    std::printf("  level8 with two shots, holding right: %s after %.2f s, the wall %s\n",
                run.level.goals.completed ? "completed" : "NOT completed", tick * kStep,
                run.level.demolish.Broken() == 1 ? "broken" : "standing");
    CHECK_MSG(run.level.demolish.Broken() == 1, "tick:player/stone/broken" + trace);
    CHECK_MSG(run.level.goals.completed, "tick:player/stone/broken" + trace);
}

void runTests() {
    ChapterOnesStonesAndWalls();
    TheRemakesReadingBreaksOnlyWhatIsFlagged();
    AStoneThrownAtAWallBreaksIt();
    WithTheFlagAloneTheStoneStops();
    AStoneWhereTheLevelPutsItBreaksNothing();
    ThePlayerBreaksNothing();
    TheMarginIsTheEdge();
    Level8WithTwoShots();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_demolish: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_demolish", 40);
}
