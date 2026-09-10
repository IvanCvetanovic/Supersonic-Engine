// The portal traversal's arithmetic, against answers worked by hand.
//
// portal_system.gd:230-263 turns a traveller's velocity through the exit's
// rotation minus the entry's, scales it, and puts the traveller out along it,
// clear of the exit. That is structure, and it is pinned here. The numbers the
// remake feeds it - mode, scale, offset, lockout - are every one marked _guess
// in its portals.json, so each Transit below is written for the test and none
// is the remake's.

#include "TestHarness.hpp"

#include "sim/Portal.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace MagicPortals;

namespace {

constexpr double kQuarter = 1.5707963267948966;
constexpr double kHalf = 3.141592653589793;

Portal::Transit Make(const std::string& mode, double scale, double offset) {
    Portal::Transit transit;
    transit.momentumMode = mode;
    transit.exitSpeedScale = scale;
    transit.exitOffsetPx = offset;
    transit.reentryLockoutS = 0.5;
    return transit;
}

// Godot does this in single precision, and the angle's sine comes from DetMath
// in single precision here too, so a thousandth of a pixel is the honest bar.
bool Near(const glm::dvec2& got, double x, double y) {
    return std::fabs(got.x - x) < 1e-3 && std::fabs(got.y - y) < 1e-3;
}

std::string Show(const glm::dvec2& v) {
    return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
}

// In the remake's space +y is down, so a positive rotation is clockwise on
// screen: a quarter turn takes "moving right" to "moving down".
void TurnsThroughTheDifference() {
    const Portal::Transit t = Make("rotate_to_exit", 1.0, 10.0);
    glm::dvec2 v = Portal::ExitVelocity({100, 0}, 0.0, kQuarter, t);
    CHECK_MSG(Near(v, 0, 100), Show(v));
    v = Portal::ExitVelocity({100, 0}, kQuarter, kQuarter, t);
    CHECK_MSG(Near(v, 100, 0), Show(v));
    v = Portal::ExitVelocity({100, 0}, 1.0, 1.0 + kQuarter, t); // only the difference counts
    CHECK_MSG(Near(v, 0, 100), Show(v));
    v = Portal::ExitVelocity({30, -40}, 0.0, kHalf, t);
    CHECK_MSG(Near(v, -30, 40), Show(v));
    v = Portal::ExitVelocity({30, -40}, kQuarter, 0.0, t); // a quarter the other way
    CHECK_MSG(Near(v, -40, -30), Show(v));
    CHECK(std::fabs(std::sqrt(v.x * v.x + v.y * v.y) - 50.0) < 1e-3); // speed is kept
}

void ModesAndScale() {
    CHECK(Near(Portal::ExitVelocity({30, -40}, 0.0, kQuarter, Make("reset", 3.0, 10.0)), 0, 0));
    CHECK(Near(Portal::ExitVelocity({30, -40}, 0.0, kQuarter, Make("preserve", 1.0, 10.0)), 30, -40));
    // portals.json offers "clamp"; the remake's match has no arm for it, so it
    // falls to the default and keeps the velocity - as does anything unknown.
    CHECK(Near(Portal::ExitVelocity({30, -40}, 0.0, kQuarter, Make("clamp", 1.0, 10.0)), 30, -40));
    CHECK(Near(Portal::ExitVelocity({30, -40}, 0.0, kQuarter, Make("", 1.0, 10.0)), 30, -40));
    // The scale applies after the turn, whatever the mode.
    CHECK(Near(Portal::ExitVelocity({100, 0}, 0.0, kQuarter, Make("rotate_to_exit", 2.0, 10.0)), 0, 200));
    CHECK(Near(Portal::ExitVelocity({30, -40}, 0.0, 0.0, Make("preserve", 0.5, 10.0)), 15, -20));
}

void ComesOutAlongItsVelocity() {
    const Portal::Transit t = Make("rotate_to_exit", 1.0, 10.0);
    CHECK(Near(Portal::ExitPosition({500, 60}, {0, 100}, t), 500, 70));
    CHECK(Near(Portal::ExitPosition({500, 60}, {30, -40}, t), 506, 52));
    // Arriving still, it comes out straight down - +y, the remake's down.
    CHECK(Near(Portal::ExitPosition({500, 60}, {0, 0}, t), 500, 70));
    // Godot's is_zero_approx is per component, under 1e-5: below it is still,
    // just above it is a direction.
    CHECK(Near(Portal::ExitPosition({500, 60}, {9e-6, -9e-6}, t), 500, 70));
    CHECK(Near(Portal::ExitPosition({500, 60}, {2e-5, 0}, t), 510, 60));
    CHECK(Near(Portal::ExitPosition({500, 60}, {0, 100}, Make("rotate_to_exit", 1.0, 0.0)), 500, 60));
}

void LoadsTransitAndRefusesGaps() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "supersonic-test-mp-portal";
    std::filesystem::create_directories(directory);
    const auto write = [&](const char* name, const std::string& text) {
        const std::filesystem::path path = directory / name;
        std::ofstream(path, std::ios::binary) << text;
        return path.string();
    };

    Portal::Transit transit;
    std::string error;
    const std::string good = write("good.json", R"({"transit": {"momentum_mode": "reset", "exit_speed_scale": 0.5,
        "exit_offset_px": 7, "reentry_lockout_s": 0.3, "control_during_transit": true}})");
    CHECK_MSG(Portal::LoadTransit(good, transit, error), error);
    CHECK(transit.momentumMode == "reset" && transit.exitSpeedScale == 0.5 && transit.exitOffsetPx == 7.0 &&
          transit.reentryLockoutS == 0.3);

    // A gap is an error naming the field, and leaves what it was given alone.
    Portal::Transit untouched = Make("preserve", 1.0, 1.0);
    error.clear();
    const std::string gap = write("gap.json", R"({"transit": {"momentum_mode": "reset", "exit_speed_scale": 0.5,
        "reentry_lockout_s": 0.3}})");
    CHECK(!Portal::LoadTransit(gap, untouched, error) && error.find("exit_offset_px") != std::string::npos);
    CHECK(untouched.momentumMode == "preserve" && untouched.exitOffsetPx == 1.0);

    error.clear();
    const std::string wrong = write("wrong.json", R"({"transit": {"momentum_mode": 3, "exit_speed_scale": 0.5,
        "exit_offset_px": 7, "reentry_lockout_s": 0.3}})");
    CHECK(!Portal::LoadTransit(wrong, transit, error) && error.find("momentum_mode") != std::string::npos);

    error.clear();
    CHECK(!Portal::LoadTransit(write("none.json", R"({"placement": {}})"), transit, error) &&
          error.find("transit") != std::string::npos);
    error.clear();
    CHECK(!Portal::LoadTransit(write("broken.json", "{"), transit, error) && !error.empty());
    error.clear();
    CHECK(!Portal::LoadTransit((directory / "absent.json").string(), transit, error) &&
          error.find("cannot open") != std::string::npos);
}

void runTests() {
    TurnsThroughTheDifference();
    ModesAndScale();
    ComesOutAlongItsVelocity();
    LoadsTransitAndRefusesGaps();
}

} // namespace

TEST_MAIN("test_mp_portal", 24)
