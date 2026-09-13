// Chapter 2's boss: the dragon of level 2-32, and the claw that eats its floor.
//
// WHAT THIS SUITE IS REALLY FOR. The inventory (test_mp_start) counts ROLES, and
// the four names the claw destroys have none - they are scenery in
// entity_roles.json. So every one of them could stand untouched for the whole
// level and test_mp_start would still report level31a as playing, on the strength
// of the boss_spawn nodes alone. Only a suite that watches the BODIES go can tell
// a level whose floor really collapses from one where the claw is drawn and
// inert. That is the blindness light_wall.ent had before test_mp_torch, and the
// reason test_mp_bounce watches a body rather than a position.
//
// THE SECOND THING IT PINS IS THE CAMERA, which is this boss's real novelty.
// level31a is the only level in the game that sets auto_camera, and it sets it to
// the dragon: the camera hard-locks to the dragon's x, and the claw's two
// margins are measured from that left edge. Get the clamp wrong at the end of the
// level and the claw would eat the ground under the exit; get the corner-versus-
// centre reading wrong and every margin would sit half a view out. Both are
// asserted below against the numbers level31a actually ships.
//
// Reads the converted levels from outside this repository, and skips, saying
// where it looked, when they are absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Dragon.hpp"
#include "sim/Game.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"
#include "sim/Units.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using namespace MagicPortals;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
const std::string kPortData = MAGICPORTALS_PORT_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

// The level the dragon is in, and the numbers level31a.tscn itself carries.
const std::string kLevel = "level31a";
constexpr double kDragonX = -164.0; // off the left edge
constexpr double kDragonY = -84.0;
constexpr double kTakeOffX = 2160.0;
constexpr double kBoundsX = 2632.0;
constexpr double kExitX = 2554.0;

bool Open(const std::string& name, Game::Data& data, entt::registry& registry, Game::Level& level) {
    const std::filesystem::path prisms = std::filesystem::temp_directory_path() / "supersonic-test-mp-dragon";
    std::string error;
    if (!Game::LoadData(kLevels + "/" + name + ".tscn", kData, prisms, data, error) ||
        !Game::Start(data, registry, level, error)) {
        CHECK_MSG(false, name + ": " + error);
        return false;
    }
    return true;
}

void TheRulesRead() {
    Dragon::Rules rules;
    std::string error;
    const bool ok = Dragon::LoadRules(kPortData + "/dragon.json", rules, error);
    CHECK_MSG(ok, error);
    if (!ok) return;

    CHECK(rules.entityName == "dragon.ent");
    CHECK(rules.knightSpawnName == "dragon_knight_spawn");
    CHECK(rules.takeOffName == "take_off");
    CHECK(::test::nearly(static_cast<float>(rules.advancePxS), 45.0f));
    CHECK(::test::nearly(static_cast<float>(rules.climbPxS), -60.0f));
    CHECK(::test::nearly(static_cast<float>(rules.sinkPxS), 25.0f));
    CHECK(::test::nearly(static_cast<float>(rules.fireIntervalMs), 4000.0f));
    CHECK(::test::nearly(static_cast<float>(rules.muzzlePx), 95.0f));

    // SIX waypoints, which is the reading this decode had to be corrected to.
    // Five would leave the claw resting mid-swing: the callback parks it on
    // getNumWaypoints() - 2 and relies on the last two coinciding.
    CHECK_EQ(rules.holdMs.size(), std::size_t{6});
    CHECK_EQ(rules.pathPx.size(), std::size_t{6});
    CHECK(rules.pathPx[4] == rules.pathPx[5]);
    double swingMs = 0.0;
    for (const double hold : rules.holdMs) swingMs += hold;
    CHECK(::test::nearly(static_cast<float>(swingMs), 1500.0f));
    std::printf("  a %zu-point swing of %.0f ms, sweeping every %.0f ms\n", rules.holdMs.size(), swingMs,
                rules.scanMs);

    CHECK(::test::nearly(static_cast<float>(rules.scanMs), 250.0f));
    CHECK(::test::nearly(static_cast<float>(rules.armPx), 94.0f));
    CHECK(::test::nearly(static_cast<float>(rules.crushPx), 64.0f));
    // The crush line inside the arm line, or it would destroy what it never
    // swung at. LoadRules refuses the other way round.
    CHECK(rules.crushPx <= rules.armPx);
    CHECK_EQ(rules.crushes.size(), std::size_t{4});

    // And the one number from outside the sim.
    CHECK(::test::nearly(static_cast<float>(rules.viewWidthPx), 455.0f));
}

