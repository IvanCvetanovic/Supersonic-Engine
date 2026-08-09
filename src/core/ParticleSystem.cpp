#include "core/ParticleSystem.hpp"
#include "core/Components.hpp"
#include <cstdlib>

namespace Engine {

std::vector<Particle> ParticleSystem::s_particlePool(200);

void ParticleSystem::Update(entt::registry& registry, float deltaTime) {
    auto view = registry.view<TransformComponent, ParticleEmitterComponent>();
    for (auto entity : view) {
        const auto& transform = view.get<TransformComponent>(entity);
        const auto& emitter = view.get<ParticleEmitterComponent>(entity);

        // Emit new particles
        for (auto& particle : s_particlePool) {
            if (!particle.active) {
                particle.position = transform.position;
                float vx = ((rand() % 100) / 100.0f - 0.5f) * emitter.velocityRange.x;
                float vy = ((rand() % 100) / 100.0f) * emitter.velocityRange.y + 0.5f;
                float vz = ((rand() % 100) / 100.0f - 0.5f) * emitter.velocityRange.z;
                particle.velocity = glm::vec3(vx, vy, vz);
                particle.color = emitter.startColor;
                particle.lifetime = emitter.particleLifetime;
                particle.maxLifetime = emitter.particleLifetime;
                particle.active = true;
                break;
            }
        }
    }

    // Update active particles
    for (auto& particle : s_particlePool) {
        if (particle.active) {
            particle.lifetime -= deltaTime;
            if (particle.lifetime <= 0.0f) {
                particle.active = false;
                continue;
            }
            particle.position += particle.velocity * deltaTime;
        }
    }
}

} // namespace Engine
