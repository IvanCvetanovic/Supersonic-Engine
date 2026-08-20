// Tests for command-line parsing.
//
// main() took no arguments at all, so the engine could only be run by a human
// closing a window: CI could prove the binary links and nothing further, and
// the validation layers could not run anywhere but a developer's desktop.
// A hand-rolled parser is also the classic place to read one past the end of
// argv, so the flag-with-no-value cases are pinned first.

#include "TestHarness.hpp"
#include "core/LaunchOptions.hpp"

#include <cmath>

#include <initializer_list>
#include <string>
#include <vector>

using namespace Supersonic;

namespace {
LaunchOptions parse(std::initializer_list<const char*> args) {
    // argv[0] is the program name, which Parse must skip.
    std::vector<const char*> argv{"SupersonicEngine.exe"};
    for (const char* a : args) argv.push_back(a);
    return LaunchOptions::Parse(static_cast<int>(argv.size()), argv.data());
}
} // namespace

static void testNoArgumentsIsTheInteractiveDefault() {
    const auto o = parse({});
    CHECK(o.ok);
    CHECK(!o.helpRequested);
    CHECK_EQ(o.maxFrames, 0);
    CHECK(o.scenePath.empty());
}

static void testFrameCountIsRead() {
    const auto o = parse({"--frames", "120"});
    CHECK(o.ok);
    CHECK_EQ(o.maxFrames, 120);
}

static void testZeroFramesStillMeansRunForever() {
    // Not an error: 0 is the documented "until the window closes" value, and
    // rejecting it would make the flag awkward to pass from a script.
    const auto o = parse({"--frames", "0"});
    CHECK(o.ok);
    CHECK_EQ(o.maxFrames, 0);
}

static void testScenePathIsRead() {
    const auto o = parse({"--scene", "assets/scenes/MainScene.scene"});
    CHECK(o.ok);
    CHECK(o.scenePath == "assets/scenes/MainScene.scene");
}

static void testFlagsCombine() {
    const auto o = parse({"--scene", "a.scene", "--frames", "3"});
    CHECK(o.ok);
    CHECK_EQ(o.maxFrames, 3);
    CHECK(o.scenePath == "a.scene");
}

static void testTrailingFlagWithNoValueIsRejected() {
    // The out-of-bounds read. Both of these used to be one argv[i + 1] away
    // from walking off the end of the array.
    const auto frames = parse({"--frames"});
    CHECK_MSG(!frames.ok, "--frames with no value must be rejected, not read past argv");
    CHECK(!frames.error.empty());

    const auto scene = parse({"--scene"});
    CHECK_MSG(!scene.ok, "--scene with no value must be rejected, not read past argv");
    CHECK(!scene.error.empty());
}

static void testNonNumericFrameCountIsRejected() {
    const auto o = parse({"--frames", "soon"});
    CHECK_MSG(!o.ok, "a non-numeric count must be rejected");
    CHECK_MSG(o.error.find("soon") != std::string::npos,
              "the message should quote what was actually passed");
}

static void testTrailingGarbageOnANumberIsRejected() {
    // strtol stops at the first non-digit and reports success, so "12frames"
    // would otherwise parse as 12 and silently do something else.
    const auto o = parse({"--frames", "12frames"});
    CHECK_MSG(!o.ok, "a number with a suffix must be rejected, not truncated");
}

static void testNegativeFrameCountIsRejected() {
    const auto o = parse({"--frames", "-1"});
    CHECK_MSG(!o.ok, "a negative count must be rejected");
}

static void testUnknownOptionIsRejected() {
    const auto o = parse({"--turbo"});
    CHECK_MSG(!o.ok, "an unrecognised option must fail rather than be ignored");
    CHECK_MSG(o.error.find("--turbo") != std::string::npos,
              "the message should name the option");
}

static void testHelpIsNotAnError() {
    // Folding --help into ok would make asking for help an error exit and
    // break any script that checks the status.
    for (const char* flag : {"--help", "-h"}) {
        const auto o = parse({flag});
        CHECK(o.helpRequested);
        CHECK_MSG(o.ok, "--help must not be reported as a parse failure");
    }
    CHECK(LaunchOptions::Usage() != nullptr);
}