// Refused, each for a reason a level could actually have.
void TheRulesAreRefusedWhenTheyDoNotMakeADragon() {
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "supersonic-test-mp-dragon-rules";
    std::error_code ec;
    std::filesystem::create_directories(scratch, ec);

    const struct Case {
        const char* name;
        const char* body;
    } cases[] = {
        {"five waypoints",
         R"({"flight":{"entity":"d","take_off":"t","advance_px_s":45,"climb_px_s":-60,"sink_px_s":25,
             "hover_ceiling_px":48,"bob_amplitude_px":13,"bob_frequency":0.1},
             "fire":{"interval_ms":4000,"muzzle_px":95},"knight":{"spawn":"k"},
             "camera":{"view_width_px":455},
             "claw":{"entity":"c","scan_ms":250,"arm_px":94,"crush_px":64,"lead_px":-100,"lift_px":72,
             "bob_amplitude_px":13,"bob_frequency":0.06,"hold_ms":[600,200,200,500,0],
             "path_px":[[0,0],[140,0],[120,40],[100,80],[0,0]],"angles_deg":[40,0,0,-20,-15],
             "crushes":["a"]}})"},
        {"a crush line outside the arm line",
         R"({"flight":{"entity":"d","take_off":"t","advance_px_s":45,"climb_px_s":-60,"sink_px_s":25,
             "hover_ceiling_px":48,"bob_amplitude_px":13,"bob_frequency":0.1},
             "fire":{"interval_ms":4000,"muzzle_px":95},"knight":{"spawn":"k"},
             "camera":{"view_width_px":455},
             "claw":{"entity":"c","scan_ms":250,"arm_px":64,"crush_px":94,"lead_px":-100,"lift_px":72,
             "bob_amplitude_px":13,"bob_frequency":0.06,"hold_ms":[600,200,200,500,0,0],
             "path_px":[[0,0],[140,0],[120,40],[100,80],[0,0],[0,0]],"angles_deg":[40,0,0,-20,-15,-15],
             "crushes":["a"]}})"},
        {"a dragon that never advances",
         R"({"flight":{"entity":"d","take_off":"t","advance_px_s":0,"climb_px_s":-60,"sink_px_s":25,
             "hover_ceiling_px":48,"bob_amplitude_px":13,"bob_frequency":0.1},
             "fire":{"interval_ms":4000,"muzzle_px":95},"knight":{"spawn":"k"},
             "camera":{"view_width_px":455},
             "claw":{"entity":"c","scan_ms":250,"arm_px":94,"crush_px":64,"lead_px":-100,"lift_px":72,
             "bob_amplitude_px":13,"bob_frequency":0.06,"hold_ms":[600,200,200,500,0,0],
             "path_px":[[0,0],[140,0],[120,40],[100,80],[0,0],[0,0]],"angles_deg":[40,0,0,-20,-15,-15],
             "crushes":["a"]}})"},
        {"a claw with nothing it may take",
         R"({"flight":{"entity":"d","take_off":"t","advance_px_s":45,"climb_px_s":-60,"sink_px_s":25,
             "hover_ceiling_px":48,"bob_amplitude_px":13,"bob_frequency":0.1},
             "fire":{"interval_ms":4000,"muzzle_px":95},"knight":{"spawn":"k"},
             "camera":{"view_width_px":455},
             "claw":{"entity":"c","scan_ms":250,"arm_px":94,"crush_px":64,"lead_px":-100,"lift_px":72,
             "bob_amplitude_px":13,"bob_frequency":0.06,"hold_ms":[600,200,200,500,0,0],
             "path_px":[[0,0],[140,0],[120,40],[100,80],[0,0],[0,0]],"angles_deg":[40,0,0,-20,-15,-15],
             "crushes":[]}})"},
    };

    int refused = 0;
    for (const Case& one : cases) {
        const std::filesystem::path file = scratch / "dragon.json";
        {
            std::ofstream out(file, std::ios::binary);
            out << one.body;
        }
        Dragon::Rules rules;
        std::string error;
        const bool ok = Dragon::LoadRules(file.string(), rules, error);
        CHECK_MSG(!ok, std::string("refused: ") + one.name);
        if (!ok) ++refused;
    }
    CHECK_EQ(refused, 4);
}

