#pragma once

#include <string>

#include <entt/entt.hpp>

#include "core/SceneSerializer.hpp"

namespace Supersonic {

class PrefabSerializer {
public:
    // Writes the entity's actual component data, not just a list of which
    // components it happened to have.
    static SerializationResult SavePrefab(entt::registry& registry, entt::entity entity, const std::string& filepath);

    // Reads the file back. Returns entt::null when the prefab is missing or
    // malformed, so a broken asset is distinguishable from a working one.
    static entt::entity InstantiatePrefab(entt::registry& registry, const std::string& filepath,
                                          SerializationResult* outResult = nullptr);
};

} // namespace Supersonic
