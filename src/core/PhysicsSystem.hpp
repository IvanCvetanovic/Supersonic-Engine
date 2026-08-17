#pragma once

#include <entt/entt.hpp>

namespace Supersonic {

class PhysicsSystem {
public:
    static void Update(entt::registry& registry, float deltaTime);
};

} // namespace Supersonic
