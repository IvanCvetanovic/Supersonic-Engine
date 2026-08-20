#pragma once

#include <cstdint>

namespace Supersonic {

// The simulation's own clock, in the registry's context.
//
// The engine had no notion of simulated time at all. Everything that needed a
// clock read the frame delta - the real, variable, machine-dependent time the
// last frame took to draw - and accumulated it. `ScriptComponent::elapsed` did
// exactly that and drove `std::sin` off the result, so a script's motion was a
// function of how fast the display was keeping up.
//
// That is not a small inaccuracy. It means the same scene, run twice on the
// same machine from the same starting state, does not do the same thing: three
// runs of one binary over one scene produced three different images. Nothing
// can be replayed, no test can assert on what a simulation produced, two
// machines in a network game disagree immediately, and a bug reported by a
// player cannot be reproduced by the person fixing it.
//
// This is the alternative: a counter that advances once per fixed step and a
// time derived from it by multiplication. Same number of ticks in, same number
// out, on any machine at any frame rate.
struct SimulationClock {
    // How many fixed steps have run since the simulation started. The number
    // that identifies a moment - what a replay seeks to, what a desync report
    // names, what a state hash is taken at.
    uint64_t tick{0};

    // The step length every tick advances by. Constant for the run.
    float fixedDelta{1.0f / 60.0f};

    // Derived, not accumulated: tick * fixedDelta computed fresh rather than
    // summed. A float summed sixty times a second drifts measurably within an
    // hour and drifts DIFFERENTLY depending on where it started, which would
    // put the reproducibility back where it was found.
    //
    // Double because the multiply is exact for far longer: at 1/60 s a float
    // stops being able to represent every tick boundary after about nine
    // hours, and a double after longer than anything will run.
    double Seconds() const {
        return static_cast<double>(tick) * static_cast<double>(fixedDelta);
    }

    // Seconds as the float most callers want. The narrowing happens once, here,
    // rather than in every caller with its own opinion about when.
    float SecondsF() const { return static_cast<float>(Seconds()); }
};

} // namespace Supersonic
