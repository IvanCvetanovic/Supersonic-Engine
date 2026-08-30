#include "core/AudioSystem.hpp"
#include "core/Components.hpp"
#include "core/EcsUtils.hpp"

#include <algorithm>
#include <cmath>

namespace Supersonic {

namespace {

// Inverse-distance attenuation with a reference distance, which is better
// behaved near the listener than the old 1/(1 + 0.1*d^2).
float attenuationFor(float distance, float referenceDistance, float maxDistance) {
    if (distance <= referenceDistance) return 1.0f;
    if (distance >= maxDistance) return 0.0f;
    return referenceDistance / distance;
}

// Fired by EnTT just before the component is removed, so the handle is still
// readable here. Covers both registry.destroy() and registry.clear().
void onAudioSourceDestroyed(entt::registry& registry, entt::entity entity) {
    auto* slot = registry.ctx().find<AudioEngine*>();
    if (!slot || !*slot) return;

    auto& source = registry.get<AudioSourceComponent>(entity);
    if (source.voice != AudioEngine::kInvalidVoice) {
        (*slot)->Stop(source.voice);
        source.voice = AudioEngine::kInvalidVoice;
    }
}

} // namespace

void AudioSystem::Attach(entt::registry& registry, AudioEngine& audio) {
    registry.ctx().insert_or_assign<AudioEngine*>(&audio);
    registry.on_destroy<AudioSourceComponent>().connect<&onAudioSourceDestroyed>();
}

void AudioSystem::Detach(entt::registry& registry) {
    registry.on_destroy<AudioSourceComponent>().disconnect<&onAudioSourceDestroyed>();
    registry.ctx().erase<AudioEngine*>();
}

size_t AudioSystem::ReloadClip(entt::registry& registry, AudioEngine& audio,
                               const std::string& path) {
    // The engine's own record of what each voice is reading, not the
    // components'. A source whose soundFile was pointed somewhere else while a
    // LOOPING voice was running still has that voice playing the old clip -
    // Update only starts a voice, it never notices the file name changing
    // underneath one - so asking the components which of them use this path
    // would miss exactly the voice that is reading the memory about to be
    // freed.
    const std::vector<AudioEngine::VoiceId> stopped = audio.StopVoicesUsing(path);

    // Only now is nothing reading the samples.
    audio.UnloadClip(path);

    size_t interrupted = 0;
    for (auto entity : registry.view<AudioSourceComponent>()) {
        auto& source = registry.get<AudioSourceComponent>(entity);

        if (source.voice != AudioEngine::kInvalidVoice &&
            std::find(stopped.begin(), stopped.end(), source.voice) != stopped.end()) {
            // Handles are never reused, so a stale one is not dangerous - it is
            // WORSE than dangerous, it is quiet. A looping source holding one
            // never satisfies needsVoice, so it would simply never be heard
            // again and nothing would say why.
            source.voice = AudioEngine::kInvalidVoice;
            ++interrupted;
        }

        // A source that gave up on a file that would not load has to be allowed
        // to try again. Somebody fixing that file is precisely the event this
        // is reacting to, and without this the retry never happens.
        if (source.soundFile == path) source.failedToLoad = false;
    }

    return interrupted;
}

void AudioSystem::Update(entt::registry& registry, AudioEngine& audio, float deltaTime) {
    (void)deltaTime;

    // Listener: the active camera. It carries a TransformComponent now, which
    // is why this query used to match nothing at all.
    glm::vec3 listenerPos(0.0f);
    glm::vec3 listenerRight(1.0f, 0.0f, 0.0f);
    glm::vec3 listenerForward(0.0f, 0.0f, -1.0f);

    if (const auto camEntity = FirstEntityOf(registry.view<CameraComponent>());
        camEntity != entt::null) {
        const auto& camera = registry.get<CameraComponent>(camEntity);
        listenerPos = camera.position;
        listenerRight = camera.right;
        listenerForward = camera.front;
    }
    (void)listenerForward; // reserved for cone attenuation / doppler

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

        // Start, restart a finished non-looping voice, or replace one that is
        // reading the wrong file.
        //
        // That last case used to be missing, and it was invisible in the only
        // way that matters: this function STARTS a voice and then never looks
        // at soundFile again, so changing the file on a LOOPING source did
        // nothing at all - not an error, not a fallback, just the old sound
        // continuing - until somebody toggled Playing off and on. A non-looping
        // source hid it, because the voice ends and the next one reads the new
        // name.
        //
        // Asked of the ENGINE, which records what each voice is reading,
        // rather than of the component, which only records what it asks for.
        const bool needsVoice = source.voice == AudioEngine::kInvalidVoice ||
                                (!source.loop && !audio.IsVoicePlaying(source.voice)) ||
                                audio.PathOf(source.voice) != source.soundFile;
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

    // AFTER the component pass, and this is the engine's only per-frame audio
    // housekeeping, so it is the only place a fire-and-forget voice can be
    // freed at all. See AudioEngine::ReapFinishedVoices.
    //
    // After rather than before, so a voice started by the loop above cannot be
    // considered by the same Update that created it. It would survive either
    // way - Play submits its buffer and starts it before returning - but "the
    // frame that starts a sound never reaps it" is a property worth having
    // without depending on a backend detail.
    //
    // A component's own finished non-looping voice may be freed here, and that
    // changes nothing: the next Update finds PathOf empty, which is already how
    // it decides to restart one.
    audio.ReapFinishedVoices();
}

} // namespace Supersonic
