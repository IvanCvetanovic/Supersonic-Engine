#pragma once

#include <entt/entt.hpp>

#include <string>

#include "core/AudioEngine.hpp"

namespace Supersonic {

class AudioSystem {
public:
    // Hooks entity destruction so a voice cannot outlive the component holding
    // its handle. Sources default to looping, so without this, deleting an
    // entity (or loading a scene, which clears the whole registry) left an
    // infinitely-looping voice playing until the process exited.
    static void Attach(entt::registry& registry, AudioEngine& audio);
    static void Detach(entt::registry& registry);

    // Drives real voices on the supplied device. The computed spatial volume
    // used to be discarded with (void)attenuatedVolume, so nothing could ever
    // be heard.
    static void Update(entt::registry& registry, AudioEngine& audio, float deltaTime);

    // Re-reads a .wav that changed on disk, and restarts whatever was playing
    // it. Returns how many sources were interrupted.
    //
    // The order inside is the whole thing and it is not negotiable: stop the
    // voices, THEN drop the clip, THEN clear the handles the components are
    // holding. Dropping first frees a sample buffer an audio thread is reading;
    // clearing the handles first loses the ids needed to stop those voices, and
    // they would play the old sound until the scene closed.
    static size_t ReloadClip(entt::registry& registry, AudioEngine& audio,
                             const std::string& path);
};

} // namespace Supersonic
