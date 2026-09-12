// The minions of worlds 3 and 4: spawned from a marker, walking a prefix of
// waypoints, and taken by the floor that kills them and nothing else.
//
// What is pinned here is the DECODE. Minions.hpp cites where each number comes
// from; the ones a test can tell apart from the remake's guesses are:
//
//   speed    1.3 px per physics step, and the game FIXES that step at 1/30 s
//            (AverageFPSRateManager calls SetFixedTimeStep(true) with 0.0333333),
//            so the walk is 39 px/s. The remake's actors.json guesses 60 and
//            marks it _guess: true.
//   arrival  8 px to advance, but 16 px to stop - minDist and minDist * 2 are
//            two separate tests in the original. Collapsing them into one
//            constant is the easy mistake, and it gives a minion that either
//            jitters at its waypoint or never settles, so both are pinned.
//   hold     a DWELL in milliseconds, adopted from the waypoint just REACHED.
//            A lone waypoint carrying the 999999999 sentinel is a stationary
//            guard, and needs no special case: it is a wait nothing outlasts.
//
// And one thing that is not a number: enemy_killer kills no player. The remake's
// role table files it under `hazard` beside death_area.ent and lava, which would
// kill the player on contact in all 44 levels that carry one.
// ETHBeginContactCallback_enemy_killer is 69 instructions that open with
// isMinion(other) and jump to the return when that is false. It is sensor=0 and
// converts to a solid StaticBody2D - a floor to stand on. Both directions are
// tested, because that is exactly the kind of thing a role table smooths over:
// the floor is no hazard and kills no player, and it does take a minion.
//
// level0b is the first level of world 3 and carries all of it at once: a minion
// patrolling wayA0 <-> wayA1 (both holding 4000), a guard on a lone wayB0 with
// the sentinel, and a 768 x 128 enemy_killer floor.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Game.hpp"
#include "sim/Minions.hpp"
#include "sim/Units.hpp"

#include <algorithm>
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

constexpr const char* kLevel = "level0b";
constexpr const char* kPatroller = "minion_spawn_1811"; // waypointName "wayA"
constexpr const char* kGuard = "minion_spawn_913";      // waypointName "wayB", one waypoint
constexpr const char* kKiller = "enemy_killer_1915";    // 768 x 128, at (1152, 448)

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-minions", run.data,
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
    registry.get<RigidBodyComponent>(body).velocity = glm::vec3(0.0f);
}

float VelocityX(entt::registry& registry, entt::entity body) {
    return registry.get<RigidBodyComponent>(body).velocity.x;
}

double PixelX(entt::registry& registry, entt::entity body) {
    return Units::ToPixels(registry.get<TransformComponent>(body).position).x;
}

// 1.3 px per 33.3333 ms is 39 px/s, and the remake's guess is 60.
void theWalkIsTheDecodedSpeedNotTheRemakesGuess() {
    Run run;
    if (!Begin(kLevel, run)) return;

    const Minions::Rules& rules = run.level.minions.rules;
    CHECK(::test::nearly(static_cast<float>(rules.speedPxPerStep), 1.3f));
    CHECK(::test::nearly(static_cast<float>(rules.stepMs), 33.3333f, 1e-3f));
    CHECK(::test::nearly(static_cast<float>(run.level.minions.SpeedPxPerSecond()), 39.0f, 1e-2f));
    CHECK_MSG(run.level.minions.SpeedPxPerSecond() < 60.0, "the remake's 60 px/s is not what is played");

    // Two thresholds, not one: it advances inside 8 px and stops inside 16.
    CHECK(::test::nearly(static_cast<float>(rules.arrivePx), 8.0f));
    CHECK(::test::nearly(static_cast<float>(rules.settlePx), 16.0f));
    CHECK_MSG(rules.settlePx > rules.arrivePx, "the stop is wider than the advance");
    CHECK(::test::nearly(static_cast<float>(rules.settlePx), static_cast<float>(rules.arrivePx * 2.0)));
}

