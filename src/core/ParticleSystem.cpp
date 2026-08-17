#include "core/ParticleSystem.hpp"
#include "core/JobSystem.hpp"

#include <algorithm>
#include <random>
#include <vector>

namespace Supersonic {

namespace {

// Deterministic per-process RNG. rand() was being used without seeding and
// without any range guarantees; this is both better distributed and reproducible.
float randomUnit() {
    static std::mt19937 engine{12345u};
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(engine);
}

} // namespace

void ParticleSystem::Update(entt::registry& registry, float deltaTime) {
    if (deltaTime <= 0.0f) return;

    // Split into a serial pass and a parallel one.
    //
    // Spawning cannot be spread across threads: it draws from a shared RNG and
    // it resizes the particle vector. Integration can - each particle reads and
    // writes only itself, and the emitter fields it consults are read-only by
    // then. That is where the cost is, since it touches every live particle
    // every frame while only a handful spawn.
    std::vector<ParticleEmitterComponent*> emitters;

    auto view = registry.view<TransformComponent, ParticleEmitterComponent>();
    for (auto entity : view) {
        const auto& transform = view.get<TransformComponent>(entity);
        auto& emitter = view.get<ParticleEmitterComponent>(entity);
        emitters.push_back(&emitter);

        const size_t capacity = std::max<size_t>(1, emitter.maxParticles);
        if (emitter.particles.size() != capacity) {
            emitter.particles.resize(capacity);
        }

        // emitRate is particles per second; the accumulator turns that into
        // whole spawns without tying emission to frame rate.
        emitter.emitAccumulator += emitter.emitRate * deltaTime;
        int toSpawn = static_cast<int>(emitter.emitAccumulator);
        emitter.emitAccumulator -= static_cast<float>(toSpawn);

        for (auto& particle : emitter.particles) {
            if (toSpawn <= 0) break;
            if (particle.active) continue;

            particle.position = transform.position;
            particle.velocity = glm::vec3(
                (randomUnit() - 0.5f) * emitter.velocityRange.x,
                 randomUnit() * emitter.velocityRange.y + 0.5f,
                (randomUnit() - 0.5f) * emitter.velocityRange.z);
            particle.color = emitter.startColor;
            particle.lifetime = emitter.particleLifetime;
            particle.maxLifetime = std::max(0.0001f, emitter.particleLifetime);
            particle.active = true;
            --toSpawn;
        }

    }

    // Every emitter's integration is dispatched before anything is waited on, so
    // several emitters overlap rather than being handled one after another. Each
    // job writes only into its own emitter's vector, which is why this is safe
    // and the ECS iteration above is not.
    for (ParticleEmitterComponent* emitter : emitters) {
        const uint32_t count = static_cast<uint32_t>(emitter->particles.size());
        JobSystem::Dispatch(count, 512u, [emitter, deltaTime](JobSystem::JobArgs args) {
            Particle& particle = emitter->particles[args.jobIndex];
            if (!particle.active) return;

            particle.lifetime -= deltaTime;
            if (particle.lifetime <= 0.0f) {
                particle.active = false;
                return;
            }

            particle.velocity.y -= 1.5f * deltaTime; // light gravity
            particle.position += particle.velocity * deltaTime;

            // endColor is now actually used, rather than being dead data.
            const float t = 1.0f - (particle.lifetime / particle.maxLifetime);
            particle.color = glm::mix(emitter->startColor, emitter->endColor, glm::clamp(t, 0.0f, 1.0f));
        });
    }
    JobSystem::Wait();
}

} // namespace Supersonic
