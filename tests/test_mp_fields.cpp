// Shock fields: the lethal ring of chapter 3, and the node it swings about.
//
// What is pinned here is the DECODE, and above all the thing a careless port gets
// wrong: the placed node is the CENTRE of the oscillation, not where the ring
// starts. angle seeds at 0 and the displacement is cos(angle) * stride, so the
// ring stands a full stride from its node on the very first frame and swings
// through the node to the far side. A test that only checked the radius would
// pass under either reading, so what is asserted is the DISPLACEMENT.
//
// level27b holds three rings and is the only level in the game that places a
// moving one where it can be played: 64 px at (448, 76) and 48 px at (160, 210)
// standing still, and 36 px at (320, 128) swinging 64 px at 1.8 radians a second.
// The others that move are in chapter 4, which no_gravity stops starting.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Fields.hpp"
#include "sim/Game.hpp"
#include "sim/Units.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

using namespace MagicPortals;
using Supersonic::RigidBodyComponent;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

constexpr const char* kLevel = "level27b";
constexpr const char* kStill = "shock_agent_ent_2130"; // (448, 76), radius 64
constexpr const char* kMoving = "shock_agent_2128";    // (320, 128), radius 36, 1.8 rad/s, stride 64
constexpr const char* kThird = "shock_agent_2048";     // (160, 210), radius 48

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-fields", run.data,
                                   error) &&
                    Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + std::string(": ") + error);
    return ok;
}

void Tick(Run& run) {
    Game::Tick(run.data, run.registry, run.level, 0.0f, kStep);
}

void PutAt(entt::registry& registry, entt::entity body, const glm::dvec2& atPx) {
    const glm::vec3 world = Units::ToWorld(atPx.x, atPx.y);
    auto& transform = registry.get<TransformComponent>(body);
    transform.position = glm::vec3(world.x, world.y, transform.position.z);
    if (registry.all_of<RigidBodyComponent>(body)) {
        registry.get<RigidBodyComponent>(body).velocity = glm::vec3(0.0f);
    }
}

void theRulesAndWhatLevelTwentySevenHolds() {
    Run run;
    if (!Begin(kLevel, run)) return;

    CHECK(run.level.fields.rules.minRadiusPx > 0.0);
    CHECK_EQ(run.level.fields.fields.size(), static_cast<std::size_t>(3));
    CHECK_MSG(run.level.fields.Moving() == static_cast<std::size_t>(1),
              "one of level27b's three rings swings; the other two stand still");
    CHECK(!run.level.fields.playerKilled);

    const Fields::Field* still = run.level.fields.FindField(kStill);
    CHECK(still != nullptr);
    if (still != nullptr) {
        CHECK(::test::nearly(static_cast<float>(still->radiusPx), 64.0f));
        CHECK(::test::nearly(static_cast<float>(still->speedRadPerSec), 0.0f));
        CHECK_MSG(std::fabs(still->centrePx.x - 448.0) < 1.0 && std::fabs(still->centrePx.y - 76.0) < 1.0,
                  "where the level puts it");
        CHECK_MSG(still->body != entt::null, "the converter gives every agent a trigger, and the builder keeps it");
    }

    const Fields::Field* moving = run.level.fields.FindField(kMoving);
    CHECK(moving != nullptr);
    if (moving != nullptr) {
        CHECK(::test::nearly(static_cast<float>(moving->radiusPx), 36.0f));
        CHECK(::test::nearly(static_cast<float>(moving->speedRadPerSec), 1.8f));
        CHECK(::test::nearly(static_cast<float>(moving->stridePx), 64.0f));
        CHECK_MSG(moving->direction == "vertical", "the key the original copies and never reads");
        CHECK_MSG(std::fabs(moving->centrePx.x - 320.0) < 1.0 && std::fabs(moving->centrePx.y - 128.0) < 1.0,
                  "and its node is the centre of that swing");
    }

    const Fields::Field* third = run.level.fields.FindField(kThird);
    CHECK(third != nullptr);
    if (third != nullptr) CHECK(::test::nearly(static_cast<float>(third->radiusPx), 48.0f));
}

// The whole of what a port gets wrong if it reads the node as a starting point.
void theNodeIsTheCentreOfTheSwing() {
    Run run;
    if (!Begin(kLevel, run)) return;
    const Fields::Field* found = run.level.fields.FindField(kMoving);
    if (found == nullptr) return;
    const glm::dvec2 centre = found->centrePx;
    const double stride = found->stridePx;

    // One tick in, cos(angle) is still all but 1, so the ring stands a FULL
    // stride from its node rather than on it.
    Tick(run);
    const Fields::Field* first = run.level.fields.FindField(kMoving);
    CHECK(first != nullptr);
    if (first == nullptr) return;
    const double firstOffset = first->atPx.y - centre.y;
    CHECK_MSG(firstOffset > stride * 0.9,
              "a stride from its node on the first tick, not on it: " + std::to_string(static_cast<int>(firstOffset)));
    CHECK_MSG(std::fabs(first->atPx.x - centre.x) < 0.001, "and vertically, which is the only way one moves");

    // Half a period is PI / 1.8 s, which is 105 ticks of 1/60. By then it has
    // swung through the node to the far side.
    for (int tick = 0; tick < 104; ++tick) Tick(run);
    const Fields::Field* half = run.level.fields.FindField(kMoving);
    CHECK(half != nullptr);
    if (half == nullptr) return;
    const double halfOffset = half->atPx.y - centre.y;
    CHECK_MSG(halfOffset < -stride * 0.9,
              "and the far side after half a period: " + std::to_string(static_cast<int>(halfOffset)));

    // The still ones never left their nodes.
    const Fields::Field* still = run.level.fields.FindField(kStill);
    if (still != nullptr) {
        CHECK_MSG(std::fabs(still->atPx.x - still->centrePx.x) < 0.001 &&
                      std::fabs(still->atPx.y - still->centrePx.y) < 0.001,
                  "a ring with no speed stands where it was placed");
    }
}

