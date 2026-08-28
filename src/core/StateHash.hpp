#pragma once

#include <entt/entt.hpp>

#include <cstdint>

namespace Supersonic {

// A number that stands for the simulation's state.
//
// Determinism is a claim nobody can check by looking. A screenshot compares
// what was drawn, which is a lossy function of the state and quantised to eight
// bits per channel, so two runs that have already diverged can still photograph
// identically - and two that agree perfectly can differ by a pixel for reasons
// that are nothing to do with the simulation. Asserting on the state itself is
// the only test that means what it says.
//
// This is the oracle: run a scene twice, hash it at the same tick, and the
// numbers agree or the simulation is not reproducible.
//
// What goes in is everything a step can change and a later step can read -
// transforms, rigid bodies, and where a script is - keyed by entity so two
// entities swapping values is a different state. What stays out is everything
// derived, cached or presentational: world matrices are recomputed from
// transforms every frame, render bounds come from the mesh, and a hash
// including them would report a difference that no simulation step caused.
//
// EVERY LIVE ENTITY is walked, not only the ones carrying those components.
// That reads like a detail and is the difference between an oracle and a
// rubber stamp: an entity is state whether or not it has been given a place to
// be, and the version of this that filtered on TransformComponent put the
// COUNT inside the filter too - so an entity without one was invisible, and so
// was the mechanism meant to stop things being invisible. A scene of scripted
// HUD elements, which is a shape this engine explicitly supports, hashed
// identically to an empty registry.
//
// The rule to apply when adding a component: does a tick write it, and can a
// later tick read it? Script state is the case that makes this concrete. It
// looks like scratch, it is not serialised, and a cooldown kept in it decides
// what the next tick does - so leaving it out did not make a replay fail, it
// made a replay SUCCEED while blind, which is the failure worth naming.
namespace StateHash {

// Hashes every simulated entity in the registry.
//
// Order-independent by construction. EnTT's iteration order is a property of
// how components were added and removed, not of the state, so two registries
// holding identical values can iterate them differently - and a hash that
// folded them in sequence would call that a divergence. Each entity's
// contribution is computed independently and combined with an operation that
// does not care what order they arrive in.
uint64_t Compute(const entt::registry& registry);

} // namespace StateHash
} // namespace Supersonic
