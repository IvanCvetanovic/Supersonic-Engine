#pragma once

#include <entt/entt.hpp>

#include "core/AudioEngine.hpp"

namespace Engine {

class AudioSystem {
public:
    // Drives real voices on the supplied device. The computed spatial volume
    // used to be discarded with (void)attenuatedVolume, so nothing could ever
    // be heard.
    static void Update(entt::registry& registry, AudioEngine& audio, float deltaTime);
};

} // namespace Engine
