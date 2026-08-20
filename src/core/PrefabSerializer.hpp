#pragma once

#include <cstddef>
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

    // Forgets every parsed prefab, so the next spawn reads from disk again.
    //
    // Called when a prefab file changes on disk, and by tests, which would
    // otherwise write a file, instantiate it, rewrite it and get the first
    // version back. Nothing calls it from the asset watcher yet - prefabs are
    // not watched - and that is a gap rather than a decision.
    static void ClearCache();

    // How many prefabs are parsed and held. For tests and for anyone wondering
    // where the memory went.
    static std::size_t CachedPrefabCount();
};

} // namespace Supersonic
