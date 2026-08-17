#include "core/AudioSystem.hpp"
#include "core/Components.hpp"

#include <algorithm>
#include <cmath>

namespace Engine {

namespace {

// Inverse-distance attenuation with a reference distance, which is better
// behaved near the listener than the old 1/(1 + 0.1*d^2).
float attenuationFor(float distance, float referenceDistance, float maxDistance) {
    if (distance <= referenceDistance) return 1.0f;
    if (distance >= maxDistance) return 0.0f;
    return referenceDistance / distance;
}

} // namespace

void AudioSystem::Update(entt::registry& registry, AudioEngine& audio, float deltaTime) {
    (void)deltaTime;

    // Listener: the active camera. It carries a TransformComponent now, which
    // is why this query used to match nothing at all.
    glm::vec3 listenerPos(0.0f);
    glm::vec3 listenerRight(1.0f, 0.0f, 0.0f);
    glm::vec3 listenerForward(0.0f, 0.0f, -1.0f);

    for (auto camEntity : registry.view<CameraComponent>()) {
        const auto& camera = registry.get<CameraComponent>(camEntity);
        listenerPos = camera.position;
        listenerRight = camera.right;
        listenerForward = camera.front;
        break;
    }

    for (auto entity : registry.view<TransformComponent, AudioSourceComponent>()) {
        const auto& transform = registry.get<TransformComponent>(entity);
        auto& source = registry.get<AudioSourceComponent>(entity);

        if (!source.isPlaying) {
            if (source.voice != AudioEngine::kInvalidVoice) {
                audio.Stop(source.voice);
                source.voice = AudioEngine::kInvalidVoice;
            }
            continue;
        }

        // Start (or restart a finished non-looping voice).
        const bool needsVoice = source.voice == AudioEngine::kInvalidVoice ||
                                (!source.loop && !audio.IsVoicePlaying(source.voice));
        if (needsVoice) {
            if (source.voice != AudioEngine::kInvalidVoice) {
                audio.Stop(source.voice);
                source.voice = AudioEngine::kInvalidVoice;
            }
            if (!source.failedToLoad) {
                source.voice = audio.Play(source.soundFile, source.loop, 0.0f, source.pitch);
                if (source.voice == AudioEngine::kInvalidVoice) {
                    // Remember the failure so a missing file is not retried
                    // every single frame.
                    source.failedToLoad = true;
                    continue;
                }
            } else {
                continue;
            }
        }

        const glm::vec3 toSource = transform.position - listenerPos;
        const float distance = glm::length(toSource);

        const float attenuation = attenuationFor(distance, source.referenceDistance, source.maxDistance);
        const float volume = std::clamp(source.volume * attenuation, 0.0f, 1.0f);

        // Pan from the component of the source direction along the listener's
        // right vector, so turning the camera moves the sound between speakers.
        float pan = 0.0f;
        if (distance > 0.0001f) {
            pan = glm::dot(toSource / distance, listenerRight);
        }

        audio.SetVoiceParameters(source.voice, volume, source.pitch, pan);
    }
}

} // namespace Engine
