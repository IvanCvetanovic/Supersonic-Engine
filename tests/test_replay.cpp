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
#include "core/Components.hpp"
#include "core/StateHash.hpp"
#include "core/UIInput.hpp"
#include "core/Input.hpp"
#include "core/ViewportInfo.hpp"

#include <algorithm>
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


// --- The producer -----------------------------------------------------------
//
// EVERY CASE ABOVE BUILDS ITS TICKS BY HAND. That is right for testing the
// format, and it means the function that produces a tick in the actual engine -
// Input::CaptureTickInput, the one thing on the recording path a real session
// goes through - was reached by nothing at all. It has exactly one caller,
// SupersonicApp, which no test runs. A field it forgot to read would be absent
// from every recording ever written and green here from top to bottom, because
// both sides of every comparison above are hand-written.
//
// So this drives the real devices, captures, and asserts the capture says what
// was driven. It is the instrument the pointer work needs, and it is worth
// having on its own.

namespace {

void resetDevices() {
    Input::ClearBindings();
    Input::SetCursorMode(CursorMode::Normal);
    Input::SuppressCursorCapture(false);
    Input::SetWindowFocused(true);
    Input::Update(RawInputState{});
    Input::Update(RawInputState{});
    Input::BeginTickInput();
}

bool listed(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

float axisIn(const Input::TickInput& tick, const std::string& name) {
    for (const auto& [axis, value] : tick.axes) {
        if (axis == name) return value;
    }
    return std::nanf("");
}

} // namespace

static void testACaptureSaysWhatTheDevicesWereDoing() {
    resetDevices();
    Input::BindActionKey("Fire", Key::Space);
    Input::BindActionKey("Jump", Key::W);
    Input::BindAxisKeys("MoveX", Key::D, Key::A);

    // An idle frame first: an action seen for the first time is seeded with
    // what it reads, so binding and pressing together is deliberately not an
    // edge. The same note sits beside try_emplace in Input.cpp.
    Input::Update(RawInputState{});
    Input::BeginTickInput();

    RawInputState state{};
    state.keys[Key::Space] = true;
    state.keys[Key::D] = true;
    state.mousePosition = glm::vec2(300.0f, 200.0f);
    Input::Update(state);
    Input::BeginTickInput();

    const Input::TickInput captured = Input::CaptureTickInput();

    CHECK_MSG(listed(captured.down, "Fire"), "a held action is written as held");
    CHECK_MSG(listed(captured.pressed, "Fire"), "and its edge is written too");
    CHECK_MSG(!listed(captured.down, "Jump"), "an action nobody touched is not held");
    CHECK_MSG(!listed(captured.pressed, "Jump"), "and did not fire an edge");

    // EVERY BOUND AXIS, including the ones at rest. The format's reader treats
    // a missing axis as zero, which is only safe because the writer never omits
    // one - so the claim belongs here, at the writer.
    CHECK_NEAR(axisIn(captured, "MoveX"), 1.0f);
    CHECK_MSG(captured.axes.size() == Input::AxisNames().size(),
              "every bound axis is written, not only the ones doing something");

    // And released, on the tick the key comes up.
    Input::Update(RawInputState{});
    Input::BeginTickInput();
    const Input::TickInput after = Input::CaptureTickInput();
    CHECK_MSG(listed(after.released, "Fire"), "the release is written on its own tick");
    CHECK_MSG(!listed(after.down, "Fire"), "and it is no longer held");
    CHECK_NEAR(axisIn(after, "MoveX"), 0.0f);

    resetDevices();
}

// A captured tick survives the file, and the simulation cannot tell the
// difference between the devices and the recording.
static void testACapturedTickReplaysAsItself() {
    resetDevices();
    Input::BindActionKey("Fire", Key::Space);
    Input::BindAxisKeys("MoveX", Key::D, Key::A);
    Input::Update(RawInputState{});
    Input::BeginTickInput();

    RawInputState state{};
    state.keys[Key::Space] = true;
    state.keys[Key::A] = true;
    Input::Update(state);
    Input::BeginTickInput();

    InputRecording recording = emptyRun(1);
    recording.ticks[0] = Input::CaptureTickInput();

    const InputRecording parsed = roundTrip(recording);
    CHECK_MSG(parsed.ticks.size() == 1, "one tick in, one tick out");
    if (parsed.ticks.size() != 1) return;

    CHECK_MSG(parsed.ticks[0] == recording.ticks[0],
              "a captured tick is the same tick after a trip through the file");

    // Read back through the queries a game actually calls, rather than through
    // the struct's own fields: the applier is a separate piece of code from the
    // parser and can lose what the parser kept.
    Input::BeginReplayedTick(parsed.ticks[0]);
    CHECK_MSG(Input::IsDown("Fire"), "the replayed tick reads as held");
    CHECK_MSG(Input::TickWasPressed("Fire"), "and as pressed");
    CHECK_NEAR(Input::GetAxis("MoveX"), -1.0f);
    Input::EndReplayedTick();

    // Hand the devices back at rest. A case that leaves a key down hands it to
    // whichever case runs next, where it arrives with no explanation.
    resetDevices();
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

// --- the whole chain, which is the claim ------------------------------------
//
// Everything above tests the FILE: that what went in comes out. None of it
// tests the thing the file exists for, which is that feeding a recorded run
// back produces the same world - and that is a different claim, because it goes
// through Input's replay override and out the other side into state.
//
// It is also the claim most likely to pass for the wrong reason. A scene that
// does not read input reproduces whatever you feed it, so a green end-to-end
// run against the demo scene says nothing at all: measured, changing a recorded
// mouse delta and replaying it produces an identical hash, because nothing in
// that scene reads one. The simulation below therefore reads input and writes
// state, which is what makes the last case here able to fail.

namespace {

// A stand-in for a game's OnFixedUpdate: reads what the tick was handed and
// turns it into world state, which is the only path that matters.
void simulateTick(entt::registry& registry, entt::entity entity) {
    auto& transform = registry.get<TransformComponent>(entity);
    transform.position.x += Input::GetAxis("MoveX") * 0.1f;
    transform.position.z += Input::MouseDelta().x * 0.01f;
    if (Input::TickWasPressed("Jump")) transform.position.y += 1.0f;
    if (Input::TickWasReleased("Jump")) transform.position.y -= 0.5f;
    if (Input::IsDown("Fire")) transform.rotation.y += 0.05f;
}

uint64_t runThrough(const InputRecording& recording) {
    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity);

    for (const Input::TickInput& tick : recording.ticks) {
        Input::BeginReplayedTick(tick);
        UIInput::BeginReplayedTickClicks(registry, tick.clicked);
        simulateTick(registry, entity);
        Input::EndReplayedTick();
    }
    return StateHash::Compute(registry);
}

// A session with something in it: movement, a jump, a held trigger, mouse look.
InputRecording aSession() {
    InputRecording recording = emptyRun(40);
    for (size_t i = 0; i < recording.ticks.size(); ++i) {
        Input::TickInput& tick = recording.ticks[i];
        tick.axes = { { "MoveX", i < 20 ? 1.0f : -0.5f } };
        tick.mouseDelta = glm::vec2(static_cast<float>(i) * 0.25f, 0.0f);
        if (i >= 10 && i < 30) tick.down = { "Fire" };
    }
    recording.ticks[5].pressed = { "Jump" };
    recording.ticks[5].down = { "Jump" };
    recording.ticks[6].released = { "Jump" };
    return recording;
}

} // namespace

static void testARecordedRunReproducesThroughTheFile() {
    // THE CLAIM, end to end: a session, written to text, read back, fed into a
    // simulation that reads input, lands on the same state to the bit.
    const InputRecording original = aSession();
    const uint64_t direct = runThrough(original);

    const InputRecording parsed = roundTrip(original);
    const uint64_t replayed = runThrough(parsed);

    CHECK_MSG(direct == replayed,
              "a run replayed from its file must land where it landed: " +
                  std::to_string(direct) + " vs " + std::to_string(replayed));

    // And again, because two agreeing could be two copies of one accident.
    CHECK_MSG(runThrough(parsed) == direct, "and again on a third run");
}

static void testChangingOneTicksInputChangesWhereTheRunEnds() {
    // THE ONE THAT STOPS THE TEST ABOVE PASSING FOR NOTHING. If the recorded
    // input were not actually reaching the simulation, every recording would
    // reproduce every other and the test above would be green and worthless.
    // That is not hypothetical - it is exactly what happens against the demo
    // scene, which reads no input at all.
    const InputRecording original = aSession();
    const uint64_t baseline = runThrough(original);

    InputRecording nudged = original;
    nudged.ticks[12].axes = { { "MoveX", 0.75f } };
    CHECK_MSG(runThrough(nudged) != baseline, "one axis, on one tick, moves the world");

    InputRecording unpressed = original;
    unpressed.ticks[5].pressed.clear();
    CHECK_MSG(runThrough(unpressed) != baseline, "and so does one press that did not happen");

    InputRecording steadier = original;
    steadier.ticks[20].mouseDelta = glm::vec2(0.0f);
    CHECK_MSG(runThrough(steadier) != baseline, "and one tick of mouse movement");
}

static void testAnEdgeHeldForOneTickTooLongIsADifferentRun() {
    // The property the format's level/edge split exists for, measured in state
    // rather than in parsed structs. A press carried to the following tick is
    // an extra jump, so the world ends somewhere else - which is what makes
    // getting the encoding wrong a bug somebody would eventually notice, and
    // what makes it worth having a test that notices first.
    InputRecording once = emptyRun(4);
    once.ticks[1].pressed = { "Jump" };

    InputRecording twice = emptyRun(4);
    twice.ticks[1].pressed = { "Jump" };
    twice.ticks[2].pressed = { "Jump" };

    CHECK_MSG(runThrough(once) != runThrough(twice),
              "one press and two presses are different runs");

    // And the file agrees with itself about which of the two it holds.
    CHECK_MSG(runThrough(roundTrip(once)) == runThrough(once),
              "the single press survives the round trip as a single press");
}

static void testTheDevicesComeBackWhenTheTickEnds() {
    // Everything after the tick runs per frame - the editor camera, the UI,
    // ImGui - and a replay that left its recorded input installed would take the
    // view away from whoever is watching the replay.
    //
    // The reset is the point of the case, not scaffolding: "the devices have
    // the input back" is a claim about what the DEVICES read, so it needs them
    // in a known state. Input is file-static and outlives any one case, so
    // without this the assertion below reads whatever the case before it left
    // held - and this one used to pass only because nothing above it had ever
    // touched a real key.
    resetDevices();

    InputRecording recording = emptyRun(1);
    recording.ticks[0].axes = { { "MoveX", 1.0f } };
    recording.ticks[0].down = { "Fire" };

    Input::BeginReplayedTick(recording.ticks[0]);
    CHECK_MSG(Input::ReplayingTick(), "the recorded tick is in force");
    CHECK_NEAR(Input::GetAxis("MoveX"), 1.0f);
    CHECK_MSG(Input::IsDown("Fire"), "and the recorded action reads as held");

    Input::EndReplayedTick();
    CHECK_MSG(!Input::ReplayingTick(), "and it is not in force afterwards");
    CHECK_NEAR(Input::GetAxis("MoveX"), 0.0f);
    CHECK_MSG(!Input::IsDown("Fire"), "the devices have the input back");
}


// --- the pointer ------------------------------------------------------------

static void testAPointerThatStopsMovingStaysWhereItIs() {
    // THE CASE THE LEVEL ENCODING EXISTS FOR, and the one the obvious mistake
    // fails. The mouse delta is skipped whenever it is zero, because a mouse
    // that did not move contributes nothing. Copy that rule for the POSITION
    // and every tick the player holds still is written as no position at all -
    // which reads back as the origin, and every tap after the first still tick
    // lands in the top-left corner of the screen.
    InputRecording original = emptyRun(6);
    for (size_t i = 0; i < original.ticks.size(); ++i) {
        original.ticks[i].mousePosition = glm::vec2(640.0f, 360.0f);
    }
    // Moves once, on tick 3, and STAYS. The ticks after it write no pointer at
    // all, so they are the ones that fail if the reader does not carry it.
    for (size_t i = 3; i < original.ticks.size(); ++i) {
        original.ticks[i].mousePosition = glm::vec2(700.0f, 360.0f);
    }

    const InputRecording parsed = roundTrip(original);
    if (parsed.ticks.size() != 6) { CHECK_MSG(false, "six ticks came back"); return; }

    CHECK_NEAR(parsed.ticks[0].mousePosition.x, 640.0f);
    CHECK_NEAR(parsed.ticks[2].mousePosition.x, 640.0f);
    CHECK_NEAR(parsed.ticks[3].mousePosition.x, 700.0f);

    // The ones AFTER the move, which is where an encoder that only wrote
    // changes but a reader that did not carry them forward would show it.
    CHECK_NEAR(parsed.ticks[4].mousePosition.x, 700.0f);
    CHECK_NEAR(parsed.ticks[5].mousePosition.x, 700.0f);
    CHECK_NEAR(parsed.ticks[5].mousePosition.y, 360.0f);
}

static void testAPointerAtTheOriginCostsNothing() {
    // The other half of the level rule: both sides start at the origin, so a
    // session that never moves the mouse writes no pointer at all. Without
    // this the encoder puts a line on tick zero of every recording ever made.
    InputRecording original = emptyRun(50);
    const std::string text = InputRecording::Write(original);
    CHECK_MSG(text.find(" pointer ") == std::string::npos,
              "a pointer that never left the origin is never written");

    // And one that DID move writes exactly once for the move.
    InputRecording moved = emptyRun(50);
    for (size_t i = 20; i < moved.ticks.size(); ++i) {
        moved.ticks[i].mousePosition = glm::vec2(12.0f, 34.0f);
    }
    const std::string movedText = InputRecording::Write(moved);
    size_t count = 0;
    for (size_t at = movedText.find(" pointer "); at != std::string::npos;
         at = movedText.find(" pointer ", at + 1)) {
        ++count;
    }
    CHECK_MSG(count == 1, "thirty ticks at one position is one line, got " +
                              std::to_string(count));
}

// --- the touches ------------------------------------------------------------

namespace {

Contact aContact(int id, glm::vec2 position, glm::vec2 delta, ContactPhase phase) {
    Contact contact;
    contact.id = id;
    contact.position = position;
    contact.delta = delta;
    contact.phase = phase;
    return contact;
}

} // namespace

static void testATouchSurvivesTheFileWithEveryFieldIntact() {
    InputRecording original = emptyRun(4);
    original.ticks[1].contacts = {
        aContact(0, glm::vec2(101.5f, 202.25f), glm::vec2(0.0f), ContactPhase::Began),
        aContact(7, glm::vec2(-3.75f, 9.125f), glm::vec2(1.5f, -2.5f), ContactPhase::Moved),
    };
    original.ticks[2].contacts = {
        aContact(0, glm::vec2(111.0f, 222.0f), glm::vec2(9.5f, 19.75f), ContactPhase::Ended),
    };

    const InputRecording parsed = roundTrip(original);
    if (parsed.ticks.size() != 4) { CHECK_MSG(false, "four ticks came back"); return; }

    CHECK_MSG(parsed.ticks[1].contacts.size() == 2, "both touches came back");
    if (parsed.ticks[1].contacts.size() != 2) return;

    // FIELD BY FIELD. A comparison of the whole vector would pass on a parser
    // that read the id and defaulted everything else, if the defaults happened
    // to match - and the phase in particular is the field a gesture machine
    // lives on.
    const Contact& first = parsed.ticks[1].contacts[0];
    CHECK_MSG(first.id == 0, "the id");
    CHECK_NEAR(first.position.x, 101.5f);
    CHECK_NEAR(first.position.y, 202.25f);
    CHECK_MSG(first.phase == ContactPhase::Began, "the phase");

    const Contact& second = parsed.ticks[1].contacts[1];
    CHECK_MSG(second.id == 7, "the second id, which is not its index");
    CHECK_NEAR(second.position.x, -3.75f);
    CHECK_NEAR(second.delta.x, 1.5f);
    CHECK_NEAR(second.delta.y, -2.5f);
    CHECK_MSG(second.phase == ContactPhase::Moved, "a Moved is not a Began");

    CHECK_MSG(parsed.ticks[2].contacts.size() == 1, "and the tick after has one");
    if (parsed.ticks[2].contacts.size() == 1) {
        CHECK_MSG(parsed.ticks[2].contacts[0].phase == ContactPhase::Ended,
                  "an Ended survives, which is the frame a tap is decided on");
    }

    // AN EDGE, NOT A LEVEL. A tick with no touch line has no touches, and a
    // finger carried forward across the ticks after it lifted would make every
    // tap in a session last until the next one.
    CHECK_MSG(parsed.ticks[3].contacts.empty(),
              "a tick after the touches is empty rather than carrying them");
    CHECK_MSG(parsed.ticks[0].contacts.empty(), "and so is the tick before");
}

static void testACorruptTouchIsRejectedRatherThanGuessedAt() {
    // The file promises to reproduce a run. A contact with a field missing, or
    // a phase that is not one, is a corrupt recording - and reading it as a
    // plausible touch puts the divergence a long way from the line that caused
    // it.
    const std::string header =
        "SUPERSONICREPLAY 1\n"
        "scene assets/scenes/MainScene.scene\n"
        "step 3c888889\n"
        "ticks 1\n"
        "action Fire\n";
    const std::string footer = "end 1\n";

    struct Case { const char* body; const char* why; };
    const Case bad[] = {
        { "t 0 touch 0,42ca0000,43520000,00000000,00000000\n", "five fields is not a contact" },
        { "t 0 touch 0,42ca0000,43520000,00000000,00000000,1,2\n", "and neither is seven" },
        { "t 0 touch 0,42ca0000,43520000,00000000,00000000,9\n", "9 is not a phase" },
        { "t 0 touch 0,zzzz0000,43520000,00000000,00000000,1\n", "a position must be hex" },
        { "t 0 touch 0\n", "an id on its own is not a contact" },
    };

    for (const Case& one : bad) {
        const InputRecording out = InputRecording::Parse(header + one.body + footer, "corrupt");
        CHECK_MSG(!out.ok, std::string(one.why) + ", but it parsed");
        CHECK_MSG(!out.error.empty(), "and the refusal says something");
    }

    // The same line, correct, must parse - or the cases above prove only that
    // the parser refuses everything, which every one of them would also show.
    const InputRecording good = InputRecording::Parse(
        header + "t 0 touch 0,42ca0000,43520000,00000000,00000000,1\n" + footer, "good");
    CHECK_MSG(good.ok, "the well-formed version parses: " + good.error);
    CHECK_MSG(good.ticks.size() == 1 && good.ticks[0].contacts.size() == 1,
              "and carries its one contact");
    if (!good.ticks.empty() && good.ticks[0].contacts.size() == 1) {
        CHECK_MSG(good.ticks[0].contacts[0].phase == ContactPhase::Moved, "as a Moved");
        CHECK_NEAR(good.ticks[0].contacts[0].position.x, 101.0f);
    }
}

// --- the queries ------------------------------------------------------------

static void testAReplayedTickAnswersThePointerQueriesTheGameCalls() {
    // WHERE A RECORDED FIELD BECOMES REAL. Storing the pointer in the tick,
    // writing it and parsing it back is worth nothing until Input::MousePosition
    // and the contact queries consult it - and a game calls those, not the
    // struct. This project has shipped the other shape: a field written,
    // parsed, and never read, with every test green.
    resetDevices();

    // The DEVICES are somewhere else entirely, which is the point: a replay is
    // watched by somebody whose mouse is wherever they left it.
    RawInputState elsewhere{};
    elsewhere.mousePosition = glm::vec2(5.0f, 5.0f);
    Input::Update(elsewhere);

    InputRecording recording = emptyRun(1);
    recording.ticks[0].mousePosition = glm::vec2(640.0f, 360.0f);
    recording.ticks[0].contacts = {
        aContact(3, glm::vec2(640.0f, 360.0f), glm::vec2(2.0f, 0.0f), ContactPhase::Moved),
    };

    Input::BeginReplayedTick(recording.ticks[0]);
    CHECK_NEAR(Input::MousePosition().x, 640.0f);
    CHECK_NEAR(Input::MousePosition().y, 360.0f);
    CHECK_MSG(Input::ContactCount() == 1, "the recorded contact is the one in force");

    const Contact replayed = Input::GetContact(0);
    CHECK_MSG(replayed.id == 3, "with its id");
    CHECK_MSG(replayed.phase == ContactPhase::Moved, "and its phase");
    CHECK_NEAR(replayed.position.x, 640.0f);

    Contact byId;
    CHECK_MSG(Input::TryGetContact(3, byId), "and it is findable by id");
    CHECK_NEAR(byId.delta.x, 2.0f);
    CHECK_MSG(!Input::TryGetContact(99, byId), "while one that is not there is not found");

    // And the devices come back afterwards, like every other replayed read.
    Input::EndReplayedTick();
    CHECK_NEAR(Input::MousePosition().x, 5.0f);
    CHECK_MSG(Input::ContactCount() == 0, "the watcher's own pointer is theirs again");

    resetDevices();
}

static void testACaptureCarriesThePointerAndTheTouches() {
    // Through the real devices, into the real capture: the half of the claim
    // that hand-built ticks cannot make.
    resetDevices();

    RawInputState state{};
    state.mousePosition = glm::vec2(321.0f, 123.0f);
    state.mouseButtons[0] = true;
    Input::SynthesiseMouseContact(state);
    Input::Update(state);
    Input::BeginTickInput();

    const Input::TickInput captured = Input::CaptureTickInput();
    CHECK_NEAR(captured.mousePosition.x, 321.0f);
    CHECK_NEAR(captured.mousePosition.y, 123.0f);
    CHECK_MSG(captured.contacts.size() == 1,
              "the press the player is making is part of what the tick was handed");
    if (captured.contacts.size() == 1) {
        CHECK_MSG(captured.contacts[0].phase == ContactPhase::Began, "as a Began");
        CHECK_NEAR(captured.contacts[0].position.x, 321.0f);
    }

    // All the way round, and read back through the queries.
    InputRecording recording = emptyRun(1);
    recording.ticks[0] = captured;
    const InputRecording parsed = roundTrip(recording);
    CHECK_MSG(parsed.ticks.size() == 1 && parsed.ticks[0] == captured,
              "a captured tick with a touch in it is the same tick after the file");

    if (parsed.ticks.size() == 1) {
        Input::BeginReplayedTick(parsed.ticks[0]);
        CHECK_MSG(Input::ContactCount() == 1, "and replays as one contact");
        CHECK_NEAR(Input::MousePosition().x, 321.0f);
        Input::EndReplayedTick();
    }

    resetDevices();
}


// --- the viewport -----------------------------------------------------------

static void testTheRectangleThePictureWasDrawnIntoSurvivesTheFile() {
    InputRecording original = emptyRun(5);
    for (size_t i = 0; i < original.ticks.size(); ++i) {
        original.ticks[i].hasViewport = true;
        original.ticks[i].viewportMin = glm::vec2(320.0f, 48.0f);
        original.ticks[i].viewportSize = glm::vec2(1280.0f, 720.0f);
        original.ticks[i].pointerOverGame = true;
    }
    original.ticks[3].pointerOverGame = false;   // the pointer moved onto a panel

    const InputRecording parsed = roundTrip(original);
    if (parsed.ticks.size() != 5) { CHECK_MSG(false, "five ticks came back"); return; }

    CHECK_MSG(parsed.ticks[0].hasViewport, "the first tick has one");
    CHECK_NEAR(parsed.ticks[0].viewportMin.x, 320.0f);
    CHECK_NEAR(parsed.ticks[0].viewportMin.y, 48.0f);
    CHECK_NEAR(parsed.ticks[0].viewportSize.x, 1280.0f);
    CHECK_NEAR(parsed.ticks[0].viewportSize.y, 720.0f);

    // Carried across the ticks that wrote nothing, like the pointer.
    CHECK_MSG(parsed.ticks[2].hasViewport, "and so does the tick that wrote no line");
    CHECK_NEAR(parsed.ticks[2].viewportMin.x, 320.0f);

    CHECK_MSG(parsed.ticks[1].pointerOverGame, "the pointer was over the game");
    CHECK_MSG(!parsed.ticks[3].pointerOverGame,
              "and was not, on the tick it moved onto a panel");
    CHECK_MSG(parsed.ticks[4].pointerOverGame, "and was again after");
}

static void testAViewportThatGoesAwayIsNotAViewportOfNoSize() {
    // The two states a lazy encoding runs together. A tick where nothing
    // published a viewport must come back as nothing published one - NOT as a
    // rectangle of zero width, which reaches a ray cast and divides by it.
    InputRecording original = emptyRun(4);
    for (size_t i = 0; i < 2; ++i) {
        original.ticks[i].hasViewport = true;
        original.ticks[i].viewportMin = glm::vec2(10.0f, 20.0f);
        original.ticks[i].viewportSize = glm::vec2(800.0f, 600.0f);
        original.ticks[i].pointerOverGame = true;
    }
    // Ticks 2 and 3 have none at all.

    const InputRecording parsed = roundTrip(original);
    if (parsed.ticks.size() != 4) { CHECK_MSG(false, "four ticks came back"); return; }

    CHECK_MSG(parsed.ticks[1].hasViewport, "held while it was published");
    CHECK_MSG(!parsed.ticks[2].hasViewport,
              "gone on the tick it stopped being published, rather than carried");
    CHECK_MSG(!parsed.ticks[3].hasViewport, "and still gone the tick after");

    // The absence is WRITTEN, or the reader would carry the last rectangle
    // forward and every tick after it would act on a stale layout.
    const std::string text = InputRecording::Write(original);
    CHECK_MSG(text.find(" view -") != std::string::npos,
              "the tick it went away says so, rather than saying nothing");
}

static void testASessionWithNoViewportWritesNoViewport() {
    // Both sides start at "nobody published one", so the common case - a
    // headless run, or any scene nobody is pointing at - costs nothing.
    const std::string text = InputRecording::Write(emptyRun(60));
    CHECK_MSG(text.find(" view ") == std::string::npos,
              "a session that never published a viewport never writes one");

    // And one that publishes the same rectangle for a minute writes it once.
    InputRecording steady = emptyRun(60);
    for (Input::TickInput& tick : steady.ticks) {
        tick.hasViewport = true;
        tick.viewportMin = glm::vec2(0.0f);
        tick.viewportSize = glm::vec2(1920.0f, 1080.0f);
        tick.pointerOverGame = true;
    }
    const std::string steadyText = InputRecording::Write(steady);
    size_t count = 0;
    for (size_t at = steadyText.find(" view "); at != std::string::npos;
         at = steadyText.find(" view ", at + 1)) {
        ++count;
    }
    CHECK_MSG(count == 1, "sixty ticks of one layout is one line, got " + std::to_string(count));
}

static void testACorruptViewportIsRejected() {
    const std::string header =
        "SUPERSONICREPLAY 1\n"
        "scene assets/scenes/MainScene.scene\n"
        "step 3c888889\n"
        "ticks 1\n"
        "action Fire\n";
    const std::string footer = "end 1\n";

    struct Case { const char* body; const char* why; };
    const Case bad[] = {
        { "t 0 view 00000000,00000000,44f00000\n", "three fields is not a viewport" },
        { "t 0 view 00000000,00000000,44f00000,44870000,1,2\n", "and neither is six" },
        { "t 0 view 00000000,00000000,44f00000,44870000,7\n", "7 is not a yes or a no" },
        { "t 0 view 00000000,zzzzzzzz,44f00000,44870000,1\n", "a corner must be hex" },
    };
    for (const Case& one : bad) {
        const InputRecording out = InputRecording::Parse(header + one.body + footer, "corrupt");
        CHECK_MSG(!out.ok, std::string(one.why) + ", but it parsed");
    }

    const InputRecording good = InputRecording::Parse(
        header + "t 0 view 00000000,00000000,44f00000,44870000,1\n" + footer, "good");
    CHECK_MSG(good.ok, "the well-formed version parses: " + good.error);
    if (good.ok && good.ticks.size() == 1) {
        CHECK_MSG(good.ticks[0].hasViewport, "and carries a viewport");
        CHECK_NEAR(good.ticks[0].viewportSize.x, 1920.0f);
        CHECK_MSG(good.ticks[0].pointerOverGame, "with the pointer over the game");
    }
}

static void testApplyingARecordedViewportPutsItWhereALayerLooks() {
    // The other end of the translation, and the half that is easy to leave out:
    // a tick's viewport reaching the registry a game layer reads, and being
    // REMOVED again on a tick that had none.
    entt::registry registry;

    Input::TickInput withOne;
    withOne.hasViewport = true;
    withOne.viewportMin = glm::vec2(64.0f, 96.0f);
    withOne.viewportSize = glm::vec2(1024.0f, 768.0f);
    withOne.pointerOverGame = true;

    BeginReplayedTickViewport(registry, withOne);
    const auto* installed = registry.ctx().find<ViewportInfo>();
    CHECK_MSG(installed != nullptr, "a layer can find the recorded viewport");
    if (installed != nullptr) {
        CHECK_NEAR(installed->rect.min.x, 64.0f);
        CHECK_NEAR(installed->Size().x, 1024.0f);
        CHECK_NEAR(installed->Size().y, 768.0f);
        CHECK_MSG(installed->pointerOverGame, "and whether the pointer was over the game");
    }

    // AND THE ERASE. Skipping the insert would leave the rectangle above
    // standing, so a game that correctly did nothing during the recording
    // would start acting during the replay, on a layout from a tick that has
    // already passed.
    const Input::TickInput withNone;
    BeginReplayedTickViewport(registry, withNone);
    CHECK_MSG(registry.ctx().find<ViewportInfo>() == nullptr,
              "a tick that had no viewport replays as a tick with no viewport");

    // And capture is its inverse.
    BeginReplayedTickViewport(registry, withOne);
    Input::TickInput captured;
    CaptureViewportInto(registry, captured);
    CHECK_MSG(captured.hasViewport, "what is published is what is captured");
    CHECK_NEAR(captured.viewportMin.y, 96.0f);
    CHECK_NEAR(captured.viewportSize.y, 768.0f);

    registry.ctx().erase<ViewportInfo>();
    Input::TickInput empty;
    empty.hasViewport = true;
    CaptureViewportInto(registry, empty);
    CHECK_MSG(!empty.hasViewport, "and nothing published is nothing captured");
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

// --- the wheel --------------------------------------------------------------
//
// Version 2 of the format. A game that zooms on the wheel from inside the tick
// could not be replayed before it: the wheel was per frame and not in the file.

static void testTheWheelIsWrittenAsAnEdgeAndReadBackBitForBit() {
    // An EDGE, like the mouse delta: the notches belong to the tick that was
    // handed them. A reader that carried them forward would zoom on every tick
    // after one turn of the wheel.
    InputRecording original = emptyRun(5);
    original.ticks[1].scroll = 1.5f;
    original.ticks[3].scroll = -0.0f; // written, because its bits are not zero

    const InputRecording parsed = roundTrip(original);
    if (parsed.ticks.size() != 5) {
        CHECK_MSG(false, "five ticks came back");
        return;
    }
    CHECK_MSG(bitsOf(parsed.ticks[1].scroll) == bitsOf(1.5f), "the notches come back bit for bit");
    CHECK_MSG(bitsOf(parsed.ticks[2].scroll) == 0u, "and are not carried to the next tick");
    CHECK_MSG(bitsOf(parsed.ticks[3].scroll) == bitsOf(-0.0f), "negative zero stays negative zero");
    CHECK_MSG(sameTick(parsed.ticks[1], original.ticks[1]), "the whole tick is the tick written");

    const std::string quiet = InputRecording::Write(emptyRun(50));
    CHECK_MSG(quiet.find(" scroll ") == std::string::npos, "a wheel nobody turned is never written");
}

static void testAReplayedTickAnswersTheWheel() {
    // Where the field becomes real: a game calls TickScroll, not the struct.
    resetDevices();
    InputRecording recording = emptyRun(1);
    recording.ticks[0].scroll = 2.0f;

    Input::BeginReplayedTick(recording.ticks[0]);
    CHECK_NEAR(Input::TickScroll(), 2.0f);
    Input::EndReplayedTick();
    CHECK_NEAR(Input::TickScroll(), 0.0f);
    resetDevices();
}

static void testACaptureCarriesTheWheel() {
    resetDevices();
    RawInputState turned{};
    turned.scroll = 3.0f;
    Input::Update(turned);
    Input::BeginTickInput();
    CHECK_NEAR(Input::CaptureTickInput().scroll, 3.0f);
    resetDevices();
}

static void testAVersionOneRecordingStillReads() {
    // Version 2 only ADDED the wheel, so a version 1 file is a version 2 file
    // that never turned it. Refusing one would throw away every recording made
    // before this build for nothing. Made by writing a real file and turning
    // its magic line back, so it has every line a real one has.
    std::string text = InputRecording::Write(emptyRun(2));
    const std::string current = "SUPERSONICREPLAY 2";
    CHECK_MSG(text.rfind(current, 0) == 0, "this build writes version 2");
    if (text.rfind(current, 0) == 0) text.replace(0, current.size(), "SUPERSONICREPLAY 1");

    const InputRecording parsed = InputRecording::Parse(text, "v1.replay");
    CHECK_MSG(parsed.ok, "a version 1 replay still parses: " + parsed.error);
    CHECK_MSG(parsed.ticks.size() == 2 && parsed.ticks[0].scroll == 0.0f, "with no wheel in it");
}

static void runTests() {
    testTheWheelIsWrittenAsAnEdgeAndReadBackBitForBit();
    testAReplayedTickAnswersTheWheel();
    testACaptureCarriesTheWheel();
    testAVersionOneRecordingStillReads();
    testAnEmptyRunSurvivesTheRoundTrip();
    testEveryTickComesBackExactlyAsItWentIn();
    testALevelCarriesAndAnEdgeDoesNot();
    testHoldingAKeyForThreeSecondsIsTwoLines();
    testFloatsComeBackToTheBit();
    testANegativeZeroStaysNegative();
    testAnActionNameWithASpaceIsFine();
    testCheckpointsSurviveAndStayInOrder();
    testACaptureSaysWhatTheDevicesWereDoing();
    testACapturedTickReplaysAsItself();
    testAPointerThatStopsMovingStaysWhereItIs();
    testAPointerAtTheOriginCostsNothing();
    testATouchSurvivesTheFileWithEveryFieldIntact();
    testACorruptTouchIsRejectedRatherThanGuessedAt();
    testAReplayedTickAnswersThePointerQueriesTheGameCalls();
    testACaptureCarriesThePointerAndTheTouches();
    testTheRectangleThePictureWasDrawnIntoSurvivesTheFile();
    testAViewportThatGoesAwayIsNotAViewportOfNoSize();
    testASessionWithNoViewportWritesNoViewport();
    testACorruptViewportIsRejected();
    testApplyingARecordedViewportPutsItWhereALayerLooks();
    testTheRunCanBeAskedWhatATickShouldHaveHashedTo();
    testATruncatedFileIsRejected();
    testGarbageIsRejectedRatherThanGuessedAt();
    testAParseErrorNamesTheFileAndTheLine();
    testAHandWrittenFileParses();
    testAFileWrittenOnTheOtherPlatformStillReads();
    testARecordedRunReproducesThroughTheFile();
    testChangingOneTicksInputChangesWhereTheRunEnds();
    testAnEdgeHeldForOneTickTooLongIsADifferentRun();
    testTheDevicesComeBackWhenTheTickEnds();
    testSaveAndLoadRoundTripThroughAFile();
    testLoadingAFileThatIsNotThereSaysSo();
}

TEST_MAIN("test_replay", 242)
