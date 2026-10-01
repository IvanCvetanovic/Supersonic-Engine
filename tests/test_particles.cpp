// The particle system: what an emitter is allowed to ask for.
//
// ParticleSystem had no suite. Everything here is about the one number a scene
// file controls that sizes an allocation - maxParticles - because that is where
// a file's claim became memory with nothing in between.

#include "TestHarness.hpp"
#include "core/Components.hpp"
#include "core/ParticleSystem.hpp"

#include <entt/entt.hpp>

#include <exception>

using namespace Supersonic;

static void testAnEmitterIsNotSizedFromAFilesClaim() {
    // maxParticles is a uint32 out of a scene. Four billion of the 56-byte
    // particles is over two hundred gigabytes, resized on the first tick after the
    // scene loaded.
    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity);
    auto& emitter = registry.emplace<ParticleEmitterComponent>(entity);
    emitter.maxParticles = 4000000000u;
    emitter.emitRate = 0.0f;

    bool threw = false;
    try {
        ParticleSystem::Update(registry, 1.0f / 60.0f);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK_MSG(!threw, "an absurd maxParticles is clamped, not thrown");
    CHECK_MSG(emitter.particles.size() == ParticleSystem::kMaxParticlesPerEmitter,
              "and the emitter keeps the most it is allowed");
}

static void testAnOrdinaryEmitterIsUntouched() {
    // The clamp is a ceiling and not a rounding: every size an effect uses is
    // exactly what it asked for.
    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity);
    auto& emitter = registry.emplace<ParticleEmitterComponent>(entity);
    emitter.maxParticles = 256;
    emitter.emitRate = 0.0f;
    ParticleSystem::Update(registry, 1.0f / 60.0f);
    CHECK_EQ(static_cast<int>(emitter.particles.size()), 256);

    // At the ceiling itself: allowed in full.
    emitter.maxParticles = static_cast<uint32_t>(ParticleSystem::kMaxParticlesPerEmitter);
    ParticleSystem::Update(registry, 1.0f / 60.0f);
    CHECK(emitter.particles.size() == ParticleSystem::kMaxParticlesPerEmitter);
}

static void testAnEmitterWithNoRoomStillKeepsOne() {
    // The existing floor, unchanged by the ceiling above it.
    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity);
    auto& emitter = registry.emplace<ParticleEmitterComponent>(entity);
    emitter.maxParticles = 0;
    emitter.emitRate = 0.0f;
    ParticleSystem::Update(registry, 1.0f / 60.0f);
    CHECK_EQ(static_cast<int>(emitter.particles.size()), 1);
}

static void runTests() {
    testAnEmitterIsNotSizedFromAFilesClaim();
    testAnOrdinaryEmitterIsUntouched();
    testAnEmitterWithNoRoomStillKeepsOne();
}

TEST_MAIN("test_particles", 4)
