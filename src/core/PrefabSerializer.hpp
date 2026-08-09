#pragma once

#include <entt/entt.hpp>
#include <string>

namespace Engine {

class PrefabSerializer {
public:
    static bool SavePrefab(entt::registry& registry, entt::entity entity, const std::string& filepath);
    static entt::entity InstantiatePrefab(entt::registry& registry, const std::string& filepath);
};

} // namespace Engine