// waypointName is a PREFIX, and the marker resolves it by seeking prefix + n.
void aMarkerResolvesItsWaypointsByPrefix() {
    Run run;
    if (!Begin(kLevel, run)) return;

    CHECK_EQ(run.level.minions.minions.size(), static_cast<std::size_t>(2));

    const Minions::Minion* patroller = run.level.minions.FindMinion(kPatroller);
    CHECK(patroller != nullptr);
    if (patroller != nullptr) {
        CHECK_MSG(patroller->waypointName == "wayA", "the patroller's prefix is wayA");
        CHECK_EQ(patroller->waypoints.size(), static_cast<std::size_t>(2));
        if (patroller->waypoints.size() == 2) {
            CHECK_MSG(patroller->waypoints[0].name == "wayA0", "the first waypoint is wayA0");
            CHECK_MSG(patroller->waypoints[1].name == "wayA1", "the second is wayA1");
            CHECK(::test::nearly(static_cast<float>(patroller->waypoints[0].atPx.x), 509.0f));
            CHECK(::test::nearly(static_cast<float>(patroller->waypoints[1].atPx.x), 547.0f));
            // The hold is quoted in the scene ("4000"); Value::AsNumber reads it.
            CHECK(::test::nearly(static_cast<float>(patroller->waypoints[0].holdMs), 4000.0f));
            CHECK(::test::nearly(static_cast<float>(patroller->waypoints[1].holdMs), 4000.0f));
        }
    }

    const Minions::Minion* guard = run.level.minions.FindMinion(kGuard);
    CHECK(guard != nullptr);
    if (guard != nullptr) {
        CHECK_MSG(guard->waypointName == "wayB", "the guard's prefix is wayB");
        // One resolved waypoint means "stand still", not "data missing".
        CHECK_EQ(guard->waypoints.size(), static_cast<std::size_t>(1));
        if (!guard->waypoints.empty()) {
            CHECK_MSG(guard->waypoints[0].name == "wayB0", "its one waypoint is wayB0");
            CHECK_MSG(guard->waypoints[0].holdMs > 9.9e8, "wayB0 carries the stationary sentinel");
        }
    }

    // A minion is teleportable (minion.ent carries the flag), so the portals
    // carry one as they carry what a launcher throws.
    if (patroller != nullptr && patroller->body != entt::null) {
        const auto& travellers = run.level.portals.travellers;
        CHECK_MSG(std::find(travellers.begin(), travellers.end(), patroller->body) != travellers.end(),
                  "a spawned minion is among the portals' travellers");
    }
}

void aPatrollerWalksToItsFirstWaypoint() {
    Run run;
    if (!Begin(kLevel, run)) return;

    const Minions::Minion* patroller = run.level.minions.FindMinion(kPatroller);
    CHECK(patroller != nullptr);
    if (patroller == nullptr || patroller->body == entt::null) return;
    const entt::entity body = patroller->body;

    Tick(run);
    CHECK(run.registry.valid(body));
    if (!run.registry.valid(body)) return;

    // It spawns at x 546 and wayA0 is at 509, so it walks left - at 39 px/s,
    // which is 0.78 m/s in the engine's units.
    CHECK(VelocityX(run.registry, body) < 0.0f);
    CHECK(::test::nearly(VelocityX(run.registry, body), -Units::ToMetres(39.0), 1e-3f));
}

// Arrival adopts the hold of the waypoint REACHED, and the walk resumes only
// once the dwell is served.
void arrivingAdoptsTheHoldAndServesIt() {
    Run run;
    if (!Begin(kLevel, run)) return;

    const Minions::Minion* found = run.level.minions.FindMinion(kPatroller);
    CHECK(found != nullptr);
    if (found == nullptr || found->body == entt::null) return;
    const entt::entity body = found->body;

    PutAt(run.registry, body, glm::dvec2(509.0, 112.0));
    Tick(run);

    const Minions::Minion* arrived = run.level.minions.FindMinion(kPatroller);
    CHECK(arrived != nullptr);
    if (arrived == nullptr) return;
    CHECK_EQ(arrived->dest, static_cast<std::size_t>(1));
    CHECK_EQ(arrived->arrivedAt, static_cast<std::size_t>(0));
    CHECK(::test::nearly(static_cast<float>(arrived->holdMs), 4000.0f));
    CHECK(::test::nearly(VelocityX(run.registry, body), 0.0f));

    // Most of the way through the 4000 ms hold: still standing.
    for (int tick = 0; tick < 150; ++tick) Tick(run);
    CHECK(run.registry.valid(body));
    if (!run.registry.valid(body)) return;
    CHECK_MSG(::test::nearly(VelocityX(run.registry, body), 0.0f), "still serving the hold at 2.5 s");

    // And past it: walking again, now towards wayA1 at x 547.
    for (int tick = 0; tick < 120; ++tick) Tick(run);
    CHECK(run.registry.valid(body));
    if (!run.registry.valid(body)) return;
    CHECK_MSG(VelocityX(run.registry, body) > 0.0f, "walking again once the hold is served");
}

