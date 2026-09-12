// Carrancas: the wall gargoyles that spit fireballs, which the remake's role
// table calls turrets.
//
// What is pinned here is the DECODE, not the remake's reading of it, and the two
// differ in one place that matters. carrancaCallback seeds elapsedTime from the
// node's startStride, gathers each frame, fires when it passes the node's stride
// and resets to zero - so level0a's carranca, whose startStride is 0 and whose
// stride is 1500, first fires a stride in. The remake treats startStride as an
// initial delay and fires at once (hazards.gd:346-349). A test that only counted
// fireballs would pass either way, so the FIRST one is timed here.
//
// The fireball's own numbers are its .ent's: 130 px/s and a 16 x 16 sensor. The
// remake's hazards.json invents 180 px/s and marks itself _guess; the original
// states it, so the port does not inherit the guess.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Game.hpp"
#include "sim/Turrets.hpp"
#include "sim/Units.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;
using Supersonic::TransformComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
const std::string kPortData = MAGICPORTALS_PORT_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-turrets", run.data,
                                   error) &&
                    Game::Start(run.data, run.registry, run.level, error);
    CHECK_MSG(ok, levelName + ": " + error);
    return ok;
}

void Tick(Run& run, float direction = 0.0f) { Game::Tick(run.data, run.registry, run.level, direction, kStep); }

std::filesystem::path Scratch() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-mp-turrets";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string WriteRules(const char* name, const std::string& body) {
    const std::filesystem::path path = Scratch() / name;
    std::ofstream file(path, std::ios::trunc);
    file << body;
    return path.string();
}

// level0a is chapter 2's first level, and one of the ten whose only unported
// role was the carranca.
constexpr const char* kLevel = "level0a";

void TheCarrancasAreFound() {
    Run run;
    if (!Begin(kLevel, run)) return;

    const Turrets::State& turrets = run.level.turrets;
    CHECK_MSG(!turrets.turrets.empty(), "level0a carries at least one carranca");
    if (turrets.turrets.empty()) return;

    std::printf("  %s: %d carranca(s)\n", kLevel, static_cast<int>(turrets.turrets.size()));

    // carranca_right_ent_792: dirX 1.2, dirY 0, so it fires to the RIGHT, and
    // the direction is carried as a unit vector however the data spells it.
    bool sawRightward = false;
    for (const Turrets::Turret& turret : turrets.turrets) {
        const double length =
            std::sqrt(turret.directionPx.x * turret.directionPx.x + turret.directionPx.y * turret.directionPx.y);
        CHECK_MSG(::test::nearly(static_cast<float>(length), 1.0f), "every direction is a unit vector");
        CHECK_MSG(turret.strideMs > 0.0, "and every stride is a real one");
        if (turret.directionPx.x > 0.5) sawRightward = true;
    }
    CHECK_MSG(sawRightward, "level0a's carranca_right fires to the right, as its dirX of 1.2 says");
}

void TheFirstShotComesAStrideIn() {
    Run run;
    if (!Begin(kLevel, run)) return;
    if (run.level.turrets.turrets.empty()) return;

    // The shortest stride in the level decides when the first fireball can
    // appear at all: elapsedTime starts at startStride and has to PASS stride.
    double soonestMs = 0.0;
    for (const Turrets::Turret& turret : run.level.turrets.turrets) {
        const double waitMs = turret.strideMs - turret.elapsedMs;
        if (soonestMs == 0.0 || waitMs < soonestMs) soonestMs = waitMs;
    }
    CHECK_MSG(soonestMs > 0.0, "no carranca fires before the level has begun");

    // NOT at once. This is the whole difference from the remake's reading, and
    // a test that only counted fireballs would not see it.
    Tick(run);
    CHECK_MSG(run.level.turrets.fireballs.empty(), "nothing is spat on the first tick");

    const int ticksToFirst = static_cast<int>(soonestMs / (kStep * 1000.0f));
    for (int tick = 1; tick < ticksToFirst; ++tick) Tick(run);
    CHECK_MSG(run.level.turrets.fireballs.empty(),
              "and none until the stride has passed, got " + std::to_string(run.level.turrets.fireballs.size()));

    // Two more: one to pass the stride, and one because the level's carrancas
    // need not share it.
    Tick(run);
    Tick(run);
    CHECK_MSG(!run.level.turrets.fireballs.empty(), "and then one is");
    std::printf("  %s: the first fireball after %d tick(s); its stride is %.0f ms\n", kLevel, ticksToFirst + 1,
                soonestMs);
}

void AFireballFliesAtItsOwnSpeed() {
    Run run;
    if (!Begin(kLevel, run)) return;
    if (run.level.turrets.turrets.empty()) return;

    // Far enough for every carranca in the level to have fired.
    for (int tick = 0; tick < 240 && run.level.turrets.fireballs.empty(); ++tick) Tick(run);
    if (run.level.turrets.fireballs.empty()) {
        CHECK_MSG(false, "no carranca fired within four seconds");
        return;
    }

    const Turrets::Fireball before = run.level.turrets.fireballs.front();
    const glm::dvec2 wasAt = before.atPx;
    Tick(run);

    // The same fireball, if it is still there: found by name, because the tick
    // may have dropped others and fired more.
    for (const Turrets::Fireball& now : run.level.turrets.fireballs) {
        if (now.name != before.name) continue;
        const glm::dvec2 moved = now.atPx - wasAt;
        const double distance = std::sqrt(moved.x * moved.x + moved.y * moved.y);
        // 130 px/s over one sixtieth of a second.
        CHECK_MSG(::test::nearly(static_cast<float>(distance),
                                 static_cast<float>(run.level.turrets.rules.speedPx * kStep), 0.01f),
                  "a fireball travels its .ent's own 130 px/s");
        return;
    }
}