static void testScreenshotPathIsRead() {
    const auto o = parse({"--screenshot", "shots/frame.png"});
    CHECK(o.ok);
    CHECK(o.screenshotPath == "shots/frame.png");

    // The same argv-overrun case as the other value-taking flags.
    const auto bare = parse({"--screenshot"});
    CHECK_MSG(!bare.ok, "--screenshot with no path must be rejected, not read past argv");

    // And it composes, because a capture is only useful with a frame count and
    // usually a chosen scene.
    const auto combined = parse({"--frames", "5", "--scene", "a.scene",
                                 "--screenshot", "out.png"});
    CHECK(combined.ok);
    CHECK_EQ(combined.maxFrames, 5);
    CHECK(combined.scenePath == "a.scene");
    CHECK(combined.screenshotPath == "out.png");
}


// --- --fixed-step -----------------------------------------------------------
//
// The flag that makes a run reproduce. Its value is optional, which is the
// awkward part of any hand-rolled parser: an optional value has to be
// distinguished from the next flag without consuming it.

static void testFixedStepDefaultsToSixtyHertz() {
    const auto o = parse({"--fixed-step"});
    CHECK(o.ok);
    CHECK_MSG(std::fabs(o.fixedDelta - 1.0f / 60.0f) < 1e-9f,
              "the bare flag means a sixtieth: got " + std::to_string(o.fixedDelta));
}

static void testFixedStepTakesAnExplicitStep() {
    const auto o = parse({"--fixed-step", "0.02"});
    CHECK(o.ok);
    CHECK_MSG(std::fabs(o.fixedDelta - 0.02f) < 1e-6f,
              "an explicit step must be read: got " + std::to_string(o.fixedDelta));
}

static void testFixedStepDoesNotSwallowTheNextFlag() {
    // The case an optional value gets wrong. Consuming unconditionally turns
    // this into a complaint about a step of "--frames", and the frame count is
    // then lost as well.
    const auto o = parse({"--fixed-step", "--frames", "60"});
    CHECK_MSG(o.ok, "the next flag must not be eaten: " + o.error);
    CHECK_MSG(std::fabs(o.fixedDelta - 1.0f / 60.0f) < 1e-9f, "the step defaults");
    CHECK_MSG(o.maxFrames == 60, "and --frames still parses: got " +
                                     std::to_string(o.maxFrames));
}

static void testFixedStepRejectsNonsense() {
    CHECK_MSG(!parse({"--fixed-step", "0"}).ok, "a step of zero is not a step");
    CHECK_MSG(!parse({"--fixed-step", "-0.01"}).ok, "nor a negative one");
    CHECK_MSG(!parse({"--fixed-step", "5"}).ok,
              "nor five seconds, which is a mistake rather than a choice");
}

static void testTheRealClockIsTheDefault() {
    // A game that ignores how long a frame took plays in slow motion the moment
    // it drops below its target rate, so this must stay opt-in.
    const auto o = parse({"--frames", "10"});
    CHECK(o.ok);
    CHECK_MSG(o.fixedDelta == 0.0f, "without the flag the simulation follows the real clock");
}

static void runTests() {
    testFixedStepDefaultsToSixtyHertz();
    testFixedStepTakesAnExplicitStep();
    testFixedStepDoesNotSwallowTheNextFlag();
    testFixedStepRejectsNonsense();
    testTheRealClockIsTheDefault();
    testScreenshotPathIsRead();
    testNoArgumentsIsTheInteractiveDefault();
    testFrameCountIsRead();
    testZeroFramesStillMeansRunForever();
    testScenePathIsRead();
    testFlagsCombine();
    testTrailingFlagWithNoValueIsRejected();
    testNonNumericFrameCountIsRejected();
    testTrailingGarbageOnANumberIsRejected();
    testNegativeFrameCountIsRejected();
    testUnknownOptionIsRejected();
    testHelpIsNotAnError();
}

TEST_MAIN("test_launchoptions", 30)
