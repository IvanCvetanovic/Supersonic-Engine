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


// --- --screenshot-every -----------------------------------------------------
//
// Several frames of one run, for any check of something that moves. The frame
// loop that acts on it needs a device, so what is pinned here is everything it
// is handed: which frames, under which names, and when the flag is refused.

static void testScreenshotEveryIsRead() {
    const auto o = parse({"--frames", "420", "--fixed-step", "--screenshot", "shots/run.png",
                          "--screenshot-every", "30"});
    CHECK_MSG(o.ok, o.error);
    CHECK_EQ(o.screenshotEvery, 30);
    CHECK(o.screenshotPath == "shots/run.png");
    CHECK_MSG(o.warnings.empty(), "a fixed-step run has nothing to warn about");

    // Not given is zero, and a plain run has no warnings to print.
    const auto plain = parse({"--frames", "420", "--screenshot", "shots/run.png"});
    CHECK(plain.ok);
    CHECK_EQ(plain.screenshotEvery, 0);
    CHECK_MSG(plain.warnings.empty(),
              "a --screenshot run without the new flag must not start warning");
}

static void testScreenshotEveryNeedsAScreenshotPath() {
    // The stamped names are made from that path; there is nothing to invent.
    const auto alone = parse({"--fixed-step", "--screenshot-every", "30"});
    CHECK_MSG(!alone.ok, "--screenshot-every without --screenshot is refused");
    CHECK_MSG(alone.error.find("--screenshot <path>") != std::string::npos,
              "and the message says what is missing: " + alone.error);

    // Checked after the loop, so the order the two were typed in is irrelevant.
    const auto before = parse({"--fixed-step", "--screenshot-every", "30", "--screenshot", "a.png"});
    const auto after = parse({"--fixed-step", "--screenshot", "a.png", "--screenshot-every", "30"});
    CHECK_MSG(before.ok, "the path may come after the flag: " + before.error);
    CHECK_MSG(after.ok, "or before it: " + after.error);
    CHECK_EQ(before.screenshotEvery, after.screenshotEvery);
}

static void testScreenshotEveryRefusesAnythingButACountOfAtLeastOne() {
    // Zero is what a script dividing a run length produces by accident, and
    // reading it as "never" would look like a capture that silently broke.
    for (const char* bad : {"0", "-1", "-30"}) {
        const auto o = parse({"--screenshot", "a.png", "--screenshot-every", bad});
        CHECK_MSG(!o.ok, std::string("'") + bad + "' is refused");
        CHECK_MSG(o.error.find(bad) != std::string::npos,
                  "and the message quotes it: " + o.error);
    }
    for (const char* bad : {"soon", "30f", "", "1.5", "1000001"}) {
        const auto o = parse({"--screenshot", "a.png", "--screenshot-every", bad});
        CHECK_MSG(!o.ok, std::string("'") + bad + "' is not a frame count");
    }
    CHECK_MSG(!parse({"--screenshot", "a.png", "--screenshot-every"}).ok,
              "and a bare flag is refused rather than read past argv");

    // The floor itself is a count: every frame.
    const auto every = parse({"--fixed-step", "--screenshot", "a.png", "--screenshot-every", "1"});
    CHECK_MSG(every.ok, every.error);
    CHECK_EQ(every.screenshotEvery, 1);
}

static void testScreenshotEveryWarnsOnTheRealClock() {
    // Accepted, since frames on the real clock are still pictures - but frame
    // N is then not N/60 s of game time, and that must not pass silently for a
    // measurement.
    const auto o = parse({"--frames", "60", "--screenshot", "a.png", "--screenshot-every", "10"});
    CHECK_MSG(o.ok, "a warning, not a refusal: " + o.error);
    CHECK_EQ(o.warnings.size(), std::size_t{1});
    CHECK_MSG(!o.warnings.empty() && o.warnings[0].find("--fixed-step") != std::string::npos,
              "naming the flag that would fix it");

    // The step given after the flag counts: the check is order-independent too.
    const auto late = parse({"--screenshot", "a.png", "--screenshot-every", "10", "--fixed-step"});
    CHECK(late.ok);
    CHECK_MSG(late.warnings.empty(), "--fixed-step after the flag still silences it");
}

