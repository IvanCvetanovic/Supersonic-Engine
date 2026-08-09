#include "core/AudioSystem.hpp"
#include "core/Components.hpp"
#include <iostream>

namespace Engine {

void AudioSystem::Update(entt::registry& registry, float deltaTime) {
    glm::vec3 listenerPos{0.0f};

    auto cameraView = registry.view<TransformComponent, CameraComponent>();
    for (auto camEnt : cameraView) {
        listenerPos = cameraView.get<TransformComponent>(camEnt).position;
        break;
    }

    auto sourceView = registry.view<TransformComponent, AudioSourceComponent>();
    for (auto sourceEnt : sourceView) {
        const auto& transform = sourceView.get<TransformComponent>(sourceEnt);
        auto& audioSource = sourceView.get<AudioSourceComponent>(sourceEnt);

        if (!audioSource.isPlaying) continue;

        float distance = glm::distance(transform.position, listenerPos);
        float attenuatedVolume = audioSource.volume / (1.0f + 0.1f * distance * distance);
        (void)attenuatedVolume; // Spatial volume calculated for audio output
    }
}

} // namespace Engine
