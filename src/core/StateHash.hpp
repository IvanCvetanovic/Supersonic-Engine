#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

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
//
// And a game's own state goes in too, through RegisterContributor below - for
// games whose authoritative state is not in the registry at all, which is the
// ordinary shape for anything built on an EngineLayer rather than out of
// components. Everything above walks a registry; that walks whatever the game
// says its state is.
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

// --- A game's own state -----------------------------------------------------
//
// THE HOLE THIS CLOSES. Everything above walks a registry, and a game's
// authoritative state is not always in one. Wolf Brigade's is a C++ object
// graph - a match, its units, a map of resources, an event bus - owned by an
// EngineLayer, which is the shape this engine deliberately supports and
// recommends for exactly that kind of game.
//
// So `Compute` would have walked a registry containing none of it and returned
// a number that agreed with itself perfectly. Not "might miss a component":
// structurally unable to see any of it. A replay of that game would have
// reproduced the engine and reported success while the game diverged on tick
// one.
//
// That is the same failure this file has already been fixed for twice - the
// seed that carried a recycle count, and script state left outside - and both
// times the symptom was a replay that SUCCEEDED while blind. ComponentCodec was
// opened to a game's own components for the same reason and much earlier; the
// oracle was not, and the asymmetry was the bug.

// Folds bytes into one hash, the way `Compute` folds its own.
//
// Handed to a contributor rather than letting it return a number, so a game
// cannot accidentally use a different fold. Two byte-identical states hashing
// differently because one side rolled its own FNV would be a divergence nobody
// caused, reported by the thing that exists to report real ones.
//
// Floats go in BY THEIR BITS, for the reason stated at the top of the .cpp: a
// tolerance is a decision about how far two runs may drift before it counts,
// which is the question the oracle exists to answer.
class Mixer {
public:
    explicit Mixer(uint64_t seed) : m_hash(seed) {}

    void Bytes(const void* data, size_t bytes);
    void Text(std::string_view text);

    void F32(float value);
    void F64(double value);
    void I64(int64_t value);
    void U64(uint64_t value);
    void Bool(bool value);

    uint64_t Value() const { return m_hash; }

private:
    uint64_t m_hash;
};

// Everything a game wants in the oracle, folded into `out`.
//
// Called with the registry because a game's state usually hangs off it - a
// pointer or a struct in `registry.ctx()`, which is where this engine already
// keeps a layer's singletons.
using StateContributor = std::function<void(const entt::registry& registry, Mixer& out)>;

// Registers one under `name`, which is also its SEED.
//
// Seeded by name and ADDED to the total rather than folded in sequence, so the
// order contributors were registered in cannot change the answer - the same
// argument the entity walk makes about EnTT's iteration order, applied one
// level up. Two games in one process, or a registration moved between two
// translation units, must not read as a divergence.
//
// Returns false if the name is already registered. Global rather than
// per-registry, matching ComponentCodec: it maps a KIND of state to how it is
// read, and that does not change between two scenes.
bool RegisterContributor(std::string name, StateContributor contributor);

// Forgets every contributor. For tests, and for a game shutting down.
void ClearContributors();

std::size_t ContributorCount();

} // namespace StateHash
} // namespace Supersonic