static void testTheFramesCapturedOverARun() {
    // The acceptance run of the flag: 420 frames at N = 30 writes frames 30,
    // 60 ... 420 - fourteen, the last of them the same frame --screenshot writes.
    const auto o = parse({"--frames", "420", "--fixed-step", "--screenshot", "x.png",
                          "--screenshot-every", "30"});
    std::vector<long long> captured;
    for (long long frame = 0; frame <= 420; ++frame) {
        if (o.CapturesFrame(frame)) captured.push_back(frame);
    }
    CHECK_EQ(captured.size(), std::size_t{14});
    CHECK_MSG(!captured.empty() && captured.front() == 30, "the first is frame 30");
    CHECK_MSG(!captured.empty() && captured.back() == 420, "and the last is frame 420");

    // Never before anything is drawn, which 0 % N alone would ask for.
    CHECK_MSG(!o.CapturesFrame(0), "frame 0 is no frame");

    // Every frame at N = 1, and none from a run that never asked.
    const auto every = parse({"--fixed-step", "--screenshot", "x.png", "--screenshot-every", "1"});
    CHECK(every.CapturesFrame(1) && every.CapturesFrame(2) && every.CapturesFrame(3));
    const auto never = parse({"--frames", "420", "--screenshot", "x.png"});
    bool any = false;
    for (long long frame = 0; frame <= 420; ++frame) any = any || never.CapturesFrame(frame);
    CHECK_MSG(!any, "no --screenshot-every, no stamped frames");

    // And a count set without a path - which Parse refuses, but a caller can
    // build by hand - captures nothing rather than writing to "_f30".
    LaunchOptions handMade;
    handMade.screenshotEvery = 30;
    CHECK_MSG(!handMade.CapturesFrame(30), "no path, no capture");
}

