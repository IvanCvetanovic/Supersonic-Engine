#pragma once

#include <entt/entt.hpp>

#include <string>

namespace Supersonic {

// Somewhere for a game to be.
//
// Until this existed the engine was an application with an editor fused into
// it, and there was nowhere for anyone else's code to run. `SupersonicApp` owned
// the registry privately, and the frame was a fixed sequence of calls to engine
// systems with no seam in it. A game built on this had exactly two options:
// edit SupersonicApp.cpp, or write its logic as script plugins through the C
// ABI - which is deliberately narrow, POD-only, and not the place for a
// simulation's own data structures.
//
// The two real games this was measured against both need this and neither is
// unusual. One has a chain of twenty-two systems it runs in a fixed order every
// tick; the other has nine long-lived singletons that own the match. Neither
// shape fits inside a script component, and neither should have to.
//
// A layer is not a subsystem of the engine. It is a peer: it gets the registry,
// it gets called at defined points in the frame, and the engine makes no other
// assumption about it.
class EngineLayer {
public:
    virtual ~EngineLayer() = default;

    EngineLayer(const EngineLayer&) = delete;
    EngineLayer& operator=(const EngineLayer&) = delete;

    // For the log, the profiler and anything that has to say which layer did
    // something. Required rather than defaulted: an unnamed layer in a stack of
    // twenty is a layer nobody can find.
    virtual const char* Name() const = 0;

    // Once, when the layer joins the stack, and once when it leaves.
    //
    // Attach is where a game builds the entities and singletons it owns.
    // Detach is where it releases anything the registry does not own for it,
    // and it runs BEFORE the registry is destroyed - a layer holding raw
    // handles can still use them here.
    virtual void OnAttach(entt::registry& registry) { (void)registry; }
    virtual void OnDetach(entt::registry& registry) { (void)registry; }

    // Simulation, on the fixed step, exactly as often as physics.
    //
    // This is where a deterministic game belongs, and the reason it is separate
    // from OnUpdate: it is called with the SAME delta every time, zero or more
    // times per frame, so a tick counter here advances at a rate that does not
    // depend on how fast the machine drew the last frame. A simulation driven
    // from a variable delta cannot be replayed, and cannot agree with itself
    // across two machines.
    //
    // Runs only while the scene is simulating - play mode, or a single step -
    // and never while the time-travel debugger is scrubbing, for the same
    // reason physics does not: the frames being replayed have already happened.
    virtual void OnFixedUpdate(entt::registry& registry, float fixedDelta) {
        (void)registry;
        (void)fixedDelta;
    }

    // Once per frame, with the real elapsed time.
    //
    // For everything whose job is to keep up with the display rather than to be
    // reproducible: interpolation, input, camera work, anything reading a clock
    // to animate. Called after the engine's own per-frame systems and before
    // the world transforms are resolved, so a layer that moves something sees
    // it rendered on the same frame rather than the next one.
    virtual void OnUpdate(entt::registry& registry, float deltaTime) {
        (void)registry;
        (void)deltaTime;
    }

protected:
    EngineLayer() = default;
};

} // namespace Supersonic