// A lone waypoint, and the wider threshold that belongs to it alone.
//
// The guard is NOT motionless from the start, which is the part worth pinning: it
// spawns 31 px short of its post at x 177 and walks there. What stops it being
// DRIVEN is the minDist * 2 test, which the original computes only for a minion
// with exactly one waypoint - and from there the two engines part company. The
// original's minion is frictionless (minion.ent friction 0.000000), so it coasts
// the last stretch in, arrives, adopts the 999999999 sentinel, and is parked by
// the brake. This port's bodies grip (kBodyFriction 1), so its guard parks a
// pixel or so short of arriving instead. Either way it ends up at its post and
// stays there, which is what is pinned here; minions.json carries the difference.
void aLoneWaypointIsWalkedToAndThenHeldForGood() {
    Run run;
    if (!Begin(kLevel, run)) return;

    const Minions::Minion* guard = run.level.minions.FindMinion(kGuard);
    CHECK(guard != nullptr);
    if (guard == nullptr || guard->body == entt::null || guard->waypoints.empty()) return;
    const entt::entity body = guard->body;
    const glm::dvec2 post = guard->waypoints[0].atPx;

    Tick(run);
    CHECK(run.registry.valid(body));
    if (!run.registry.valid(body)) return;
    CHECK_MSG(VelocityX(run.registry, body) > 0.0f, "the guard sets off towards its post");

    for (int tick = 0; tick < 180; ++tick) Tick(run);
    CHECK(run.registry.valid(body));
    if (!run.registry.valid(body)) return;

    const Minions::Minion* posted = run.level.minions.FindMinion(kGuard);
    CHECK(posted != nullptr);
    if (posted == nullptr) return;
    CHECK_MSG(std::abs(PixelX(run.registry, body) - post.x) < run.level.minions.rules.settlePx,
              "the guard stopped within the wider threshold of its post");
    CHECK_MSG(posted->waypoints.size() == 1, "and it got there on one waypoint, which is why it stopped");
    CHECK(::test::nearly(VelocityX(run.registry, body), 0.0f));

    // Still there three seconds later: the wait is never restarted.
    const double atPost = PixelX(run.registry, body);
    for (int tick = 0; tick < 180; ++tick) Tick(run);
    CHECK(run.registry.valid(body));
    if (!run.registry.valid(body)) return;
    CHECK(::test::nearly(VelocityX(run.registry, body), 0.0f));
    CHECK_MSG(std::abs(PixelX(run.registry, body) - atPost) < 1.0, "and stands at its post for good");
}

// The settled branch writes NO velocity, which leaves a minion inside the band to
// its body's own friction. That is only safe because the body cannot roll: the
// original is fixedRotation=1 and the port locks z to match. A minion is
// teleportable and is among the portals' travellers, so arriving at its post
// carrying speed is a state a real level can reach - this gives it three times a
// walk and checks it still parks rather than sailing through.
void aGuardCarryingMomentumStillParks() {
    Run run;
    if (!Begin(kLevel, run)) return;

    const Minions::Minion* guard = run.level.minions.FindMinion(kGuard);
    CHECK(guard != nullptr);
    if (guard == nullptr || guard->body == entt::null || guard->waypoints.empty()) return;
    const entt::entity body = guard->body;
    const glm::dvec2 post = guard->waypoints[0].atPx;

    // Let it walk to its post and settle there first.
    for (int tick = 0; tick < 180; ++tick) Tick(run);
    CHECK(run.registry.valid(body));
    if (!run.registry.valid(body)) return;

    // Then shove it at its post, three times as fast as it walks.
    run.registry.get<RigidBodyComponent>(body).velocity.x = 3.0f * Units::ToMetres(39.0);
    for (int tick = 0; tick < 120; ++tick) Tick(run);

    CHECK(run.registry.valid(body));
    if (!run.registry.valid(body)) return;
    CHECK(::test::nearly(VelocityX(run.registry, body), 0.0f));
    CHECK_MSG(std::abs(PixelX(run.registry, body) - post.x) < run.level.minions.rules.settlePx,
              "momentum did not carry the guard through its post");
}