// Two entity names answer true, and no other boss's does.
void ItAnswersForItsOwnTwoNodesAndNoOthers() {
    Dragon::Rules rules;
    std::string error;
    if (!Dragon::LoadRules(kPortData + "/dragon.json", rules, error)) return;

    const struct Case {
        const char* level;
        int expected; // boss_spawn nodes this dragon claims
    } cases[] = {
        {"level31a", 2}, // dragon.ent and dragon_knight_spawn
        {"level31", 0},  // the beholder's adder
        {"level31b", 0}, // the ghost
        {"level31c", 0}, // the dark dragon, which is NOT built
    };

    for (const Case& one : cases) {
        Tscn::Scene scene;
        Roles::Table roles;
        if (!Tscn::Load(kLevels + "/" + one.level + ".tscn", scene, error)) {
            CHECK_MSG(false, error);
            continue;
        }
        if (!Roles::Load(kData + "/entity_roles.json", roles, error)) {
            CHECK_MSG(false, error);
            continue;
        }
        int claimed = 0;
        int spawns = 0;
        for (const Tscn::Node& node : scene.nodes) {
            if (node.parent != "." || Roles::RoleOf(roles, node) != Roles::kBossSpawn) continue;
            ++spawns;
            if (Dragon::Plays(rules, node)) ++claimed;
        }
        CHECK_MSG(claimed == one.expected, std::string(one.level) + " has " + std::to_string(claimed) +
                                               " node(s) this dragon claims, of " + std::to_string(spawns) +
                                               " boss spawn(s)");
    }

    // And the role itself is still not blanket-played: the dark dragon has to
    // stay inert, and IsPorted is per-role and cannot say "for three of four".
    CHECK(!Roles::IsPorted(Roles::kBossSpawn));
}

void TheLevelPlacesItWhereTheFileSaysIt() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kLevel, data, registry, level)) return;

    CHECK(level.dragon.present);
    if (!level.dragon.present) return;
    CHECK(::test::nearly(static_cast<float>(level.dragon.virtualPx.x), static_cast<float>(kDragonX)));
    CHECK(::test::nearly(static_cast<float>(level.dragon.virtualPx.y), static_cast<float>(kDragonY)));
    CHECK(level.dragon.hasTakeOff);
    CHECK(::test::nearly(static_cast<float>(level.dragon.takeOffPx.x), static_cast<float>(kTakeOffX)));
    CHECK(level.dragon.hasClaw);
    CHECK(::test::nearly(static_cast<float>(level.dragon.boundsX), static_cast<float>(kBoundsX)));
    CHECK(!level.dragon.tookOff);

    // The claw idles motionless, parked where setCurrentWaypoint(n - 2) puts it.
    // Asserted as the LISTING states it rather than as "already armed": the two
    // coincide from the first tick onward, and the first sweep is 250 ms away, so
    // pinning the decoded index is the claim that can actually be checked.
    CHECK_EQ(level.dragon.claw.waypoint, static_cast<int>(level.dragon.rules.holdMs.size()) - 2);
    CHECK(!level.dragon.crushables.empty());
    Game::Tick(data, registry, level, 0.0f, kStep);
    CHECK(level.dragon.claw.IsLastFrame(level.dragon.rules));

    // Twenty-two platforms it may take, counted from the level rather than
    // assumed - and every one of them a body that was actually built.
    CHECK_EQ(level.dragon.crushables.size(), std::size_t{22});
    for (const Dragon::Crushable& one : level.dragon.crushables) {
        CHECK_MSG(registry.valid(one.entity), one.name + " was built");
    }
    std::printf("  %s: the dragon at (%.0f, %.0f), take_off at %.0f, %zu platform(s) the claw may take\n",
                kLevel.c_str(), level.dragon.virtualPx.x, level.dragon.virtualPx.y, level.dragon.takeOffPx.x,
                level.dragon.crushables.size());

    // And no other level in the game has one.
    for (const char* other : {"level30a", "level31", "level31b", "level31c"}) {
        Game::Data second;
        entt::registry secondRegistry;
        Game::Level secondLevel;
        if (!Open(other, second, secondRegistry, secondLevel)) continue;
        CHECK_MSG(!secondLevel.dragon.present, std::string(other) + " places no dragon");
    }
}