void AFireballLeavesTheLevel() {
    Run run;
    if (!Begin(kLevel, run)) return;
    if (run.level.turrets.turrets.empty()) return;

    // A minute of it. Fireballs are sensors, so nothing in the world stops one:
    // every one fired must leave by the level's edge, or they would pile up
    // without bound.
    for (int tick = 0; tick < 3600; ++tick) Tick(run);

    int fired = 0;
    for (const Turrets::Turret& turret : run.level.turrets.turrets) fired += turret.fired;
    CHECK_MSG(fired > 0, "a minute fires some");
    CHECK_MSG(static_cast<int>(run.level.turrets.fireballs.size()) < fired,
              "and they do not all still be in the air: " + std::to_string(run.level.turrets.fireballs.size()) +
                  " live of " + std::to_string(fired) + " fired");
    std::printf("  %s after a minute: %d fired, %d live\n", kLevel, fired,
                static_cast<int>(run.level.turrets.fireballs.size()));
}

void AFireballBurnsThePlayer() {
    Run run;
    if (!Begin(kLevel, run)) return;
    if (run.level.turrets.turrets.empty()) return;

    // Put the player exactly where the first carranca's mouth is. A fireball
    // spawns at the carranca's own position, so the player is in it at once -
    // which is also what proves the kill runs through Hazards, as the boss's
    // does, rather than through a second death of its own.
    const Turrets::Turret& turret = run.level.turrets.turrets.front();
    auto& transform = run.registry.get<TransformComponent>(run.level.player);
    const glm::vec3 at = Units::ToWorld(turret.atPx.x, turret.atPx.y);
    transform.position = glm::vec3(at.x, at.y, transform.position.z);

    const int ticks = static_cast<int>(turret.strideMs / (kStep * 1000.0f)) + 4;
    for (int tick = 0; tick < ticks && !run.level.hazards.playerDied; ++tick) {
        Tick(run);
        // Held there: the step would otherwise drop it out of the way.
        if (!run.level.hazards.playerDied) {
            auto& held = run.registry.get<TransformComponent>(run.level.player);
            held.position = glm::vec3(at.x, at.y, held.position.z);
        }
    }

    CHECK_MSG(run.level.turrets.playerKilled, "a fireball burns a player standing in it");
    CHECK_MSG(run.level.hazards.playerDied, "and it dies as a hazard kills it");
    CHECK_MSG(run.level.hazards.killedBy == run.level.turrets.killedBy,
              "named by the fireball, got '" + run.level.hazards.killedBy + "'");
    std::printf("  %s: the player was burned by %s\n", kLevel, run.level.hazards.killedBy.c_str());
}

void TheReaderRefusesWhatWouldBeSilent() {
    Turrets::Rules rules;
    std::string error;

    // The port's own file reads.
    CHECK_MSG(Turrets::LoadRules(kPortData + "/turrets.json", rules, error), kPortData + "/turrets.json: " + error);
    CHECK_MSG(::test::nearly(static_cast<float>(rules.speedPx), 130.0f),
              "fireball.ent's speed is 130, not the remake's invented 180");
    CHECK_MSG(::test::nearly(static_cast<float>(rules.hitPx.x), 16.0f) &&
                  ::test::nearly(static_cast<float>(rules.hitPx.y), 16.0f),
              "and its Collision is 16 x 16");

    // A fireball that never leaves the mouth, and a carranca that fires every
    // tick, would each look like a level that works.
    const std::string zeroSpeed = WriteRules("zero-speed.json", R"({
      "fireball": {"speed_px_s": 0.0, "hit_px": [16.0, 16.0]},
      "stride": {"default_ms": 2000.0},
      "cull": {"margin_px": [64.0, 64.0]}
    })");
    CHECK_MSG(!Turrets::LoadRules(zeroSpeed, rules, error), "a speed of nothing is refused");

    const std::string zeroStride = WriteRules("zero-stride.json", R"({
      "fireball": {"speed_px_s": 130.0, "hit_px": [16.0, 16.0]},
      "stride": {"default_ms": 0.0},
      "cull": {"margin_px": [64.0, 64.0]}
    })");
    CHECK_MSG(!Turrets::LoadRules(zeroStride, rules, error), "and so is a stride of nothing");

    const std::string noPair = WriteRules("no-pair.json", R"({
      "fireball": {"speed_px_s": 130.0, "hit_px": [16.0]},
      "stride": {"default_ms": 2000.0},
      "cull": {"margin_px": [64.0, 64.0]}
    })");
    CHECK_MSG(!Turrets::LoadRules(noPair, rules, error), "and a hit box that is not two numbers");
}

void runTests() {
    TheReaderRefusesWhatWouldBeSilent();
    TheCarrancasAreFound();
    TheFirstShotComesAStrideIn();
    AFireballFliesAtItsOwnSpeed();
    AFireballLeavesTheLevel();
    AFireballBurnsThePlayer();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_turrets SKIPPED - needs the converted levels at %s and the remake's data at %s\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_turrets", 20);
}
