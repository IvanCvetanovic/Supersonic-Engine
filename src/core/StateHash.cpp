#include "core/StateHash.hpp"
#include "core/Components.hpp"
#include "core/PhysicsSettings.hpp"

#include <cstring>
#include <vector>

namespace Supersonic::StateHash {

namespace {

uint64_t mix(uint64_t hash, const void* data, size_t bytes) {
    const auto* input = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < bytes; ++i) {
        hash ^= input[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

// Floats by their BYTES, not by value.
//
// Comparing them as numbers would need a tolerance, and a tolerance is a
// decision about how far two runs may drift before it counts - which is
// exactly the question a determinism test exists to answer, so it cannot be
// the test's own parameter. Bit equality is the only threshold that is not an
// opinion.
//
// The cost is that -0.0 and +0.0 hash differently, and any NaN differs from
// any other. Both are correct here: a simulation that produces a negative zero
// where it used to produce a positive one has changed, whether or not the
// difference is visible yet, and a simulation producing NaNs has bigger
// problems than this hash.
uint64_t mixFloat(uint64_t hash, float value) {
    return mix(hash, &value, sizeof(value));
}

uint64_t mixVec3(uint64_t hash, const glm::vec3& v) {
    hash = mixFloat(hash, v.x);
    hash = mixFloat(hash, v.y);
    return mixFloat(hash, v.z);
}

struct Contributor {
    std::string name;
    StateContributor fn;
};

// A function-local static rather than a namespace-scope one, for the reason
// ComponentCodec's registry gives: this lives in a static library linked into
// several binaries, and a function-local static has one definition however many
// translation units reach it.
std::vector<Contributor>& contributors() {
    static std::vector<Contributor> registered;
    return registered;
}

} // namespace

// --- Mixer ------------------------------------------------------------------

void Mixer::Bytes(const void* data, size_t bytes) { m_hash = mix(m_hash, data, bytes); }
void Mixer::Text(std::string_view text) { m_hash = mix(m_hash, text.data(), text.size()); }
void Mixer::F32(float value) { m_hash = mixFloat(m_hash, value); }
void Mixer::F64(double value) { m_hash = mix(m_hash, &value, sizeof(value)); }
void Mixer::I64(int64_t value) { m_hash = mix(m_hash, &value, sizeof(value)); }
void Mixer::U64(uint64_t value) { m_hash = mix(m_hash, &value, sizeof(value)); }

void Mixer::Bool(bool value) {
    // One byte, not sizeof(bool), which is not required to be one and is not the
    // same on every compiler. A padded bool would fold the padding too, and
    // padding is not state.
    const unsigned char byte = value ? 1u : 0u;
    m_hash = mix(m_hash, &byte, 1);
}

// --- registration -----------------------------------------------------------

bool RegisterContributor(std::string name, StateContributor contributor) {
    if (name.empty() || !contributor) return false;

    auto& registered = contributors();
    for (const Contributor& existing : registered) {
        if (existing.name == name) return false;
    }

    registered.push_back(Contributor{std::move(name), std::move(contributor)});
    return true;
}

void ClearContributors() { contributors().clear(); }

std::size_t ContributorCount() { return contributors().size(); }

uint64_t Compute(const entt::registry& registry) {
    uint64_t total = 0;
    uint64_t entities = 0;

    // EVERY LIVE ENTITY, not only the ones with somewhere to be.
    //
    // This used to open on view<const TransformComponent>(), which put both the
    // contribution and the COUNT inside a filter - so an entity without a
    // transform was invisible twice over, and the count could not do the job
    // its comment below claims.
    //
    // Entities without transforms are not an edge case here. ScriptEngine runs
    // "every scripted entity, with or without a place in the world", precisely
    // so a script can drive a HUD element - and a HUD element has no transform
    // by design, because it lives in screen space. The demo scene builds one.
    // So a script could run for an hour on such an entity, keep counters,
    // spawn prefabs and destroy other entities, and the oracle would report
    // that nothing had happened. A whole menu hashed the same as an empty
    // registry.
    //
    // An entity EXISTING is state, which is the other half: spawning one that
    // carries nothing yet must move the number, or a replay diverging by one
    // spawn goes unreported until the spawned thing is given a position.
    for (auto entity : registry.view<entt::entity>()) {
        // Seeded with the entity, so a value found on a different entity is a
        // different state - two crates swapping positions is not the same
        // world, even though the multiset of positions is unchanged.
        //
        // THE INDEX, NOT THE HANDLE. An EnTT handle packs an index and a
        // VERSION into one integer, and the version counts how many times that
        // slot has been recycled - destroying an entity bumps it so a stale
        // handle can be spotted. registry.clear() destroys everything, so a
        // scene loaded a second time into the same registry gets its indices
        // back carrying different versions.
        //
        // Seeding on the whole handle therefore made the hash a function of how
        // many scenes the process had loaded, which is not state. The editor
        // reloads on every Stop and Play; a packaged game reloads the level the
        // player just died in. Both of those changed the number, so "this run
        // reproduces" only ever meant "within one process that has done nothing
        // else" - and the moment a replay is compared against a recording made
        // in a different process, that is no longer a useful thing to have
        // proved. The version is bookkeeping about handles; the index is the
        // identity, and the identity is what the state is keyed on.
        const auto id = static_cast<uint32_t>(entt::to_entity(entity));
        uint64_t hash = mix(1469598103934665603ull, &id, sizeof(id));

        if (const auto* transform = registry.try_get<const TransformComponent>(entity)) {
            hash = mixVec3(hash, transform->position);
            hash = mixVec3(hash, transform->rotation);
            hash = mixVec3(hash, transform->scale);
        }

        if (const auto* body = registry.try_get<const RigidBodyComponent>(entity)) {
            hash = mixVec3(hash, body->velocity);
            hash = mixVec3(hash, body->angularVelocity);
            hash = mixFloat(hash, body->mass);

            // Sleep is part of the state, not an optimisation detail. A body
            // asleep on one machine and awake on another will diverge on the
            // next thing that touches it.
            const unsigned char sleeping = body->isSleeping ? 1u : 0u;
            hash = mix(hash, &sleeping, 1);
            hash = mixFloat(hash, body->sleepTimer);
        }

        // WHERE A SCRIPT IS, which is the rule at the top of the header applied
        // to the one place a game actually writes its logic: a tick writes
        // this and a later tick reads it, so it is state by the same test that
        // admitted velocity.
        //
        // Leaving it out was the dangerous kind of gap, because it makes a
        // replay report SUCCESS while blind. A script keeping a counter, a
        // cooldown or a state-machine phase in here could diverge on tick one
        // and go unnoticed until the tick that turned the counter into a
        // position - by which point the reported divergence names a tick
        // thousands after the one that caused it.
        //
        // Folded in sequence rather than added, unlike entities. The order of
        // `state` is the order the script wrote its names in, and two runs
        // that wrote the same names in a different order have not agreed -
        // that IS a divergence, and the one case where order is the state.
        if (const auto* script = registry.try_get<const ScriptComponent>(entity)) {
            for (const auto& [name, value] : script->state) {
                hash = mix(hash, name.data(), name.size());
                hash = mixFloat(hash, value);
            }

            // The script's own clock, and whether it has started. Derived from
            // the simulation clock rather than accumulated, so it reproduces -
            // but it is read by the next tick, so it still has to be in here or
            // a script enabled one tick apart in two runs hashes the same.
            hash = mixFloat(hash, script->elapsed);
            hash = mixFloat(hash, script->clockOrigin);
            const unsigned char started = script->clockStarted ? 1u : 0u;
            hash = mix(hash, &started, 1);
        }

        // Addition, so the order entities are visited in cannot change the
        // answer. EnTT iterates in an order that depends on how components
        // were added and removed rather than on the state, and folding the
        // per-entity hashes in sequence would report that as a divergence.
        total += hash;
        ++entities;
    }

    // The count, so an entity whose contribution happens to be zero, or a pair
    // that cancels, cannot hide. Folded in at the end where the order-
    // independence above is already established.
    uint64_t hash = mix(total, &entities, sizeof(entities));

    // THE WORLD'S OWN SETTINGS, which are not on any entity.
    //
    // Everything above walks entities, and the registry's context is read by
    // every tick and was hashed by nothing. Most of what lives there is a
    // pointer to a subsystem or a cache and is rightly out - but gravity is
    // not. Every integration step reads it, so two runs under different gravity
    // are two different simulations, and the oracle called them identical right
    // up until something had fallen far enough to notice.
    //
    // That is reachable without anything exotic: gravity is authored per scene
    // and survives registry.clear() in the context, so a scene loaded over
    // another one that failed to overwrite it would diverge with nothing to
    // point at. Narrow on purpose - this is the ctx entry that meets the rule
    // at the top of the header, not an argument for hashing the context
    // wholesale.
    if (const auto* physics = registry.ctx().find<const PhysicsSettings>()) {
        hash = mixVec3(hash, physics->gravity);
        const unsigned char ground = physics->hasGroundPlane ? 1u : 0u;
        hash = mix(hash, &ground, 1);
        hash = mixFloat(hash, physics->groundPlaneY);
    }

    // WHAT THE GAME SAYS ITS STATE IS, for the games whose state is not in the
    // registry at all. See the header for why that is the ordinary case rather
    // than an exotic one.
    //
    // Each contributor is seeded by its own NAME and the results are ADDED, so
    // the order they were registered in cannot change the answer - the same
    // argument the entity walk makes about EnTT's iteration order, one level up.
    // A registration moved between two translation units is not a divergence.
    if (!contributors().empty()) {
        uint64_t gameTotal = 0;
        uint64_t count = 0;

        for (const Contributor& contributor : contributors()) {
            Mixer mixer(mix(1469598103934665603ull, contributor.name.data(),
                            contributor.name.size()));
            contributor.fn(registry, mixer);
            gameTotal += mixer.Value();
            ++count;
        }

        hash = mix(hash, &gameTotal, sizeof(gameTotal));
        hash = mix(hash, &count, sizeof(count));
    }

    // Guarded on there being any, deliberately. A run with nothing registered
    // has to hash exactly what it hashed before this existed, or every recording
    // ever made stops comparing against the engine that made it - and the whole
    // point of the number is that it survives leaving the process.
    return hash;
}

} // namespace Supersonic::StateHash