static void testTheStampedNameSitsBeforeTheExtension() {
    const auto stamped = [](const char* path, long long frame) {
        LaunchOptions o;
        o.screenshotPath = path;
        o.screenshotEvery = 1;
        return o.ScreenshotPathForFrame(frame);
    };
    const auto expect = [&](const char* path, long long frame, const std::string& want) {
        const std::string got = stamped(path, frame);
        CHECK_MSG(got == want, std::string(path) + " at frame " + std::to_string(frame) +
                                   " -> '" + got + "', expected '" + want + "'");
    };

    expect("shots/run.png", 30, "shots/run_f30.png");
    expect("run.png", 420, "run_f420.png");
    // Not zero-padded: the capture scripts already file frames as _f420.
    expect("run.png", 5, "run_f5.png");
    // A name that already carries a frame keeps it and gains the stamp.
    expect("C:/out/1-01_level0_f420.png", 30, "C:/out/1-01_level0_f420_f30.png");
    // Only the last dot of the FILE name is the extension, never a directory's.
    expect("captures.v2/run", 60, "captures.v2/run_f60");
    expect("a/run.tar.png", 90, "a/run.tar_f90.png");
#if defined(_WIN32)
    // The separator the caller typed is kept, so the log names the file as asked.
    expect("C:\\out\\run.png", 30, "C:\\out\\run_f30.png");
#endif
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

// --- --record and --replay --------------------------------------------------

static void testRecordAndReplayTakePaths() {
    const auto recording = parse({"--record", "session.replay"});
    CHECK(recording.ok);
    CHECK(recording.recordPath == "session.replay");
    CHECK(recording.replayPath.empty());

    const auto replaying = parse({"--replay", "bug.replay"});
    CHECK(replaying.ok);
    CHECK(replaying.replayPath == "bug.replay");
    CHECK(replaying.recordPath.empty());

    CHECK_MSG(!parse({"--record"}).ok, "a path is not optional");
    CHECK_MSG(!parse({"--replay"}).ok, "for either of them");
}

static void testRecordingAReplayIsRefused() {
    // Not resolved by picking one. A run being fed its input while writing that
    // input down produces a file that agrees with itself by construction - a
    // verification that passes without verifying anything, which is worse than
    // either flag simply failing.
    const auto both = parse({"--record", "out.replay", "--replay", "in.replay"});
    CHECK_MSG(!both.ok, "recording a replay is not a thing");

    // And the other way round, because the message must not depend on the order
    // the two were typed in.
    const auto reversed = parse({"--replay", "in.replay", "--record", "out.replay"});
    CHECK_MSG(!reversed.ok, "in either order");
    CHECK_MSG(both.error == reversed.error, "with the same message: " + both.error);
}

static void testAReplayCanBePinnedAndFramed() {
    // The combination a verification run actually uses: a recorded session,
    // a constant delta so the tick count is pinned, and a frame budget so it
    // exits by itself.
    const auto options = parse({"--replay", "bug.replay", "--fixed-step", "--frames", "600"});
    CHECK(options.ok);
    CHECK(options.replayPath == "bug.replay");
    CHECK_EQ(options.maxFrames, 600);
    CHECK_MSG(options.fixedDelta > 0.0f, "and the step is pinned");
}


// --- --window: a size for one run ----------------------------------------

static void testAWindowSizeIsParsedBothWaysRoundIsSpelled() {
    const char* lower[] = {"engine", "--window", "1920x1080"};
    const auto a = LaunchOptions::Parse(3, lower);
    CHECK(a.ok);
    CHECK_EQ(a.windowWidth, uint32_t{1920});
    CHECK_EQ(a.windowHeight, uint32_t{1080});

    // Capital X too, because a size is a thing people type.
    const char* upper[] = {"engine", "--window", "800X600"};
    const auto b = LaunchOptions::Parse(3, upper);
    CHECK(b.ok);
    CHECK_EQ(b.windowWidth, uint32_t{800});
    CHECK_EQ(b.windowHeight, uint32_t{600});
}

static void testNoWindowFlagLeavesItToWhoeverElseHasAnOpinion() {
    // Zero is "not given", which is what lets the manifest - and failing that
    // the engine's default - answer instead. A flag that defaulted to 1280x720
    // here would silently outrank a game's own declared size.
    const char* argv[] = {"engine", "--frames", "10"};
    const auto options = LaunchOptions::Parse(3, argv);
    CHECK(options.ok);
    CHECK_EQ(options.windowWidth, uint32_t{0});
    CHECK_EQ(options.windowHeight, uint32_t{0});
}

static void testASizeThatIsAlmostRightIsRefusedRatherThanGuessed() {
    // The ones worth complaining about. "1920 x 1080" and "1920x1080x" both
    // contain the answer, and a parser that read them anyway would teach people
    // a spelling that stops working the day it is tightened.
    for (const char* bad : {"1920", "1920x", "x1080", "1920 x 1080", "1920x1080x",
                            "abcxdef", "-100x200", "1920x1080y"}) {
        const char* argv[] = {"engine", "--window", bad};
        const auto options = LaunchOptions::Parse(3, argv);
        CHECK_MSG(!options.ok, std::string("'") + bad + "' is refused");
        CHECK_MSG(options.error.find(bad) != std::string::npos,
                  std::string("and the message says what was given: ") + options.error);
    }

    // And the flag with nothing after it.
    const char* bare[] = {"engine", "--window"};
    CHECK_MSG(!LaunchOptions::Parse(2, bare).ok, "a bare --window is refused");
}

static void testASizeNobodyCouldUseIsRefused() {
    // Not clamped. A window of sixteen pixels is a typo, and opening one looks
    // like the engine failing rather than like the mistake it is.
    for (const char* bad : {"16x9", "1x1", "63x63"}) {
        const char* argv[] = {"engine", "--window", bad};
        CHECK_MSG(!LaunchOptions::Parse(3, argv).ok,
                  std::string("'") + bad + "' is too small");
    }

    // The other end, which is what an accidental extra digit looks like.
    const char* huge[] = {"engine", "--window", "99999x1080"};
    CHECK_MSG(!LaunchOptions::Parse(3, huge).ok, "and an implausible one is too");

    // The floor itself is accepted, or the boundary is off by one and nobody
    // would notice.
    const char* edge[] = {"engine", "--window", "64x64"};
    CHECK_MSG(LaunchOptions::Parse(3, edge).ok, "the smallest allowed size works");
}


// --- --fullscreen and --windowed: the mode for one run ------------------------

static void testNeitherModeFlagLeavesItToTheManifest() {
    const auto options = parse({"--frames", "60"});
    CHECK(options.ok);
    CHECK_MSG(!options.fullscreen && !options.windowed,
              "no flag, no opinion: the manifest answers");
}

static void testTheModeFlagsAreRead() {
    const auto full = parse({"--fullscreen"});
    CHECK(full.ok);
    CHECK(full.fullscreen);
    CHECK(!full.windowed);

    const auto windowed = parse({"--windowed"});
    CHECK(windowed.ok);
    CHECK(windowed.windowed);
    CHECK(!windowed.fullscreen);

    // Each takes no value, so the flag after it is still a flag.
    const auto capture = parse({"--windowed", "--window", "1024x768", "--frames", "300"});
    CHECK(capture.ok);
    CHECK(capture.windowed);
    CHECK_EQ(capture.windowWidth, uint32_t{1024});
    CHECK_EQ(capture.maxFrames, 300);
}

static void testFullscreenAndWindowedTogetherAreRefused() {
    // As --record with --replay: whichever won, the other was typed by somebody
    // expecting it to do something.
    const auto both = parse({"--fullscreen", "--windowed"});
    CHECK_MSG(!both.ok, "both at once is refused");

    const auto reversed = parse({"--windowed", "--fullscreen"});
    CHECK_MSG(!reversed.ok, "in either order");
    CHECK_MSG(both.error == reversed.error, "with the same message: " + both.error);
}


// --- --screenshot-ui: the frame as the window shows it -----------------------
//
// The readback itself needs a swapchain. What is pinned here is when it is
// asked for - which has to be known BEFORE a frame is drawn, since the copy is
// recorded into that frame - and under which names.

static void testScreenshotUiPathIsRead() {
    const auto o = parse({"--frames", "30", "--screenshot-ui", "shots/ui.png"});
    CHECK_MSG(o.ok, o.error);
    CHECK(o.screenshotUiPath == "shots/ui.png");
    CHECK_MSG(o.screenshotPath.empty(), "and it does not stand in for --screenshot");

    CHECK_MSG(!parse({"--screenshot-ui"}).ok, "a bare flag is refused rather than read past argv");
    CHECK_MSG(!parse({"--screenshot-ui", ""}).ok, "and so is an empty path");

    // Both at once, each to its own file: one run, the scene and the window.
    const auto both = parse({"--frames", "30", "--screenshot", "scene.png",
                             "--screenshot-ui", "window.png"});
    CHECK_MSG(both.ok, both.error);
    CHECK(both.screenshotPath == "scene.png" && both.screenshotUiPath == "window.png");

    // One file for both would keep whichever was written second.
    const auto same = parse({"--screenshot", "x.png", "--screenshot-ui", "x.png"});
    CHECK_MSG(!same.ok, "the same path for both is refused");
    CHECK_MSG(same.error.find("x.png") != std::string::npos, "naming it: " + same.error);
}

static void testScreenshotEveryStampsTheUiCaptureToo() {
    // Either path is enough for --screenshot-every; with only the UI one, the
    // scene capture is not written at all.
    const auto o = parse({"--frames", "90", "--fixed-step", "--screenshot-ui", "shots/ui.png",
                          "--screenshot-every", "30"});
    CHECK_MSG(o.ok, o.error);
    CHECK(o.CapturesUiFrame(30) && o.CapturesUiFrame(60) && o.CapturesUiFrame(90));
    CHECK_MSG(!o.CapturesUiFrame(0) && !o.CapturesUiFrame(45), "only the multiples, never frame 0");
    CHECK_MSG(!o.CapturesFrame(30), "no --screenshot, no scene capture");
    CHECK_MSG(o.UiScreenshotPathForFrame(30) == "shots/ui_f30.png",
              "stamped as the scene capture is: " + o.UiScreenshotPathForFrame(30));

    // With both, one frame gives two files named alike.
    const auto both = parse({"--fixed-step", "--screenshot", "run.png", "--screenshot-ui",
                             "run_ui.png", "--screenshot-every", "10"});
    CHECK_MSG(both.ok, both.error);
    CHECK(both.CapturesFrame(10) && both.CapturesUiFrame(10));
    CHECK(both.ScreenshotPathForFrame(10) == "run_f10.png");
    CHECK(both.UiScreenshotPathForFrame(10) == "run_ui_f10.png");
    CHECK(LaunchOptions::StampFrame("a/b.png", 7) == "a/b_f7.png");
}

static void testTheFramesTheSwapchainIsReadBackFor() {
    const auto countOver = [](const LaunchOptions& o, long long frames) {
        std::vector<long long> read;
        for (long long frame = 0; frame <= frames; ++frame) {
            if (o.ReadsBackSwapchain(frame)) read.push_back(frame);
        }
        return read;
    };

    // Every stamped frame, and the last is one of them: 14 copies in 420.
    const auto stamped = parse({"--frames", "420", "--fixed-step", "--screenshot-ui", "u.png",
                                "--screenshot-every", "30"});
    const auto a = countOver(stamped, 420);
    CHECK_EQ(a.size(), std::size_t{14});
    CHECK_MSG(!a.empty() && a.front() == 30 && a.back() == 420, "30 to 420");

    // A last frame that is not a multiple is read back as well, for the final
    // file: 30, 60, 90 and 100.
    const auto ragged = parse({"--frames", "100", "--fixed-step", "--screenshot-ui", "u.png",
                               "--screenshot-every", "30"});
    const auto b = countOver(ragged, 100);
    CHECK_EQ(b.size(), std::size_t{4});
    CHECK_MSG(!b.empty() && b.back() == 100, "the last frame, for <path> itself");

    // Without --screenshot-every, only the last frame.
    const auto last = countOver(parse({"--frames", "30", "--screenshot-ui", "u.png"}), 30);
    CHECK_EQ(last.size(), std::size_t{1});
    CHECK_MSG(!last.empty() && last.front() == 30, "frame 30 of 30");

    // And none at all for a run that did not ask, or has no last frame.
    CHECK_MSG(countOver(parse({"--frames", "30", "--screenshot", "s.png"}), 30).empty(),
              "--screenshot alone reads nothing back from the swapchain");
    CHECK_MSG(countOver(parse({"--screenshot-ui", "u.png"}), 500).empty(),
              "no --frames and no --screenshot-every, nothing to copy");
}

// --- What a capture run owes ------------------------------------------------
//
// Every way a capture fails only logs, so main() compares the files a run wrote
// with these. Without it a run whose PNG was never written exited 0, and a
// script found last week's file at the path it named.

static void testACaptureRunOwesEveryFileItNamed() {
    // Counted from the same predicates the loop asks, so the count and the
    // loop cannot drift apart: the stamped frames from 1 to maxFrames, then one
    // final file per path.
    const auto byTheLoop = [](const LaunchOptions& o) {
        long long files = 0;
        for (long long frame = 1; frame <= o.maxFrames; ++frame) {
            if (o.CapturesFrame(frame)) ++files;
            if (o.CapturesUiFrame(frame)) ++files;
        }
        if (o.maxFrames > 0 && !o.screenshotPath.empty()) ++files;
        if (o.maxFrames > 0 && !o.screenshotUiPath.empty()) ++files;
        return files;
    };

    const auto one = parse({"--frames", "30", "--screenshot", "a.png"});
    CHECK_EQ(one.CapturesOwed(), 1LL);

    const auto two = parse({"--frames", "30", "--screenshot", "a.png", "--screenshot-ui", "b.png"});
    CHECK_EQ(two.CapturesOwed(), 2LL);

    // 30 ... 420 is fourteen stamped files, and the final one besides.
    const auto every = parse({"--frames", "420", "--fixed-step", "--screenshot", "x.png",
                              "--screenshot-every", "30"});
    CHECK_EQ(every.CapturesOwed(), 15LL);
    CHECK_EQ(every.CapturesOwed(), byTheLoop(every));

    // A ragged last frame: 30, 60, 90, and the final at 100, for each path.
    const auto ragged = parse({"--frames", "100", "--fixed-step", "--screenshot", "x.png",
                               "--screenshot-ui", "u.png", "--screenshot-every", "30"});
    CHECK_EQ(ragged.CapturesOwed(), 8LL);
    CHECK_EQ(ragged.CapturesOwed(), byTheLoop(ragged));

    const auto uiOnly = parse({"--frames", "7", "--fixed-step", "--screenshot-ui", "u.png",
                               "--screenshot-every", "1"});
    CHECK_EQ(uiOnly.CapturesOwed(), 8LL);
    CHECK_EQ(uiOnly.CapturesOwed(), byTheLoop(uiOnly));

    // Nothing asked, nothing owed; and nothing without --frames, where the
    // person closing the window decides the last frame.
    CHECK_EQ(parse({"--frames", "30"}).CapturesOwed(), 0LL);
    CHECK_EQ(parse({"--screenshot", "a.png"}).CapturesOwed(), 0LL);
    CHECK_EQ(parse({"--fixed-step", "--screenshot", "a.png", "--screenshot-every", "10"})
                 .CapturesOwed(),
             0LL);
}

static void testARunThatWroteLessThanItOwedIsAFailure() {
    const auto o = parse({"--frames", "100", "--fixed-step", "--screenshot", "x.png",
                          "--screenshot-ui", "u.png", "--screenshot-every", "30"});
    CHECK_MSG(o.MissingCaptures(8).empty(), "all eight written is a pass");

    // One short - the UI capture of a frame the swapchain was rebuilt on, say.
    const std::string missing = o.MissingCaptures(7);
    CHECK_MSG(!missing.empty(), "one missing is a failure");
    CHECK_MSG(missing.find("1 of the 8") != std::string::npos, "and says how many: " + missing);
    CHECK_MSG(!o.MissingCaptures(0).empty(), "none written is a failure");

    // A run that asked for nothing, or has no last frame, fails on nothing.
    CHECK(parse({"--frames", "30"}).MissingCaptures(0).empty());
    CHECK(parse({"--screenshot", "a.png"}).MissingCaptures(0).empty());
}

// --- --hidden: a run that never shows a window -------------------------------
//
// What lets a capture run on a desk somebody is using. The window it suppresses
// needs a device to exist, so what is pinned here is the flag: read, off by
// default, composing with the capture flags, and refused beside --fullscreen.

static void testHiddenIsOffUnlessAskedFor() {
    const auto plain = parse({"--frames", "60", "--windowed"});
    CHECK(plain.ok);
    CHECK_MSG(!plain.hidden, "every run shows its window unless told otherwise");

    const auto hidden = parse({"--hidden", "--frames", "30"});
    CHECK_MSG(hidden.ok, hidden.error);
    CHECK(hidden.hidden);
    CHECK_MSG(!hidden.fullscreen && !hidden.windowed,
              "and it sets neither mode flag: the app treats it as windowed itself");
}

static void testHiddenComposesWithACaptureRun() {
    // The command line stage 3's captures are taken with. --hidden takes no
    // value, so the flag after it is still a flag.
    const auto o = parse({"--hidden", "--fixed-step", "--window", "1920x1061", "--frames", "30",
                          "--screenshot", "C:/shots/a.png"});
    CHECK_MSG(o.ok, o.error);
    CHECK(o.hidden);
    CHECK_EQ(o.windowWidth, uint32_t{1920});
    CHECK_EQ(o.windowHeight, uint32_t{1061});
    CHECK_EQ(o.maxFrames, 30);
    CHECK(o.screenshotPath == "C:/shots/a.png");
    CHECK_MSG(o.warnings.empty(), "a framed, fixed-step hidden run has nothing to warn about");

    // --windowed says the same thing twice, which is not a contradiction.
    CHECK_MSG(parse({"--hidden", "--windowed", "--frames", "1"}).ok, "hidden beside windowed");
}

static void testHiddenAndFullscreenTogetherAreRefused() {
    // A hidden window that went fullscreen would still switch the monitor it
    // is not shown on, which is the thing the flag exists to prevent.
    const auto both = parse({"--hidden", "--fullscreen", "--frames", "1"});
    CHECK_MSG(!both.ok, "both at once is refused");
    CHECK_MSG(both.error.find("--hidden") != std::string::npos &&
                  both.error.find("--fullscreen") != std::string::npos,
              "naming both flags: " + both.error);

    const auto reversed = parse({"--fullscreen", "--frames", "1", "--hidden"});
    CHECK_MSG(!reversed.ok, "in either order");
    CHECK_MSG(both.error == reversed.error, "with the same message: " + both.error);
}

static void testHiddenWithoutAFrameCountWarns() {
    // Accepted - a script may mean to stop it - but a hidden process nobody
    // meant to leave running has no window to close.
    const auto o = parse({"--hidden"});
    CHECK_MSG(o.ok, "a warning, not a refusal: " + o.error);
    CHECK_EQ(o.warnings.size(), std::size_t{1});
    CHECK_MSG(!o.warnings.empty() && o.warnings[0].find("--frames") != std::string::npos,
              "naming the flag that would end it");
}

static void runTests() {
    testRecordAndReplayTakePaths();
    testRecordingAReplayIsRefused();
    testAReplayCanBePinnedAndFramed();
    testFixedStepDefaultsToSixtyHertz();
    testFixedStepTakesAnExplicitStep();
    testFixedStepDoesNotSwallowTheNextFlag();
    testFixedStepRejectsNonsense();
    testTheRealClockIsTheDefault();
    testScreenshotPathIsRead();
    testScreenshotEveryIsRead();
    testScreenshotEveryNeedsAScreenshotPath();
    testScreenshotEveryRefusesAnythingButACountOfAtLeastOne();
    testScreenshotEveryWarnsOnTheRealClock();
    testTheFramesCapturedOverARun();
    testTheStampedNameSitsBeforeTheExtension();
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

    testAWindowSizeIsParsedBothWaysRoundIsSpelled();
    testNoWindowFlagLeavesItToWhoeverElseHasAnOpinion();
    testASizeThatIsAlmostRightIsRefusedRatherThanGuessed();
    testASizeNobodyCouldUseIsRefused();

    testNeitherModeFlagLeavesItToTheManifest();
    testTheModeFlagsAreRead();
    testFullscreenAndWindowedTogetherAreRefused();

    testHiddenIsOffUnlessAskedFor();
    testHiddenComposesWithACaptureRun();
    testHiddenAndFullscreenTogetherAreRefused();
    testHiddenWithoutAFrameCountWarns();

    testScreenshotUiPathIsRead();
    testScreenshotEveryStampsTheUiCaptureToo();
    testTheFramesTheSwapchainIsReadBackFor();

    testACaptureRunOwesEveryFileItNamed();
    testARunThatWroteLessThanItOwedIsAFailure();
}

TEST_MAIN("test_launchoptions", 202)