// The box is the shape the level gives it, not Trigger's 16 px fallback.
//
// And the floor is TILED rather than single: level0b lays two 768 x 128 slabs end
// to end, at x 384 and x 1152. Thirty of the 44 levels carry one slab and
// fourteen carry two, so the count is pinned too - a finder that took only the
// first would leave half of those levels' floors inert.
void theKillerFloorIsTheShapeNotTheFallback() {
    Run run;
    if (!Begin(kLevel, run)) return;

    CHECK_EQ(run.level.minions.killers.size(), static_cast<std::size_t>(2));

    const Minions::Killer* found = nullptr;
    for (const Minions::Killer& killer : run.level.minions.killers) {
        if (killer.name == kKiller) found = &killer;
    }
    CHECK_MSG(found != nullptr, "the level's enemy_killer is among them");
    if (found == nullptr) return;
    CHECK(::test::nearly(found->box.half.x, Units::ToMetres(384.0)));
    CHECK(::test::nearly(found->box.half.y, Units::ToMetres(64.0)));
    CHECK_MSG(found->box.half.x > Units::ToMetres(8.0), "not the 16 px fallback a hazard would get");
}

// The correction, in the direction that matters most: it is not a hazard, and a
// player stood in it does not die.
void theKillerFloorKillsNoPlayer() {
    Run run;
    if (!Begin(kLevel, run)) return;

    CHECK_MSG(run.level.hazards.FindHazard(kKiller) == nullptr, "the killer floor is no hazard");
    for (const Hazards::Hazard& hazard : run.level.hazards.hazards) {
        CHECK_MSG(hazard.name.rfind("enemy_killer", 0) != 0, hazard.name + " is not among the hazards");
    }

    if (run.level.player == entt::null) return;
    PutAt(run.registry, run.level.player, glm::dvec2(1152.0, 448.0));
    for (int tick = 0; tick < 60; ++tick) Tick(run);
    CHECK_MSG(!run.level.hazards.playerDied, "a second stood in the killer floor does not kill the player");
}

// And in the other direction: it does take a minion.
void theKillerFloorTakesAMinion() {
    Run run;
    if (!Begin(kLevel, run)) return;

    const Minions::Minion* guard = run.level.minions.FindMinion(kGuard);
    CHECK(guard != nullptr);
    if (guard == nullptr || guard->body == entt::null) return;
    const entt::entity body = guard->body;
    CHECK_EQ(run.level.minions.killed, 0);

    PutAt(run.registry, body, glm::dvec2(1152.0, 448.0));
    for (int tick = 0; tick < 5 && run.registry.valid(body); ++tick) Tick(run);

    CHECK(!run.registry.valid(body));
    CHECK_EQ(run.level.minions.killed, 1);
    const Minions::Minion* after = run.level.minions.FindMinion(kGuard);
    CHECK(after != nullptr);
    if (after != nullptr) CHECK(after->gone);

    // And it went through Forget, as a burnt crate does.
    const auto& travellers = run.level.portals.travellers;
    CHECK_MSG(std::find(travellers.begin(), travellers.end(), body) == travellers.end(),
              "a taken minion is no longer a traveller");
}

void runTests() {
    theWalkIsTheDecodedSpeedNotTheRemakesGuess();
    aMarkerResolvesItsWaypointsByPrefix();
    aPatrollerWalksToItsFirstWaypoint();
    arrivingAdoptsTheHoldAndServesIt();
    aLoneWaypointIsWalkedToAndThenHeldForGood();
    aGuardCarryingMomentumStillParks();
    theKillerFloorIsTheShapeNotTheFallback();
    theKillerFloorKillsNoPlayer();
    theKillerFloorTakesAMinion();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_minions SKIPPED - needs the converted levels at %s and the remake's data at %s\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_minions", 40);
}
