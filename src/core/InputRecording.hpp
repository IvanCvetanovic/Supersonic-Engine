#pragma once

#include "core/Input.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Supersonic {

// A run's input, as a file.
//
// Determinism on its own is a property nobody can spend. The engine can already
// prove that the same scene stepped the same number of times produces the same
// state - that is what StateHash is for - but a run is not only a scene and a
// tick count. It is also everything the player did, and that was the one part
// of a run with nowhere to be written down. So a bug a player hit could be
// reproduced only by describing it, which is the thing determinism was supposed
// to stop being necessary.
//
// This is the missing half. Record what each tick was handed, play it back into
// a fresh run, and compare the hashes.
//
// WHAT IS RECORDED IS WHAT THE TICK WAS HANDED, not what the devices did. The
// obvious alternative - store the keys and stick positions and re-derive the
// rest - cannot work, and the reason is easy to miss: some of what a tick reads
// is computed per FRAME. A mouse delta is the clear case, and a UI click is the
// one that surprised us. A frame running three ticks hands the same delta to
// all three; a frame running none hands it to nobody. Re-deriving that on a
// machine whose frames fall differently produces different numbers from the
// same recording, which is a divergence nobody caused. Freezing the resolved
// value has a second benefit that comes free: a replay survives a rebind,
// because the file says the player moved rather than that they held W.
//
// THE FORMAT IS TEXT, and it is delta-encoded. Text because a replay is
// evidence - it arrives attached to a bug report, and being able to read it,
// diff two of them and hand-write one for a test is worth more than the bytes.
// Delta-encoded because most ticks are identical to the one before: a player
// holding a key for three seconds is two lines rather than a hundred and eighty.
//
// LEVELS PERSIST AND EDGES DO NOT, which is the rule the whole encoding turns
// on and the one that is wrong by default. `down` and the axes are levels: they
// carry until a line changes them. `press`, `rel`, `click` and the mouse delta
// belong to exactly the tick that names them and are empty on every other. A
// format that held a press the way it holds a key-down would report one
// keystroke on every tick until the next line - undoing, in the file, the
// latch that exists to give a keypress to exactly one tick.
//
// FLOATS ARE WRITTEN AS THEIR BITS. Nothing in this repository writes a decimal
// float that reads back bit-identical - the codec uses the iostream default of
// six significant digits - so a decimal axis value would not be the value the
// recorded tick actually saw, and the replay would diverge for a reason that is
// not a bug in anything. Bits are also the same stance StateHash already takes
// about comparing floats, for the same reason: they are the only threshold that
// is not an opinion.

// One tick, and the hash of the world at it.
struct ReplayCheckpoint {
    uint64_t tick{0};
    uint64_t hash{0};
};

struct InputRecording {
    // What was being played. Named so a replay can load it, and compared so a
    // file cannot be pointed at the wrong scene in silence.
    std::string scenePath;

    // The authored tick length, kept as the exact float the run used rather
    // than as the rate it was authored from. A rate round-trips through a
    // division and this does not have to.
    float fixedDelta{1.0f / 60.0f};

    // Every tick, materialised. The delta encoding is a property of the FILE,
    // not of this: a caller wants to ask for tick N, and reconstructing it from
    // a cursor would make that either slow or stateful.
    //
    // The cost is honest and worth naming: ten minutes at 60 Hz is thirty-six
    // thousand of these, which is megabytes rather than kilobytes. That is fine
    // for the sessions this is for and would not be for an eight-hour soak, and
    // the fix that day is a streaming reader rather than a different format.
    std::vector<Input::TickInput> ticks;

    // Where the run was checked. Ascending by tick.
    //
    // Tick zero is always one of them and is the most valuable: it is taken
    // after the scene loads and before anything runs, so a mismatch there says
    // "this is not the scene that was recorded" instead of letting the run
    // diverge for four thousand ticks and reporting the symptom.
    //
    // It is necessary and not sufficient, which is worth stating because it
    // reads like a full guarantee. The hash covers what a tick can change; it
    // does not cover everything a scene load establishes, so a scene edited in
    // a way the hash cannot see still replays wrongly and says nothing until
    // the difference reaches a transform.
    std::vector<ReplayCheckpoint> checkpoints;

    // Carried rather than thrown, exactly as LaunchOptions carries a parse
    // failure: the caller prints it and a test asserts on it.
    bool ok{true};
    std::string error;

    // How often Record should take a checkpoint. Every second at 60 Hz.
    //
    // A checkpoint costs a walk of the registry, so one per tick would make a
    // recording run measurably slower than the session it is recording - and
    // the point of a checkpoint is to bound how far a divergence can travel
    // before it is noticed, which a second does well enough to find the tick
    // that caused it.
    static constexpr uint64_t kDefaultCheckpointInterval = 60;

    // Text in, value out. `source` is what a parse error names, so a caller
    // reading from a file passes the path and a test passes anything readable.
    static InputRecording Parse(const std::string& text, const std::string& source);
    static std::string Write(const InputRecording& recording);

    static InputRecording Load(const std::string& path);
    static bool Save(const InputRecording& recording, const std::string& path,
                     std::string& error);

    // What this run was checked against at this tick, or nothing.
    //
    // The whole of what a replay needs to verify itself: at every tick, ask
    // whether the recording knows what the world should have hashed to, and if
    // it does, compare. Returns the checkpoint rather than a bool so a
    // divergence report can carry both numbers and the tick - which is the
    // difference between "the replay diverged" and something a person can act
    // on.
    //
    // Checkpoints are few and in ascending order, so this is a scan. A binary
    // search would be the same speed at these sizes and one more thing that
    // could be wrong at the boundaries.
    const ReplayCheckpoint* CheckpointAt(uint64_t tick) const;
};

} // namespace Supersonic
