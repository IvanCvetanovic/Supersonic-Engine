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
    //
    // AUTHORED, and no longer the physics step. Those were one number because
    // there was only one loop, which meant a game wanting to simulate at 20 Hz
    // paid for 60 and a game wanting 120 could not have it. Physics keeps its
    // own rate underneath this - a solver has a stability reason for its step
    // that has nothing to do with how often a game wants to think.
    float fixedDelta{1.0f / 60.0f};

    // How far into the NEXT tick the frame being drawn is, 0 to 1.
    //
    // Without it a 20 Hz simulation is drawn at 20 Hz however fast the display
    // runs, and looks it. With it the render transform is a lerp between the
    // last two ticks, which is what makes a low tick rate a simulation decision
    // rather than a visible one.
    //
    // Written once per frame AFTER the tick loop, so it is the remainder the
    // loop could not consume. Deliberately NOT part of the simulation: nothing
    // inside a tick may read it, or the tick's result would depend on the frame
    // rate again and everything below would be undone.
    float alpha{0.0f};

    // Simulated time the loop was unable to run and threw away, in seconds.
    //
    // A frame that arrives late has to be answered one of two ways: run the
    // ticks it owes, which under sustained load never catches up and spirals,
    // or drop them, which makes every timer in the game run slow. There is no
    // third answer, so the engine drops - and RECORDS IT, which is the part
    // that was missing. It used to zero the accumulator silently, so a machine
    // that could not keep up ran its missions short and said nothing.
    //
    // Two sources, both counted here: a tick loop that hit its ceiling, and a
    // frame longer than kMaxFrameDelta, which is clamped before the accumulator
    // ever sees it. The second one is older and easier to miss.
    //
    // A game that cares can read this - to warn, to lower its own detail, or to
    // refuse to keep playing a competitive match. A test asserts it is zero.
    double droppedSeconds{0.0};

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
