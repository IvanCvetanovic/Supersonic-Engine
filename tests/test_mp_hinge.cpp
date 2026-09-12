// Seesaws and spinning platforms: the bodies the original pins to an anchor.
//
// The one Magic Portals role with no tick of its own. Hinge::Find attaches a
// JointComponent when the level is built and the engine's solver does the rest,
// so what is pinned here is the ARITHMETIC that turns the original's numbers into
// that component - and one thing about the engine, which is that a joint stops
// its two ends colliding (test_physics owns that check; without it a platform
// climbs out of the anchor it hangs from and none of this would hold).
//
// THE ANGLE IS THE PART THAT CAN SILENTLY BE WRONG. Box2D measures the joint from
// the pose the level was BUILT at, and the engine measures from where the two
// bodies' reference directions coincide, so the authored range is measured from
// the rest angle rather than used as it is. It is ADDED: a body turned about the
// engine's +z RAISES the hinge angle, so the engine's sense runs with the
// original's. That was briefly disbelieved on the strength of a reading taken
// three seconds in with gravity on - by then gravity had dragged the bar back
// past rest, which is indistinguishable from an inverted sign - so the direction
// is read SIX TICKS in here, before anything else can touch it.
//
// level30a is the only level that could say so. Its seesaw runs from -1.1325 to
// 0.4382; every other placement in the game is a symmetric quarter turn and would
// pass either way, which is the whole reason the uneven one is under test.
//
// What is NOT asserted: that the bar reaches either stop. It is 254 px long on a
// 127 px arm, and a level built around one has geometry above it and below -
// swung one way it settles at 2.11 and the other at 1.67, neither of them a
// limit. Where it comes to rest is the LEVEL's business and not the joint's.
// What is pinned is the direction it turns, that it never passes either stop, and
// the arithmetic that puts those stops where they are.
//
// Reads the converted levels and the remake's data from outside this repository,
// and skips, saying where it looked, when either is absent.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "core/PhysicsSettings.hpp"
#include "sim/Game.hpp"
#include "sim/Hinge.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using namespace MagicPortals;
using Supersonic::JointComponent;
using Supersonic::RigidBodyComponent;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kData = MAGICPORTALS_DATA_DIR;
const std::string kPortData = MAGICPORTALS_PORT_DATA_DIR;
constexpr float kStep = 1.0f / 60.0f;

// 2-28 pins a spinning platform; 2-31 a seesaw, and the only one in the game
// whose range is uneven.
constexpr const char* kPlatformLevel = "level27a";
constexpr const char* kSeesawLevel = "level30a";

struct Run {
    Game::Data data;
    entt::registry registry;
    Game::Level level;
};

bool Begin(const std::string& levelName, Run& run, bool withStatics = true) {
    std::string error;
    const bool ok = Game::LoadData(kLevels + "/" + levelName + ".tscn", kData,
                                   std::filesystem::temp_directory_path() / "supersonic-test-mp-hinge", run.data,
                                   error) &&
                    Game::Start(run.data, run.registry, run.level, error, withStatics);
    CHECK_MSG(ok, levelName + ": " + error);
    return ok;
}

void Tick(Run& run) {
    Game::Tick(run.data, run.registry, run.level, 0.0f, kStep);
}

std::filesystem::path Scratch() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-mp-hinge";
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

void TheReaderRefusesWhatWouldBeSilent() {
    Hinge::Rules rules;
    std::string error;
    CHECK_MSG(Hinge::LoadRules(kPortData + "/hinge.json", rules, error), kPortData + "/hinge.json: " + error);
    CHECK_MSG(::test::nearly(static_cast<float>(rules.stiffness), 0.8f), "the port runs at the engine's own 0.8");

    // Nothing taken out is a hinge that drifts apart under load; more than all of
    // it over-corrects and shakes. Both look like a level that works until
    // something stands on the platform.
    const std::string none = WriteRules("no-stiffness.json", R"({"joint": {"stiffness": 0.0}})");
    CHECK_MSG(!Hinge::LoadRules(none, rules, error), "a stiffness of nothing is refused");

    const std::string over = WriteRules("over-stiffness.json", R"({"joint": {"stiffness": 1.5}})");
    CHECK_MSG(!Hinge::LoadRules(over, rules, error), "and so is more than all of it");
}

