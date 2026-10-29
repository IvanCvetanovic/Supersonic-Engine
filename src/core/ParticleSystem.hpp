#pragma once

#include <entt/entt.hpp>

#include "core/Components.hpp"

namespace Supersonic {

class ParticleSystem {
public:
    // Stateless: every particle lives in its emitter's component, so the system
    // holds no process-global state and two emitters cannot starve each other.
    static void Update(entt::registry& registry, float deltaTime);

    // The most particles one emitter keeps. maxParticles is a uint32 read out of a
    // scene file, and the particle vector was sized from it as it stood: a scene
    // saying four billion became a 200 GB allocation on the next tick. A million
    // is about 55 MB per emitter and a hundred times what any effect here uses.
    static constexpr size_t kMaxParticlesPerEmitter = size_t{1} << 20;
};

} // namespace Supersonic
