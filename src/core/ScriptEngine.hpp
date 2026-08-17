#pragma once

#include <entt/entt.hpp>

namespace Supersonic {

class ScriptEngine {
public:
    // Registers the built-in scripts with ScriptRegistry. Call once at startup.
    static void RegisterBuiltInScripts();

    static void Update(entt::registry& registry, float deltaTime);
};

} // namespace Supersonic
