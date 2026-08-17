#pragma once

#include <entt/entt.hpp>

#include "core/Components.hpp"

namespace Supersonic {

class ParticleSystem {
public:
    // Stateless: every particle lives in its emitter's component, so the system
    // holds no process-global state and two emitters cannot starve each other.
    static void Update(entt::registry& registry, float deltaTime);
};

} // namespace Supersonic
