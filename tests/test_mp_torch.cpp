// The torches of chapter 4's dark levels, and the wall of light one drops.
//
// WHY THIS SUITE HAS TO EXIST, and it is not the usual reason. test_mp_start
// counts ROLES, and light_wall.ent is in no role table at all - Roles::RoleOf
// answers empty for it and IsPorted answers true. So the moment `torch` is
// admitted, all ten of these levels read "playing" whether or not the wall ever
// moves. Six of them contain a solid 30 x 126 StaticBody2D across the way, and
// the inventory is structurally incapable of noticing it. Nothing but a test that
// watches the BODY can tell a played level from an unfinishable one here.
//
// What is pinned, therefore:
//
//   the switch  a shot within scale(24) of an unlit torch lights it, and the shot
//               is SPENT - killProjectile - rather than going on to open a portal.
//   the wall    destroy() only FLAGS it. It stands for 1500 ms and is gone after,
//               and its body leaves the registry when it goes.
//   the toggle  shooting the orbiting flame puts the torch back out and REBUILDS
//               the wall. A port that built only the first half would play every
//               level correctly and still be wrong about the mechanism.
//   the fuel    hasProjectileAround takes a fireball as readily as a portal shot,
//               so a fire diamond works these switches too.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
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

// level25c is one of the six that ship a light wall.
constexpr const char* kWalled = "level25c";
// level21c has torches and no wall at all, which is the other half of the census.
constexpr const char* kWallless = "level21c";

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-torch", run.data,
                                   error) &&
                    Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + std::string(": ") + error);
    return ok;
}

void Tick(Run& run) {
    Game::Tick(run.data, run.registry, run.level, 0.0f, kStep);
}

// A shot put where a test needs it. Portals::Shoot aims from the player and is
// gated by two cooldowns; what this suite is about is the POLL, so the flight is
// placed directly rather than aimed.
void PutShotAt(Run& run, const glm::dvec2& atPx) {
    Portals::Flight fired;
    fired.fromPx = atPx;
    fired.toPx = atPx;
    fired.atPx = atPx;
    run.level.portals.flight = fired;
}

bool WallBodyAlive(const Run& run) {
    for (const Demolish::Breakable& breakable : run.level.demolish.breakables) {
        if (breakable.name != run.level.torch.wall.name) continue;
        return breakable.body != entt::null && run.registry.valid(breakable.body);
    }
    return false;
}

void theRulesAndWhatTheLevelsHold() {
    Run run;
    if (!Begin(kWalled, run)) return;

    const Torch::Rules& rules = run.level.torch.rules;
    CHECK(::test::nearly(static_cast<float>(rules.reachPx), 24.0f));
    CHECK(::test::nearly(static_cast<float>(rules.wallFadeMs), 1500.0f));
    CHECK(::test::nearly(static_cast<float>(rules.orbitPx), 18.0f));
    CHECK(::test::nearly(static_cast<float>(rules.spinDegPerSec), 300.0f));
    CHECK_MSG(rules.wallName == "light_wall.ent", "the wall, found by name because it has no role");

    CHECK_EQ(run.level.torch.lights.size(), static_cast<std::size_t>(1));
    CHECK_EQ(run.level.torch.Unlit(), static_cast<std::size_t>(1));
    CHECK_MSG(run.level.torch.wall.present, "level25c ships a light wall");
    CHECK_MSG(!run.level.torch.wall.gone, "which is standing");
    CHECK_MSG(WallBodyAlive(run), "and Demolish holds a body for it, because it is metadata/breakable");

    Run bare;
    if (!Begin(kWallless, bare)) return;
    CHECK_EQ(bare.level.torch.lights.size(), static_cast<std::size_t>(1));
    CHECK_MSG(!bare.level.torch.wall.present, "level21c has a torch and NO wall");
}

