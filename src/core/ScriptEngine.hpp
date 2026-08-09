#pragma once

#include <entt/entt.hpp>

namespace Engine {

class ScriptEngine {
public:
    static void Update(entt::registry& registry, float deltaTime);
};

} // namespace Engine