// The camera is the whole of the claw's frame of reference, so it is pinned on
// its own before anything that depends on it.
void TheCameraIsTheDragonHeldInsideTheLevel() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kLevel, data, registry, level)) return;
    if (!level.dragon.present) return;

    // It starts OFF the left edge, so the camera is clamped to 0 and stays there
    // until the dragon reaches the level. camMin is (0,0).
    CHECK(level.dragon.atPx.x < 0.0);
    CHECK(::test::nearly(static_cast<float>(level.dragon.CameraLeftPx().x), 0.0f));
    // And y is pinned to 0, which is the whole of the vertical: the level is one
    // view tall.
    CHECK(::test::nearly(static_cast<float>(level.dragon.CameraLeftPx().y), 0.0f));

    // Ten seconds in, the dragon has advanced 45 px a second and the camera is
    // with it.
    for (int tick = 0; tick < 600; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);
    const double flown = level.dragon.virtualPx.x - kDragonX;
    CHECK(flown > 440.0 && flown < 460.0);
    CHECK(::test::nearly(static_cast<float>(level.dragon.CameraLeftPx().x),
                         static_cast<float>(level.dragon.atPx.x)));
    std::printf("  after 10 s: the dragon at x %.1f, the camera's left edge at %.1f\n", level.dragon.atPx.x,
                level.dragon.CameraLeftPx().x);

    // And the clamp at the far end: the camera never shows past the level.
    level.dragon.atPx.x = kBoundsX + 1000.0;
    const double most = kBoundsX - level.dragon.viewWidthPx;
    CHECK(::test::nearly(static_cast<float>(level.dragon.CameraLeftPx().x), static_cast<float>(most)));
    // Which is what keeps the exit out of the claw's reach - the assertion the
    // whole level rests on.
    CHECK(most + level.dragon.rules.crushPx < kExitX);
}

// THE ONE THAT MATTERS: the bodies go.
void TheClawTakesTheFloorAndTheRegistryAgrees() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kLevel, data, registry, level)) return;
    if (!level.dragon.present) return;

    // Nothing has gone before the dragon reaches the level.
    CHECK_EQ(level.dragon.crushed, 0);

    const std::size_t placed = level.dragon.crushables.size();
    // Thirty seconds: the dragon is at x ~1186 by then, so the destroy line has
    // swept most of the level.
    for (int tick = 0; tick < 1800; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);

    CHECK(level.dragon.crushed > 0);
    std::printf("  after 30 s: %d of %zu platform(s) taken, the camera's left edge at %.1f\n", level.dragon.crushed,
                placed, level.dragon.CameraLeftPx().x);

    const double line = level.dragon.CameraLeftPx().x + level.dragon.rules.crushPx;
    // The sweep runs on a 250 ms clock of its own, not every frame, so the line
    // can have moved a sweep's worth of travel past a platform that is still
    // standing and perfectly correct. That slack is the claw's own cadence -
    // 45 px a second for a quarter second - and not a tolerance for being wrong.
    const double sweptPx = level.dragon.rules.advancePxS * level.dragon.rules.scanMs / 1000.0;
    int gone = 0;
    for (const Dragon::Crushable& one : level.dragon.crushables) {
        if (!one.gone) {
            // What stands, stands because it is beyond the destroy line. This is
            // the check that would catch a claw eating the level from the wrong
            // end.
            CHECK_MSG(one.atPx.x >= line - sweptPx - 1.0,
                      one.name + " stands, and is no more than one sweep behind the destroy line");
            continue;
        }
        ++gone;
        // What went, went because it was behind the line - and its BODY went with
        // it. A claw that only set a flag would pass every count above.
        CHECK_MSG(one.atPx.x < line + 1.0, one.name + " went, and was behind the destroy line");
        CHECK_MSG(one.entity == entt::null, one.name + " was dropped from the claw's list");
    }
    CHECK_EQ(gone, level.dragon.crushed);

    // And the level's own map of built bodies no longer names them, so nothing
    // else in the port can reach a destroyed entity through it.
    for (const Dragon::Crushable& one : level.dragon.crushables) {
        if (!one.gone) continue;
        CHECK_MSG(level.built.entities.find(one.name) == level.built.entities.end(),
                  one.name + " is gone from the built level");
    }
}