// The switch, and that it costs the shot.
void aShotLightsATorchAndIsSpent() {
    Run run;
    if (!Begin(kWalled, run)) return;
    if (run.level.torch.lights.empty()) return;

    const glm::dvec2 torchPx = run.level.torch.lights[0].atPx;

    // Well outside scale(24): nothing happens, which is what says the radius is
    // doing the work rather than the mere presence of a shot.
    PutShotAt(run, torchPx + glm::dvec2(120.0, 0.0));
    Tick(run);
    CHECK_MSG(run.level.torch.lit == 0, "a shot 120 px away lights nothing");

    PutShotAt(run, torchPx + glm::dvec2(8.0, 0.0));
    Tick(run);
    CHECK_MSG(run.level.torch.lit == 1, "and one 8 px away lights it");
    CHECK_MSG(run.level.torch.lights[0].lit, "that torch in particular");
    CHECK_MSG(!run.level.portals.flight.has_value(), "killProjectile: the shot is spent, not flying on");
    CHECK_MSG(run.level.torch.Unlit() == static_cast<std::size_t>(0), "and it was the level's only one");
}

// The 1500 ms, which is load-bearing rather than a flourish: it is the window in
// which the wall can still be FOUND, and so the reason a signal exists at all.
void theWallStandsForASecondAndAHalf() {
    Run run;
    if (!Begin(kWalled, run)) return;
    if (run.level.torch.lights.empty() || !run.level.torch.wall.present) return;

    PutShotAt(run, run.level.torch.lights[0].atPx);
    Tick(run);
    CHECK_MSG(run.level.torch.wall.going, "the wall is flagged");
    CHECK_MSG(!run.level.torch.wall.gone, "and still standing on the frame it was flagged");
    CHECK_MSG(WallBodyAlive(run), "its body is still in the registry");
    CHECK_MSG(run.level.torch.signal.present, "and a signal appeared, because the wall could still be found");

    // 1400 ms in - 84 ticks - it is still there.
    for (int tick = 0; tick < 83; ++tick) Tick(run);
    CHECK_MSG(!run.level.torch.wall.gone, "still standing at 1.4 s");
    CHECK_MSG(WallBodyAlive(run), "and still solid");

    // And past 1500 ms it goes, body and all.
    for (int tick = 0; tick < 12; ++tick) Tick(run);
    CHECK_MSG(run.level.torch.wall.gone, "gone after 1.5 s");
    CHECK_EQ(run.level.torch.wallsGone, 1);
    CHECK_MSG(!WallBodyAlive(run), "and its body left the registry - the level is passable");

    // Demolish was told, rather than having a body destroyed behind its back.
    for (const Demolish::Breakable& breakable : run.level.demolish.breakables) {
        if (breakable.name != run.level.torch.wall.name) continue;
        CHECK_MSG(breakable.broken, "Demolish knows the wall it lost");
        CHECK_MSG(breakable.body == entt::null, "and is not still holding an invalid body");
    }
}

