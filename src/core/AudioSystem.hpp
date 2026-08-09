#pragma once

#include <entt/entt.hpp>

namespace Engine {

class AudioSystem {
public:
    static void Update(entt::registry& registry, float deltaTime);
};

} // namespace Engine