void WhatTheLevelsCarry() {
    Run run;
    if (!Begin(kPlatformLevel, run)) return;

    CHECK_EQ(static_cast<int>(run.level.hinge.hinges.size()), 1);
    if (run.level.hinge.hinges.empty()) return;

    const Hinge::Hinged& hinged = run.level.hinge.hinges.front();
    CHECK_MSG(hinged.limited, "level27a's platform is limited");
    CHECK_MSG(::test::nearly(static_cast<float>(hinged.lowerRad), -1.5708f, 1e-3f) &&
                  ::test::nearly(static_cast<float>(hinged.upperRad), 1.5708f, 1e-3f),
              "a quarter turn either way, as its .ent says");
    CHECK_MSG(run.registry.valid(hinged.anchor), "and it is pinned to the anchor the level places");
    CHECK_MSG(run.registry.all_of<JointComponent>(hinged.body),
              "the body carries the joint the solver reads");

    const JointComponent& joint = run.registry.get<JointComponent>(hinged.body);
    CHECK_MSG(joint.type == JointComponent::Type::Hinge, "as a hinge");
    CHECK_MSG(joint.connectedBody == hinged.anchor, "to that anchor");
    CHECK_MSG(!joint.useMotor, "with no motor: every placement disables it, and the original zeroes it besides");
    CHECK_MSG(!joint.collideConnected,
              "and not colliding with what it hangs from, which in this level overlaps it by 89 px");
    std::printf("  %s: %s pinned to %s\n", kPlatformLevel, hinged.name.c_str(), hinged.anchorName.c_str());
}

void TheAuthoredRangeIsMeasuredFromTheRestAngle() {
    Run run;
    if (!Begin(kSeesawLevel, run)) return;
    if (run.level.hinge.hinges.empty()) {
        CHECK_MSG(false, "level30a pins a seesaw");
        return;
    }

    // Added to the rest angle, each bound keeping its place: the engine's hinge
    // angle runs with the original's.
    const Hinge::Hinged& hinged = run.level.hinge.hinges.front();
    CHECK_MSG(::test::nearly(static_cast<float>(hinged.minRad),
                             static_cast<float>(hinged.restRad + hinged.lowerRad), 1e-4f),
              "the range is measured from where the level was built");
    CHECK_MSG(::test::nearly(static_cast<float>(hinged.maxRad),
                             static_cast<float>(hinged.restRad + hinged.upperRad), 1e-4f),
              "at both ends");

    // The uneven one. If this reads as symmetric, the level was misread.
    CHECK_MSG(std::fabs(hinged.lowerRad + hinged.upperRad) > 0.5,
              "and level30a's own range is uneven, which is what makes it the test");
    std::printf("  %s: %.4f to %.4f, resting at %.4f\n", kSeesawLevel, hinged.minRad, hinged.maxRad, hinged.restRad);
}

