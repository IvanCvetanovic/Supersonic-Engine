#pragma once

#include <entt/entt.hpp>

namespace Engine {

class PhysicsSystem {
public:
    static void Update(entt::registry& registry, float deltaTime);
};

} // namespace Engine
