#pragma once

#include <entt/entt.hpp>

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
};

} // namespace Supersonic