// THE sign test. Turned one way it must stop at the near limit, and the other
// way at the far one - and those are 1.13 and 0.44 radians from rest, so a
// swapped or negated range lands against the wrong stop by a wide margin.
void ASeesawStopsAtItsOwnEnds() {
    // GRAVITY OFF, so the joint is the only thing that can stop it. Left on, the
    // seesaw settles at whatever balance gravity finds - 2.1113 in the middle of
    // a range running 1.6251 to 3.1958 - and never reaches a stop at all, which
    // says nothing about where the stops are.
    // outStart is the angle BEFORE anything is stepped. Everything here rests on
    // restRad - computed from the level's rotations - being the angle the solver
    // actually sees, and that has never been measured. If the two differ, the
    // limits are offset from reality and the solver drags the body to a limit at
    // once, which looks exactly like a body swinging.
    const auto swing = [](double spin, double& outAngle, double& outRest, double& outMin, double& outMax,
                          double& outStart, double& outEarly) {
        Run run;
        if (!Begin(kSeesawLevel, run)) return false;
        if (run.level.hinge.hinges.empty()) return false;
        const Hinge::Hinged& hinged = run.level.hinge.hinges.front();
        outMin = hinged.minRad;
        outMax = hinged.maxRad;
        outRest = hinged.restRad;
        outStart = Hinge::AngleOf(run.registry, hinged);

        Supersonic::PhysicsSettings settings;
        settings.gravity = glm::vec3(0.0f);
        run.registry.ctx().insert_or_assign<Supersonic::PhysicsSettings>(std::move(settings));

        if (!run.registry.all_of<RigidBodyComponent>(hinged.body)) return false;
        run.registry.get<RigidBodyComponent>(hinged.body).angularVelocity =
            glm::vec3(0.0f, 0.0f, static_cast<float>(spin));

        for (int tick = 0; tick < 6; ++tick) Tick(run);
        outEarly = Hinge::AngleOf(run.registry, hinged);
        for (int tick = 0; tick < 234; ++tick) Tick(run);
        outAngle = Hinge::AngleOf(run.registry, hinged);
        return true;
    };

    double angle = 0.0;
    double rest = 0.0;
    double minRad = 0.0;
    double maxRad = 0.0;
    double start = 0.0;
    double early = 0.0;

    // Up first, and the direction is read six ticks in - the only window in which
    // nothing but the joint has touched the bar.
    if (!swing(3.0, angle, rest, minRad, maxRad, start, early)) return;
    std::printf("  %s: +z from %.4f (rest says %.4f), after 6 ticks %.4f, stopped %.4f, ends %.4f..%.4f\n",
                kSeesawLevel, start, rest, early, angle, minRad, maxRad);
    CHECK_MSG(::test::nearly(static_cast<float>(start), static_cast<float>(rest), 1e-3f),
              "the angle the solver sees at rest is the one the limits were built from, got " +
                  std::to_string(start) + " against " + std::to_string(rest));
    CHECK_MSG(early > start + 0.1, "turning about +z RAISES the hinge angle, got " + std::to_string(early) +
                                       " from " + std::to_string(start));
    CHECK_MSG(angle <= maxRad + 0.08, "and it never passes the upper stop, got " + std::to_string(angle) +
                                          " against " + std::to_string(maxRad));

    // And down. NEITHER end is actually reached - the bar meets the level's own
    // geometry first, whichever way it turns - so what is checked is that it goes
    // the right way and never passes a stop.
    if (!swing(-3.0, angle, rest, minRad, maxRad, start, early)) return;
    std::printf("  %s: -z from %.4f (rest says %.4f), after 6 ticks %.4f, stopped %.4f, ends %.4f..%.4f\n",
                kSeesawLevel, start, rest, early, angle, minRad, maxRad);
    CHECK_MSG(early < start - 0.1, "and about -z it falls, got " + std::to_string(early) + " from " +
                                       std::to_string(start));
    CHECK_MSG(angle >= minRad - 0.08, "stopping at the lower end rather than past it, got " +
                                          std::to_string(angle) + " against " + std::to_string(minRad));
    CHECK_MSG(std::fabs((rest - minRad) - 1.1325) < 1e-3 && std::fabs((maxRad - rest) - 0.4382) < 1e-3,
              "with the long 1.1325 below rest and the short 0.4382 above, as level30a authors it");
}

void WithoutItsAnchorThereIsNoHinge() {
    // The anchor is static, so a level started without its statics has none, and
    // a joint attached to nothing would be worse than no joint at all.
    Run run;
    if (!Begin(kPlatformLevel, run, false)) return;
    CHECK_MSG(run.level.hinge.hinges.empty(), "a level with its statics left out pins nothing");
}

void runTests() {
    TheReaderRefusesWhatWouldBeSilent();
    WhatTheLevelsCarry();
    TheAuthoredRangeIsMeasuredFromTheRestAngle();
    ASeesawStopsAtItsOwnEnds();
    WithoutItsAnchorThereIsNoHinge();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec) ||
        !std::filesystem::is_regular_file(kData + "/entity_roles.json", ec)) {
        std::printf("test_mp_hinge SKIPPED - needs the converted levels at %s and the remake's data at %s\n",
                    kLevels.c_str(), kData.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_hinge", 18);
}
