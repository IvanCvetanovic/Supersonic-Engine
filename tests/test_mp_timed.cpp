// Timed crystals: the inline `crystal` with a `time`, which goes that long after
// the level starts (the remake's TimedCollectible, behaviours.gd:209-230).
//
// What the files say is pinned: which crystals are timed and for how long,
// chapter 1's eight of them. What happens is judged by threshold, a few ticks
// either side of the time, since a tick is 1/60 s. The exit switch the remake
// marks UNVERIFIED is run both ways, because a gone crystal counts against it.
//
// And the clock the original shows on the dial behind each (art.json timer,
// ETHCallback_timer): the cell of the time elapsed, the pulse whose leg shortens
// as the time runs out, and the shrink once it is up. Pure arithmetic on the
// port's committed data, and on Goals' own clock.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "sim/Art.hpp"
#include "sim/Game.hpp"
#include "sim/Goals.hpp"

#include <algorithm>
#include <cmath>
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

// ---- The dial (art.json timer) ------------------------------------------------

bool LoadTimer(Art::Timer& out) {
    Art::Rules rules;
    std::string error;
    const bool ok = Art::LoadRules(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/art.json", rules, error);
    CHECK_MSG(ok, error);
    out = rules.timer;
    return ok;
}

void TheDialsNumbersAreTheScripts() {
    Art::Timer timer;
    if (!LoadTimer(timer)) return;
    CHECK_MSG(timer.frames == 8, "ins 238 and 247: eight cells, 0 to 7");
    CHECK_MSG(timer.alpha == 0.5, "addTimerToCrystal ins 100-102: SetAlpha(0.5f)");
    CHECK_MSG(timer.zOffset == -2, "addTimerToCrystal ins 12-25: 2 behind its crystal");
    CHECK_MSG(timer.pulseFrom == 1.0 && timer.pulseTo == 1.15, "ins 211-222: V2_ONE to (1.15, 1.15)");
    CHECK_MSG(timer.pulseMinLegMs == 400.0, "ins 204-208: max(400, the time left)");
    CHECK_MSG(timer.shrinkPerFrame == 0.9 && timer.goneBelowScale == 0.1, "ins 105-199: x 0.9 a frame, gone below 0.1");
    CHECK_MSG(timer.decayFramesPerSecond == 60.0, "a frame of 1/60 s (00_order K2)");
    CHECK_MSG(timer.screenPxPerUnit == 720.0 / 256.0,
              "bounce ins 90-102 and SGlobalScale::scale: stored x m_scaleFactor, 720 / 256 (R12, 00_order 8.12a)");
}

void TheDialShowsTheTimeElapsed() {
    Art::Timer timer;
    if (!LoadTimer(timer)) return;
    // int(float(elapsed) / float(time) x 8f), clamped to 0..7, for every time the
    // levels carry (3,900 to 25,000 ms): each cell turns on its own millisecond.
    int wrong = 0;
    std::string first;
    for (const double time : {3900.0, 4000.0, 5000.0, 6000.0, 9000.0, 10000.0, 11100.0, 11500.0, 12000.0, 25000.0}) {
        const auto expect = [&](double elapsed, int frame) {
            const int got = timer.FrameAt(elapsed, time);
            if (got == frame) return;
            if (wrong++ == 0) {
                first = "time " + std::to_string(time) + " at " + std::to_string(elapsed) + ": " +
                        std::to_string(got) + ", not " + std::to_string(frame);
            }
        };
        expect(0.0, 0);
        expect(time / 8.0 - 1.0, 0);
        expect(time / 8.0, 1);
        expect(time - 1.0, 7);
        for (int k = 1; k < 8; ++k) {
            expect(k * time / 8.0 - 1.0, k - 1);
            expect(k * time / 8.0, k);
        }
        // Past the time the script stops setting a cell; clamped, it would show 7.
        expect(time, 7);
        expect(time * 2.0, 7);
    }
    CHECK_MSG(wrong == 0, std::to_string(wrong) + " cells wrong; the first " + first);

    // On Goals' own clock, which the layer reads: level26's 10 s crystal shows
    // cell (tick x 8) / 600 on every tick it lives, and goes on tick 600.
    Run run;
    if (!Begin("level26", run)) return;
    const Goals::Crystal* crystal = run.level.goals.FindCrystal("crystal_1264");
    CHECK_MSG(crystal != nullptr && crystal->timed && crystal->lifeS == 10.0, "level26's crystal_1264, 10 s");
    if (crystal == nullptr || !crystal->timed) return;
    int tick = 0;
    int off = 0;
    while (tick < 700) {
        Tick(run);
        ++tick;
        crystal = run.level.goals.FindCrystal("crystal_1264");
        if (crystal == nullptr || crystal->expired || crystal->collected) break;
        const int frame = timer.FrameAt((crystal->lifeS - crystal->leftS) * 1000.0, crystal->lifeS * 1000.0);
        if (frame != tick * 8 / 600) ++off;
    }
    CHECK_MSG(off == 0, std::to_string(off) + " ticks off the cell (tick x 8) / 600");
    CHECK_MSG(crystal != nullptr && crystal->expired && tick == 600,
              "expired on tick " + std::to_string(tick) + ", the 600th");
}

void TheDialPulsesFasterAsTheTimeRunsOut() {
    Art::Timer timer;
    if (!LoadTimer(timer)) return;
    // Each leg max(400, the time left).
    CHECK(timer.LegMs(0.0, 10000.0) == 10000.0);
    CHECK(timer.LegMs(9599.0, 10000.0) == 401.0);
    CHECK(timer.LegMs(9600.0, 10000.0) == 400.0);
    CHECK(timer.LegMs(9999.0, 10000.0) == 400.0);
    // bounce() with this moment's leg: the legs so far say which way, how far into
    // this one where, eased by smoothEnd.
    const auto bounce = [](double elapsed, double leg) {
        const double legs = std::floor(elapsed / leg);
        double bias = (elapsed - legs * leg) / leg;
        if (static_cast<long long>(legs) % 2 == 1) bias = 1.0 - bias;
        return 1.0 + 0.15 * std::sin(bias * 3.14159265358979 / 2.0);
    };
    int wrong = 0;
    int outside = 0;
    std::string first;
    for (const double time : {5000.0, 10000.0}) {
        for (double elapsed = 0.0; elapsed <= time; elapsed += 1.0) {
            const glm::dvec2 got = timer.PulseAt(elapsed, time);
            const double want = bounce(elapsed, std::max(400.0, time - elapsed));
            if (got.x != got.y || got.x < 1.0 - 1e-6 || got.x > 1.15 + 1e-6) ++outside;
            if (std::fabs(got.x - want) > 1e-5) {
                if (wrong++ == 0) first = std::to_string(elapsed) + " of " + std::to_string(time);
            }
        }
    }
    CHECK_MSG(wrong == 0, std::to_string(wrong) + " scales off bounce(); the first at " + first);
    CHECK_MSG(outside == 0, std::to_string(outside) + " scales outside 1..1.15, or not the same both ways");
    CHECK(std::fabs(timer.PulseAt(0.0, 10000.0).x - 1.0) < 1e-9);
    // Half its time gone, one leg is done: at its top.
    CHECK(std::fabs(timer.PulseAt(5000.0, 10000.0).x - 1.15) < 1e-6);
    // 9,800 ms in, legs of 400: 24 of them done, half into the 25th, rising.
    CHECK(std::fabs(timer.PulseAt(9800.0, 10000.0).x - (1.0 + 0.15 * std::sin(3.14159265358979 / 4.0))) < 1e-5);
}

void TheDialShrinksAwayOnceTheTimeIsUp() {
    Art::Timer timer;
    if (!LoadTimer(timer)) return;
    // x 0.9 a frame of 1/60 s, scale and alpha alike, gone once the scale the
    // original stores - the port's x m_scaleFactor - is below 0.1.
    CHECK(std::fabs(timer.DecayOver(1.0 / 60.0) - 0.9) < 1e-12);
    CHECK(std::fabs(timer.DecayOver(32.0 / 60.0) - std::pow(0.9, 32.0)) < 1e-12);
    // The frames to go: each multiplies, then tests, so the dial is drawn shrinking
    // on one fewer.
    const auto framesToGo = [&timer](double scale) {
        int frames = 0;
        do {
            scale *= timer.DecayOver(1.0 / 60.0);
            ++frames;
        } while (!timer.Gone(scale) && frames < 1000);
        return frames;
    };
    // The script's own arithmetic, in its floats: SetScale(GetScale() * 0.9f) on the
    // stored scale, then GetScale().x < 0.1f (ins 105-125, 184-199).
    const auto scriptFramesToGo = [](float pulse) {
        float stored = pulse * (720.0f / 256.0f);
        int frames = 0;
        do {
            stored *= 0.9f;
            ++frames;
        } while (!(stored < 0.1f) && frames < 1000);
        return frames;
    };
    CHECK_MSG(framesToGo(1.0) == 32, "from scale 1, gone on the 32nd frame: " + std::to_string(framesToGo(1.0)));
    CHECK_MSG(framesToGo(1.15) == 33, "from its pulse's top, the 33rd: " + std::to_string(framesToGo(1.15)));
    // level26's expiry (the last pulse 1.1497) and a pickup on tick 61 (1.0265),
    // either side of 0.1 / (2.8125 x 0.9^32) = 1.0355.
    CHECK_MSG(framesToGo(1.1497) == 33 && framesToGo(1.0265) == 32 && framesToGo(1.035) == 32 &&
                  framesToGo(1.036) == 33,
              "33 from 1.1497 and 1.036, 32 from 1.0265 and 1.035");
    int differ = 0;
    for (int milli = 1000; milli <= 1150; ++milli) {
        const double pulse = milli / 1000.0;
        if (framesToGo(pulse) != scriptFramesToGo(static_cast<float>(pulse))) ++differ;
    }
    CHECK_MSG(differ == 0, std::to_string(differ) + " pulses from 1 to 1.15 where the port's count is not the script's");
    // Not the pulse itself: a dial of 0.09 in the port's units is 0.253 stored.
    CHECK_MSG(!timer.Gone(0.09) && timer.Gone(0.035),
              "0.09 x 2.8125 stays, 0.035 x 2.8125 goes");
    CHECK_MSG(std::fabs(0.5 * std::pow(timer.DecayOver(1.0 / 60.0), 32.0) - 0.5 * std::pow(0.9, 32.0)) < 1e-9,
              "its alpha fading with it");
}

void runTests() {
    ChapterOnesEightTimedCrystals();
    ItGoesAtItsTime();
    ThePlayerCollectsItFirst();
    AGoneCrystalStillCountsAgainstTheExit();
    TheDialsNumbersAreTheScripts();
    TheDialShowsTheTimeElapsed();
    TheDialPulsesFasterAsTheTimeRunsOut();
    TheDialShrinksAwayOnceTheTimeIsUp();
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
    return ::test::summary("test_mp_timed", 60);
}
