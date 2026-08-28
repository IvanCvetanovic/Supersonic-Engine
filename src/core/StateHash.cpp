#include "core/StateHash.hpp"
#include "core/Components.hpp"

#include <cstring>

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

} // namespace

uint64_t Compute(const entt::registry& registry) {
    uint64_t total = 0;
    uint64_t entities = 0;

    for (auto entity : registry.view<const TransformComponent>()) {
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

        const auto& transform = registry.get<const TransformComponent>(entity);
        hash = mixVec3(hash, transform.position);
        hash = mixVec3(hash, transform.rotation);
        hash = mixVec3(hash, transform.scale);

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
    return mix(total, &entities, sizeof(entities));
}

} // namespace Supersonic::StateHash
