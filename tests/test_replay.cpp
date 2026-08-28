// Tests for recording a run's input and reading it back.
//
// What was impossible before: sending somebody a bug. A run was already
// reproducible given a scene and a tick count - that is what StateHash proves -
// but a run is also everything the player did, and that had nowhere to be
// written down. So a bug could only be described, which is the thing
// determinism was supposed to stop being necessary.
//
// Two properties carry the whole file and both are easy to get wrong in a way
// that still looks like it works. Levels persist between ticks and edges do
// not: a format that carried a press the way it carries a key-down would report
// one keystroke on every following tick, undoing in the file the latch that
// exists to give a keypress to exactly one tick. And floats are written as
// their bits, because nothing in this repository writes a decimal float that
// reads back bit-identical - a recorded axis would not be the value the tick
// saw, and the replay would diverge for a reason that is not a bug in anything.

#include "TestHarness.hpp"

#include "core/InputRecording.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Supersonic;

namespace {

namespace fs = std::filesystem;

fs::path scratchDirectory() {
    return fs::temp_directory_path() / "supersonic_replay_test";
}

uint32_t bitsOf(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// A recording of `count` ticks with nothing in them, to hang a case off.
InputRecording emptyRun(size_t count) {
    InputRecording recording;
    recording.scenePath = "assets/scenes/MainScene.scene";
    recording.fixedDelta = 1.0f / 60.0f;
    recording.ticks.assign(count, Input::TickInput{});
    return recording;
}

// Write it, read it back, and insist the read succeeded. Returns the parsed
// value so a case can compare against what it put in.
InputRecording roundTrip(const InputRecording& original) {
    const std::string text = InputRecording::Write(original);
    InputRecording parsed = InputRecording::Parse(text, "roundtrip");
    CHECK_MSG(parsed.ok, "a recording this file wrote must parse: " + parsed.error);
    return parsed;
}

bool sameTick(const Input::TickInput& a, const Input::TickInput& b) { return a == b; }

} // namespace

static void testAnEmptyRunSurvivesTheRoundTrip() {
    // The degenerate case first, because the delta encoding means a run where
    // nothing happens writes no tick lines at all - so this is also the test
    // that the tick COUNT survives on its own, with no data to infer it from.
    const InputRecording original = emptyRun(120);
    const InputRecording parsed = roundTrip(original);

    CHECK_EQ(parsed.ticks.size(), size_t{120});
    CHECK_MSG(parsed.scenePath == original.scenePath, "the scene came back");
    CHECK_MSG(bitsOf(parsed.fixedDelta) == bitsOf(original.fixedDelta),
              "and the step, to the bit");
}

static void testEveryTickComesBackExactlyAsItWentIn() {
    InputRecording original = emptyRun(6);

    original.ticks[0].down = { "Fire" };
    original.ticks[0].pressed = { "Fire" };
    original.ticks[0].axes = { { "MoveX", 1.0f }, { "MoveY", -1.0f } };
    original.ticks[0].mouseDelta = glm::vec2(3.5f, -2.25f);

    original.ticks[1].down = { "Fire" };
    original.ticks[1].axes = { { "MoveX", 1.0f }, { "MoveY", -1.0f } };

    original.ticks[2].down = { "Fire", "Jump" };
    original.ticks[2].pressed = { "Jump" };
    original.ticks[2].axes = { { "MoveX", 0.0f }, { "MoveY", -1.0f } };

    original.ticks[3].axes = { { "MoveX", 0.0f }, { "MoveY", -1.0f } };
    original.ticks[3].released = { "Fire", "Jump" };

    original.ticks[4].axes = { { "MoveX", 0.0f }, { "MoveY", -1.0f } };
    original.ticks[4].clicked = { 7, 12 };

    original.ticks[5].axes = { { "MoveX", 0.0f }, { "MoveY", -1.0f } };

    const InputRecording parsed = roundTrip(original);
    CHECK_EQ(parsed.ticks.size(), original.ticks.size());

    for (size_t i = 0; i < original.ticks.size() && i < parsed.ticks.size(); ++i) {
        CHECK_MSG(sameTick(original.ticks[i], parsed.ticks[i]),
                  "tick " + std::to_string(i) + " came back different");
    }
}

static void testALevelCarriesAndAnEdgeDoesNot() {
    // THE ONE THE FORMAT TURNS ON. A tick that writes no line inherits the
    // previous tick's held keys and axis values, and inherits none of its
    // edges. Getting the second half wrong is the dangerous one, because the
    // recording would still parse, still have the right number of ticks, and
    // report one keypress as a hundred and eighty.
    InputRecording original = emptyRun(4);

    original.ticks[0].down = { "Fire" };
    original.ticks[0].pressed = { "Fire" };
    // Ticks 1..3 hold Fire and press nothing.
    for (size_t i = 1; i < 4; ++i) original.ticks[i].down = { "Fire" };

    const InputRecording parsed = roundTrip(original);
    CHECK_EQ(parsed.ticks.size(), size_t{4});
    if (parsed.ticks.size() != 4) return;

    CHECK_MSG(parsed.ticks[0].pressed.size() == 1, "the press is on the tick that made it");
    for (size_t i = 1; i < 4; ++i) {
        CHECK_MSG(parsed.ticks[i].down.size() == 1,
                  "tick " + std::to_string(i) + " still holds the key");
        CHECK_MSG(parsed.ticks[i].pressed.empty(),
                  "but tick " + std::to_string(i) + " did not press it again");
    }

    // And the file really is small, which is the reason for the encoding rather
    // than a nice-to-have: three of the four ticks write no line.
    const std::string text = InputRecording::Write(original);
    size_t tickLines = 0;
    size_t start = 0;
    while ((start = text.find("\nt ", start)) != std::string::npos) {
        ++tickLines;
        ++start;
    }
    CHECK_MSG(tickLines == 1,
              "only the tick that changed something is written, got " + std::to_string(tickLines));
}

static void testHoldingAKeyForThreeSecondsIsTwoLines() {
    // The saving, stated as the thing it is for. A player holding a movement
    // key at 60 Hz for three seconds is one line when it goes down and one when
    // it comes up, not a hundred and eighty.
    InputRecording original = emptyRun(200);
    for (size_t i = 10; i < 190; ++i) original.ticks[i].down = { "MoveForward" };

    const std::string text = InputRecording::Write(original);
    size_t tickLines = 0;
    size_t start = 0;
    while ((start = text.find("\nt ", start)) != std::string::npos) {
        ++tickLines;
        ++start;
    }
    CHECK_MSG(tickLines == 2, "down and up, got " + std::to_string(tickLines) + " lines");

    const InputRecording parsed = roundTrip(original);
    CHECK_MSG(parsed.ticks[9].down.empty(), "not held before it was pressed");
    CHECK_MSG(parsed.ticks[10].down.size() == 1, "held from the tick it went down");
    CHECK_MSG(parsed.ticks[189].down.size() == 1, "still held on the last tick of the hold");
    CHECK_MSG(parsed.ticks[190].down.empty(), "and let go after");
}

static void testFloatsComeBackToTheBit() {
    // Nothing in this repository writes a decimal float that reads back
    // bit-identical - ComponentCodec uses the iostream default of six
    // significant digits. A recorded axis written that way is not the value the
    // tick saw, so the replay diverges for a reason that is not a bug in
    // anything, and the divergence is tiny and intermittent, which is the worst
    // kind to go looking for.
    const float values[] = {
        0.0f,
        -0.0f,
        1.0f,
        -1.0f,
        0.1f,                       // not representable, and six digits loses it
        1.0f / 3.0f,
        0.18f,                      // the stick deadzone
        3.4028234663852886e+38f,    // FLT_MAX
        1.1754943508222875e-38f,    // the smallest normal
        1.4012984643e-45f,          // a denormal
        123456.789f,
    };

    InputRecording original = emptyRun(sizeof(values) / sizeof(values[0]));
    for (size_t i = 0; i < original.ticks.size(); ++i) {
        original.ticks[i].axes = { { "Value", values[i] } };
        original.ticks[i].mouseDelta = glm::vec2(values[i], -values[i]);
    }

    const InputRecording parsed = roundTrip(original);
    CHECK_EQ(parsed.ticks.size(), original.ticks.size());

    for (size_t i = 0; i < original.ticks.size() && i < parsed.ticks.size(); ++i) {
        CHECK_MSG(parsed.ticks[i].axes.size() == 1, "the axis is there");
        if (parsed.ticks[i].axes.size() != 1) continue;

        CHECK_MSG(bitsOf(parsed.ticks[i].axes[0].second) == bitsOf(values[i]),
                  "axis " + std::to_string(i) + " changed its bits on the way through");
        CHECK_MSG(bitsOf(parsed.ticks[i].mouseDelta.x) == bitsOf(values[i]),
                  "and so did the mouse delta at " + std::to_string(i));
    }
}

static void testANegativeZeroStaysNegative() {
    // Pedantic and cheap, and it caught a real one: the writer skipped a mouse
    // delta whose components compared equal to zero, and negative zero does.
    // The file promises bit-exactness in exactly one place and this was the
    // case where it would not have held.
    InputRecording original = emptyRun(1);
    original.ticks[0].mouseDelta = glm::vec2(-0.0f, 0.0f);

    const InputRecording parsed = roundTrip(original);
    CHECK_MSG(bitsOf(parsed.ticks[0].mouseDelta.x) == bitsOf(-0.0f),
              "negative zero is a different float and must survive as one");
}

static void testAnActionNameWithASpaceIsFine() {
    // Nothing stops a game binding "Move Left", and a format that split names
    // on whitespace would turn that into two actions and an index nobody can
    // resolve. Names go one per line, rest-of-line, so this cannot happen.
    InputRecording original = emptyRun(2);
    original.ticks[0].down = { "Move Left" };
    original.ticks[0].axes = { { "Look X", 0.5f } };
    original.ticks[1].down = { "Move Left" };
    original.ticks[1].axes = { { "Look X", 0.5f } };

    const InputRecording parsed = roundTrip(original);
    CHECK_MSG(parsed.ticks[0].down.size() == 1 && parsed.ticks[0].down[0] == "Move Left",
              "the action name survived with its space");
    CHECK_MSG(parsed.ticks[1].axes.size() == 1 && parsed.ticks[1].axes[0].first == "Look X",
              "and so did the axis name");
}

static void testCheckpointsSurviveAndStayInOrder() {
    InputRecording original = emptyRun(180);
    original.checkpoints = { { 0, 0x0123456789abcdefull },
                             { 60, 0xfedcba9876543210ull },
                             { 120, 1ull },
                             { 180, 0ull } };

    const InputRecording parsed = roundTrip(original);
    CHECK_EQ(parsed.checkpoints.size(), size_t{4});
    if (parsed.checkpoints.size() != 4) return;

    for (size_t i = 0; i < 4; ++i) {
        CHECK_MSG(parsed.checkpoints[i].tick == original.checkpoints[i].tick,
                  "checkpoint " + std::to_string(i) + " kept its tick");
        CHECK_MSG(parsed.checkpoints[i].hash == original.checkpoints[i].hash,
                  "checkpoint " + std::to_string(i) + " kept its hash");
    }
}

static void testTheRunCanBeAskedWhatATickShouldHaveHashedTo() {
    // How a replay verifies itself: at every tick, ask whether the recording
    // knows what the world should look like, and compare only when it does.
    InputRecording recording = emptyRun(180);
    recording.checkpoints = { { 0, 11ull }, { 60, 22ull }, { 120, 33ull } };

    const ReplayCheckpoint* atZero = recording.CheckpointAt(0);
    CHECK_MSG(atZero != nullptr && atZero->hash == 11ull, "tick zero is checked");

    const ReplayCheckpoint* atSixty = recording.CheckpointAt(60);
    CHECK_MSG(atSixty != nullptr && atSixty->hash == 22ull, "and so is tick sixty");

    // The ticks in between are not checked, and saying so is the point: a
    // caller that treated a missing checkpoint as a mismatch would report a
    // divergence on every tick of a perfect replay.
    CHECK_MSG(recording.CheckpointAt(1) == nullptr, "a tick nobody checked reports nothing");
    CHECK_MSG(recording.CheckpointAt(59) == nullptr, "nor the one before a checkpoint");
    CHECK_MSG(recording.CheckpointAt(9999) == nullptr, "nor one past the end");
}

// --- what a broken file has to do -----------------------------------------
//
// A replay arrives from somewhere else, which is the whole point of it, so
// every one of these is a file somebody will actually send. None may crash and
// none may be read as valid.

static void testATruncatedFileIsRejected() {
    // THE ONE THE END MARKER EXISTS FOR. Delta encoding makes half a file
    // syntactically perfect: the reader carries the last levels forward, hands
    // back exactly the number of ticks the header promised, and reports success
    // on half a session. Nothing in the data can reveal that.
    InputRecording original = emptyRun(200);
    for (size_t i = 0; i < 200; i += 7) original.ticks[i].pressed = { "Fire" };

    const std::string whole = InputRecording::Write(original);
    CHECK_MSG(InputRecording::Parse(whole, "whole").ok, "the whole file is fine");

    // Cut on a LINE BOUNDARY, which is the case the end marker exists for. A
    // cut in the middle of a line is caught by the grammar and proves nothing
    // about truncation - the file below is perfectly well-formed as far as it
    // goes, and only the missing end marker says it is not all there.
    const size_t midpoint = whole.rfind('\n', whole.size() / 2);
    CHECK_MSG(midpoint != std::string::npos, "the recording has several lines");
    const std::string half = whole.substr(0, midpoint + 1);

    const InputRecording parsed = InputRecording::Parse(half, "half.replay");
    CHECK_MSG(!parsed.ok, "half a recording must not read as a whole one");
    CHECK_MSG(parsed.error.find("truncated") != std::string::npos,
              "and it must say so: " + parsed.error);

    // The other kind, for completeness: a file cut mid-line is rejected by the
    // grammar rather than by the end marker, and must still be rejected.
    const std::string ragged = whole.substr(0, whole.size() / 2);
    CHECK_MSG(!InputRecording::Parse(ragged, "ragged.replay").ok,
              "and a file cut mid-line is not readable either");
}

static void testGarbageIsRejectedRatherThanGuessedAt() {
    struct Case {
        const char* text;
        const char* what;
    };
    const Case cases[] = {
        { "", "an empty file" },
        { "hello\n", "something that is not a replay at all" },
        { "SUPERSONICREPLAY\n", "a magic line with no version" },
        { "SUPERSONICREPLAY 99\nticks 0\nend 0\n", "a version from the future" },
        { "SUPERSONICREPLAY 1\nticks 2\nnonsense 4\nend 2\n", "a keyword nobody knows" },
        { "SUPERSONICREPLAY 1\nstep zzzzzzzz\nticks 0\nend 0\n", "a step that is not hex" },
        { "SUPERSONICREPLAY 1\nstep 00000000\nticks 0\nend 0\n", "a step of zero" },
        { "SUPERSONICREPLAY 1\nticks two\nend 2\n", "a tick count that is not a number" },
        { "SUPERSONICREPLAY 1\nticks 4\nend 5\n", "an end that disagrees with the header" },
        { "SUPERSONICREPLAY 1\nticks 4\nt 0 down 0\nend 4\n", "an action index nobody declared" },
        { "SUPERSONICREPLAY 1\nticks 4\naction Fire\nt 0 down\nend 4\n", "a segment with no value" },
        { "SUPERSONICREPLAY 1\nticks 4\naction Fire\nt 2 press 0\nt 1 press 0\nend 4\n",
          "ticks that go backwards" },
        { "SUPERSONICREPLAY 1\nticks 4\naction Fire\nt 9 press 0\nend 4\n",
          "a tick past the declared count" },
        { "SUPERSONICREPLAY 1\nticks 4\ncheckpoint 900 0\nend 4\n",
          "a checkpoint the run never reached" },
        { "SUPERSONICREPLAY 1\nticks 4\naction Fire\nt 0 press 0\naction Jump\nend 4\n",
          "an action declared after the ticks started" },
        { "SUPERSONICREPLAY 1\nticks 4\naction Fire\nt 0 mouse 3f800000\nend 4\n",
          "a mouse delta with one component" },
        { "SUPERSONICREPLAY 1\nticks 4\naxis X\nt 0 axis 0\nend 4\n",
          "an axis assignment with no value" },
    };

    for (const Case& one : cases) {
        const InputRecording parsed = InputRecording::Parse(one.text, "broken.replay");
        CHECK_MSG(!parsed.ok, std::string("must be rejected: ") + one.what);
        CHECK_MSG(!parsed.error.empty(), std::string("and must say why: ") + one.what);
    }
}

static void testAParseErrorNamesTheFileAndTheLine() {
    const InputRecording parsed =
        InputRecording::Parse("SUPERSONICREPLAY 1\nticks 4\nnonsense 1\nend 4\n",
                              "session.replay");
    CHECK_MSG(!parsed.ok, "the file is broken");
    CHECK_MSG(parsed.error.find("session.replay") != std::string::npos,
              "the message names the file: " + parsed.error);
    CHECK_MSG(parsed.error.find(":3:") != std::string::npos,
              "and the line: " + parsed.error);
    CHECK_MSG(parsed.error.find("nonsense") != std::string::npos,
              "and quotes what it choked on: " + parsed.error);
    CHECK_MSG(parsed.error.find("nothing was replayed") != std::string::npos,
              "and says what was left alone: " + parsed.error);
}

static void testAHandWrittenFileParses() {
    // A format nobody can write by hand is a format nobody can shrink a bug
    // report down to. This is the smallest interesting replay, typed out.
    const std::string text =
        "SUPERSONICREPLAY 1\n"
        "scene assets/scenes/MainScene.scene\n"
        "step 3c888889\n"
        "ticks 3\n"
        "action Fire\n"
        "axis MoveX\n"
        "checkpoint 0 0000000000000001\n"
        "t 0 down - axis 0=3f800000\n"
        "t 1 down 0 press 0\n"
        "t 2 down - rel 0 click 4\n"
        "end 3\n";

    const InputRecording parsed = InputRecording::Parse(text, "typed");
    CHECK_MSG(parsed.ok, "a hand-written replay must parse: " + parsed.error);
    if (!parsed.ok) return;

    CHECK_EQ(parsed.ticks.size(), size_t{3});
    CHECK_MSG(parsed.scenePath == "assets/scenes/MainScene.scene", "the scene path read whole");

    CHECK_MSG(parsed.ticks[0].down.empty(), "nothing held on tick 0");
    CHECK_MSG(parsed.ticks[0].axes.size() == 1 && parsed.ticks[0].axes[0].second == 1.0f,
              "MoveX is fully over");

    CHECK_MSG(parsed.ticks[1].down.size() == 1, "Fire held on tick 1");
    CHECK_MSG(parsed.ticks[1].pressed.size() == 1, "and pressed on tick 1");
    CHECK_MSG(parsed.ticks[1].axes.size() == 1 && parsed.ticks[1].axes[0].second == 1.0f,
              "the axis carried without being restated");

    CHECK_MSG(parsed.ticks[2].down.empty(), "released by tick 2");
    CHECK_MSG(parsed.ticks[2].released.size() == 1, "and the release is reported once");
    CHECK_MSG(parsed.ticks[2].clicked.size() == 1 && parsed.ticks[2].clicked[0] == 4u,
              "and the button was clicked");
}

static void testAFileWrittenOnTheOtherPlatformStillReads() {
    // A replay travels between machines - that is what it is for - and a bug
    // report written on Windows and opened on Linux must not fail on the line
    // endings.
    InputRecording original = emptyRun(3);
    original.ticks[1].pressed = { "Fire" };

    std::string text = InputRecording::Write(original);
    std::string withCrLf;
    for (const char c : text) {
        if (c == '\n') withCrLf += '\r';
        withCrLf += c;
    }

    const InputRecording parsed = InputRecording::Parse(withCrLf, "windows.replay");
    CHECK_MSG(parsed.ok, "carriage returns are not a parse error: " + parsed.error);
    CHECK_MSG(parsed.ok && parsed.ticks.size() == 3 && parsed.ticks[1].pressed.size() == 1,
              "and the content survived them");
}

// --- the disk ---------------------------------------------------------------

static void testSaveAndLoadRoundTripThroughAFile() {
    const fs::path directory = scratchDirectory();
    std::error_code ec;
    fs::remove_all(directory, ec);

    // Deliberately inside a directory that does not exist yet: ofstream will
    // not create one, and a path somebody chose for a recording is exactly
    // where that bites.
    const fs::path path = directory / "nested" / "session.replay";

    InputRecording original = emptyRun(10);
    original.ticks[3].pressed = { "Jump" };
    original.ticks[3].mouseDelta = glm::vec2(1.5f, -0.25f);
    original.checkpoints = { { 0, 42ull }, { 10, 99ull } };

    std::string error;
    CHECK_MSG(InputRecording::Save(original, path.string(), error),
              "saving must create the directory it was pointed at: " + error);

    const InputRecording loaded = InputRecording::Load(path.string());
    CHECK_MSG(loaded.ok, "and it must read back: " + loaded.error);
    if (loaded.ok) {
        CHECK_EQ(loaded.ticks.size(), size_t{10});
        CHECK_MSG(sameTick(loaded.ticks[3], original.ticks[3]), "the tick with something in it");
        CHECK_EQ(loaded.checkpoints.size(), size_t{2});
    }

    fs::remove_all(directory, ec);
}

static void testLoadingAFileThatIsNotThereSaysSo() {
    const InputRecording loaded =
        InputRecording::Load((scratchDirectory() / "no_such.replay").string());
    CHECK_MSG(!loaded.ok, "a missing file is not an empty recording");
    CHECK_MSG(loaded.error.find("no_such.replay") != std::string::npos,
              "and the message names it: " + loaded.error);
}

static void runTests() {
    testAnEmptyRunSurvivesTheRoundTrip();
    testEveryTickComesBackExactlyAsItWentIn();
    testALevelCarriesAndAnEdgeDoesNot();
    testHoldingAKeyForThreeSecondsIsTwoLines();
    testFloatsComeBackToTheBit();
    testANegativeZeroStaysNegative();
    testAnActionNameWithASpaceIsFine();
    testCheckpointsSurviveAndStayInOrder();
    testTheRunCanBeAskedWhatATickShouldHaveHashedTo();
    testATruncatedFileIsRejected();
    testGarbageIsRejectedRatherThanGuessedAt();
    testAParseErrorNamesTheFileAndTheLine();
    testAHandWrittenFileParses();
    testAFileWrittenOnTheOtherPlatformStillReads();
    testSaveAndLoadRoundTripThroughAFile();
    testLoadingAFileThatIsNotThereSaysSo();
}

TEST_MAIN("test_replay", 90)