// It kills, and the death is Hazards' as every other death in the port is.
void aRingKillsWhatStandsInIt() {
    Run run;
    if (!Begin(kLevel, run)) return;
    if (run.level.player == entt::null) return;
    const Fields::Field* still = run.level.fields.FindField(kStill);
    if (still == nullptr) return;

    PutAt(run.registry, run.level.player, still->centrePx);
    Tick(run);

    CHECK_MSG(run.level.fields.playerKilled, "standing in a ring kills");
    CHECK_MSG(run.level.fields.killedBy == kStill, "named by the ring it died in, got '" + run.level.fields.killedBy + "'");
    CHECK_MSG(run.level.hazards.playerDied, "and it is folded into Hazards, as the boss's and the fire's are");
    CHECK_MSG(run.level.hazards.killedBy == run.level.fields.killedBy, "under the same name");
}

void andLeavesAloneWhatStandsOutsideIt() {
    Run run;
    if (!Begin(kLevel, run)) return;
    if (run.level.player == entt::null) return;
    const Fields::Field* still = run.level.fields.FindField(kStill);
    if (still == nullptr) return;

    // Just beyond the radius, and well clear of the other two rings.
    const glm::dvec2 outside(still->centrePx.x + still->radiusPx + 8.0, still->centrePx.y);
    for (int tick = 0; tick < 10; ++tick) {
        PutAt(run.registry, run.level.player, outside);
        Tick(run);
    }
    CHECK_MSG(!run.level.fields.playerKilled, "8 px beyond the radius is outside it");
    CHECK_MSG(!run.level.hazards.playerDied, "and nothing else killed the player there either");
}

// The poll, rather than an entry: the player never moves, and the ring arrives.
void aSwingingRingReachesAStandingPlayer() {
    Run run;
    if (!Begin(kLevel, run)) return;
    if (run.level.player == entt::null) return;
    const Fields::Field* moving = run.level.fields.FindField(kMoving);
    if (moving == nullptr) return;

    // On the far side of the node from where the ring begins: 128 px from it at
    // the first tick, which is far outside its 36.
    const glm::dvec2 standing(moving->centrePx.x, moving->centrePx.y - moving->stridePx);

    PutAt(run.registry, run.level.player, standing);
    Tick(run);
    CHECK_MSG(!run.level.fields.playerKilled, "the ring starts a stride away on the other side");

    int died = 0;
    for (int tick = 1; tick <= 160 && !run.level.fields.playerKilled; ++tick) {
        PutAt(run.registry, run.level.player, standing);
        Tick(run);
        if (run.level.fields.playerKilled) died = tick;
    }
    CHECK_MSG(run.level.fields.playerKilled, "and swings onto a player that never moved");
    CHECK_MSG(run.level.fields.killedBy == kMoving, "killed by the ring that moved, got '" +
                                                        run.level.fields.killedBy + "'");
    CHECK_MSG(died > 40 && died < 140, "around half a period in: tick " + std::to_string(died));
    std::printf("  level27b: the swinging ring reached a standing player after %d tick(s)\n", died);
}

void everyStartingLevelFindsItsRings() {
    const struct Level {
        const char* name;
        std::size_t count;
    } levels[] = {{"level23b", 2}, {"level24b", 1}, {"level25b", 1}, {"level26b", 1},
                  {"level27b", 3}, {"level30b", 1}, {"level27c", 1}};

    std::size_t total = 0;
    for (const Level& one : levels) {
        Run run;
        if (!Begin(one.name, run)) continue;
        CHECK_MSG(run.level.fields.fields.size() == one.count,
                  std::string(one.name) + " places " + std::to_string(one.count) + " shock agent(s)");
        total += run.level.fields.fields.size();
        for (const Fields::Field& field : run.level.fields.fields) {
            CHECK_MSG(field.radiusPx >= run.level.fields.rules.minRadiusPx,
                      std::string(one.name) + ": " + field.name + " has a radius");
        }
    }
    CHECK_MSG(total == 10, "10 of the game's 21 shock agents are in levels that start");
    std::printf("  %zu shock agent(s) across %zu level(s) that start\n", total, sizeof(levels) / sizeof(levels[0]));
}

void runTests() {
    theRulesAndWhatLevelTwentySevenHolds();
    theNodeIsTheCentreOfTheSwing();
    aRingKillsWhatStandsInIt();
    andLeavesAloneWhatStandsOutsideIt();
    aSwingingRingReachesAStandingPlayer();
    everyStartingLevelFindsItsRings();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_fields SKIPPED - needs the converted levels at %s and the remake's data at %s\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_fields", 40);
}