// The signal orbits rather than sitting still, and shooting it undoes everything.
void shootingTheSignalPutsTheWallBack() {
    Run run;
    if (!Begin(kWalled, run)) return;
    if (run.level.torch.lights.empty() || !run.level.torch.wall.present) return;

    const glm::dvec2 torchPx = run.level.torch.lights[0].atPx;
    PutShotAt(run, torchPx);
    Tick(run);
    if (!run.level.torch.signal.present) return;

    // It circles its torch at scale(18) rather than standing on it.
    const double gap = std::sqrt((run.level.torch.signal.atPx.x - torchPx.x) *
                                     (run.level.torch.signal.atPx.x - torchPx.x) +
                                 (run.level.torch.signal.atPx.y - torchPx.y) *
                                     (run.level.torch.signal.atPx.y - torchPx.y));
    CHECK_MSG(std::fabs(gap - run.level.torch.rules.orbitPx) < 1.0,
              "the signal circles at 18 px: " + std::to_string(static_cast<int>(gap)));
    const glm::dvec2 first = run.level.torch.signal.atPx;
    for (int tick = 0; tick < 20; ++tick) Tick(run);
    CHECK_MSG(std::fabs(run.level.torch.signal.atPx.x - first.x) > 1.0 ||
                  std::fabs(run.level.torch.signal.atPx.y - first.y) > 1.0,
              "and it MOVES, which is what makes it a shot worth aiming");

    // Let the wall finish going, so the restore is a real rebuild.
    for (int tick = 0; tick < 100; ++tick) Tick(run);
    CHECK_MSG(run.level.torch.wall.gone, "the wall went");
    CHECK_MSG(!WallBodyAlive(run), "and its body with it");

    PutShotAt(run, run.level.torch.signal.atPx);
    Tick(run);
    CHECK_MSG(!run.level.torch.signal.present, "the signal was shot out");
    CHECK_MSG(!run.level.torch.lights[0].lit, "its torch is unlit again");
    CHECK_MSG(run.level.torch.lights[0].switched, "and carries the original's `switch`");
    CHECK_EQ(run.level.torch.putOut, 1);
    CHECK_MSG(!run.level.torch.wall.gone, "the wall is back");
    CHECK_EQ(run.level.torch.wallsBack, 1);
    CHECK_MSG(WallBodyAlive(run), "and it is SOLID again, rebuilt into the registry");
    CHECK_MSG(!run.level.portals.flight.has_value(), "that shot was spent too");
}

// A fireball works a switch as readily as a portal shot: hasProjectileAround is
// called with includeFireballs set, which is the third place the fire diamond's
// machinery turns out to matter.
void aFireballWorksASwitchToo() {
    Run run;
    if (!Begin(kWalled, run)) return;
    if (run.level.torch.lights.empty()) return;

    Turrets::Fireball ball;
    ball.name = "test_fireball";
    ball.atPx = run.level.torch.lights[0].atPx;
    ball.velocityPx = glm::dvec2(0.0);
    ball.killsPlayer = false;
    run.level.turrets.fireballs.push_back(ball);

    Tick(run);
    CHECK_MSG(run.level.torch.lit == 1, "a fireball lights a torch");
    CHECK_MSG(run.level.turrets.fireballs.empty(), "and is spent doing it");
}

// The census, over the ten levels that place one.
void everyTorchLevelFindsThem() {
    const struct Level {
        const char* name;
        bool wall;
    } levels[] = {{"level21c", false}, {"level22c", false}, {"level23c", false}, {"level24c", false},
                  {"level25c", true},  {"level26c", true},  {"level28c", true},  {"level29c", true},
                  {"level30c", true},  {"level31c", true}};

    std::size_t torches = 0;
    int walls = 0;
    for (const Level& one : levels) {
        Run run;
        if (!Begin(one.name, run)) continue;
        CHECK_MSG(run.level.torch.lights.size() == static_cast<std::size_t>(1),
                  std::string(one.name) + " places one torch");
        torches += run.level.torch.lights.size();
        CHECK_MSG(run.level.torch.wall.present == one.wall,
                  std::string(one.name) + (one.wall ? " ships a light wall" : " ships none"));
        if (run.level.torch.wall.present) ++walls;
    }
    CHECK_EQ(torches, static_cast<std::size_t>(10));
    CHECK_EQ(walls, 6);
    std::printf("  %zu torch(es) across 10 levels, and %d light wall(s)\n", torches, walls);
}

void runTests() {
    theRulesAndWhatTheLevelsHold();
    aShotLightsATorchAndIsSpent();
    theWallStandsForASecondAndAHalf();
    shootingTheSignalPutsTheWallBack();
    aFireballWorksASwitchToo();
    everyTorchLevelFindsThem();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_torch SKIPPED - needs the converted levels at %s and the remake's data at %s\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_torch", 40);
}