// The exit's ground survives the whole level, which is what makes 2-32 winnable.
void TheExitIsNeverEaten() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kLevel, data, registry, level)) return;
    if (!level.dragon.present) return;

    // A full minute: longer than the dragon's whole run. It reaches take_off at
    // (2160 + 164) / 45 = 51.6 s.
    for (int tick = 0; tick < 3600; ++tick) Game::Tick(data, registry, level, 0.0f, kStep);

    CHECK(level.dragon.tookOff);
    std::printf("  after 60 s: took off %s, %d platform(s) taken in all\n", level.dragon.tookOff ? "yes" : "no",
                level.dragon.crushed);

    for (const Dragon::Crushable& one : level.dragon.crushables) {
        if (!one.gone) continue;
        CHECK_MSG(one.atPx.x < kExitX, one.name + " went, and stood short of the exit");
    }
}

// It fires at the player, and only when it can see one.
void ItSpitsAtWhatItCanSee() {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
    if (!Open(kLevel, data, registry, level)) return;
    if (!level.dragon.present) return;

    const glm::dvec2 playerPx = Units::ToPixels(registry.get<TransformComponent>(level.player).position);

    // Put it just above the player with its clock already past the interval, and
    // one tick should produce one fireball. Set through virtualPx, because the
    // drawn position is derived from it every tick.
    const std::size_t before = level.turrets.fireballs.size();
    level.dragon.virtualPx = playerPx + glm::dvec2(0.0, -56.0);
    level.dragon.elapsedMs = level.dragon.rules.fireIntervalMs + 1.0;
    Game::Tick(data, registry, level, 0.0f, kStep);

    CHECK(level.turrets.fireballs.size() > before);
    CHECK(level.dragon.fired > 0);
    if (level.turrets.fireballs.size() > before) {
        const Turrets::Fireball& made = level.turrets.fireballs.back();
        // A carranca's kills and a fire diamond's does not; this one kills.
        CHECK(made.killsPlayer);
        CHECK(made.name.rfind(level.dragon.name, 0) == 0);
        // Aimed at the player, and started a muzzle's length along that aim.
        const double speed = std::sqrt(made.velocityPx.x * made.velocityPx.x +
                                       made.velocityPx.y * made.velocityPx.y);
        CHECK(::test::nearly(static_cast<float>(speed), static_cast<float>(level.turrets.rules.speedPx)));
        CHECK(made.velocityPx.y > 0.0); // the player is below it
        std::printf("  a fireball from %.0f px above the player, at %.0f px/s\n", 56.0, speed);
    }
    // And its clock restarted, so the next one is an interval away.
    CHECK(level.dragon.elapsedMs < level.dragon.rules.fireIntervalMs);

    // AND THE GATE REFUSES. Put it under the floor the player is standing on,
    // with its clock past the interval, and the shot must not happen - otherwise
    // the check above is a rubber stamp that would pass for a dragon that fires
    // through the world.
    {
        const std::size_t before2 = level.turrets.fireballs.size();
        const int fired2 = level.dragon.fired;
        level.dragon.virtualPx = playerPx + glm::dvec2(0.0, 200.0);
        level.dragon.elapsedMs = level.dragon.rules.fireIntervalMs + 1.0;
        Game::Tick(data, registry, level, 0.0f, kStep);
        CHECK_EQ(level.turrets.fireballs.size(), before2);
        CHECK_EQ(level.dragon.fired, fired2);
        std::printf("  and refused the shot with the floor in the way\n");
    }

    // Once it has taken off it stops, which is what ends its part of the level.
    level.dragon.virtualPx = playerPx + glm::dvec2(0.0, -56.0);
    level.dragon.tookOff = true;
    level.dragon.elapsedMs = level.dragon.rules.fireIntervalMs + 1.0;
    const std::size_t after = level.turrets.fireballs.size();
    Game::Tick(data, registry, level, 0.0f, kStep);
    CHECK_EQ(level.turrets.fireballs.size(), after);
}

void runTests() {
    TheRulesRead();
    TheRulesAreRefusedWhenTheyDoNotMakeADragon();
    ItAnswersForItsOwnTwoNodesAndNoOthers();
    TheLevelPlacesItWhereTheFileSaysIt();
    TheCameraIsTheDragonHeldInsideTheLevel();
    TheClawTakesTheFloorAndTheRegistryAgrees();
    TheExitIsNeverEaten();
    ItSpitsAtWhatItCanSee();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_dragon: SKIPPED - needs the converted levels at %s\n"
                    "  and the remake's data at %s.\n"
                    "  Both live outside this repository; configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=... and -DSUPERSONIC_MAGICPORTALS_DATA=...\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_dragon", 60);
}
