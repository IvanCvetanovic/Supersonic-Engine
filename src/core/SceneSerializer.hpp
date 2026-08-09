#pragma once

#include <entt/entt.hpp>
#include <string>

namespace Engine {

class SceneSerializer {
public:
    static bool Serialize(entt::registry& registry, const std::string& filepath);
    static bool Deserialize(entt::registry& registry, const std::string& filepath);
};

} // namespace Engine
