#include "core/AssetRepointer.hpp"

#include <iostream>

#include "core/Components.hpp"
#include "core/EnvironmentSettings.hpp"
#include "core/Log.hpp"
#include "core/MaterialLibrary.hpp"

namespace Supersonic {

namespace {

// One field, one move. Returns whether it changed, so the caller can count
// rewrites rather than fields looked at.
bool repoint(std::string& field, const AssetDatabase::Move& move) {
    if (field.empty() || field != move.from) return false;
    field = move.to;
    return true;
}

} // namespace

RepointResult RepointAssets(entt::registry& registry, MaterialLibrary* materials,
                            const std::vector<AssetDatabase::Move>& moves) {
    RepointResult result;

    for (const auto& move : moves) {
        // A move with no old path cannot be matched against anything. It is
        // recorded rather than dropped so the import's count and its list agree,
        // and skipped here rather than there.
        if (move.from.empty() || move.to.empty() || move.from == move.to) continue;

        for (auto entity : registry.view<MeshComponent>()) {
            if (repoint(registry.get<MeshComponent>(entity).filePath, move)) {
                ++result.componentFields;
            }
        }

        for (auto entity : registry.view<MaterialComponent>()) {
            auto& material = registry.get<MaterialComponent>(entity);
            // All five, including materialPath: a .material is an asset with an
            // identity of its own, so renaming one has to follow just as a
            // texture does. And the overlay, which a scene saves with a Guid
            // like the other three maps.
            if (repoint(material.albedoTexturePath, move)) ++result.componentFields;
            if (repoint(material.normalTexturePath, move)) ++result.componentFields;
            if (repoint(material.ormTexturePath, move)) ++result.componentFields;
            if (repoint(material.overlayTexturePath, move)) ++result.componentFields;
            if (repoint(material.materialPath, move)) ++result.componentFields;
        }

        for (auto entity : registry.view<ConvexHullColliderComponent>()) {
            if (repoint(registry.get<ConvexHullColliderComponent>(entity).sourcePath, move)) {
                ++result.componentFields;
            }
        }

        for (auto entity : registry.view<AudioSourceComponent>()) {
            if (repoint(registry.get<AudioSourceComponent>(entity).soundFile, move)) {
                ++result.componentFields;
            }
        }

        // Not a component, so it is missed by any sweep over the registry's
        // views - and it is the one asset path whose staleness is visible over
        // the entire scene rather than on one object.
        if (auto* environment = registry.ctx().find<EnvironmentSettings>()) {
            if (repoint(environment->hdriPath, move)) ++result.environmentFields;
        }

        // The second population of the same strings. MaterialSystem::Sync copies
        // these onto every component that names the asset every frame, so
        // leaving them stale would put the old paths straight back.
        if (materials) result.materialFields += materials->Repoint(move.from, move.to);
    }

    if (result.Total() > 0) {
        SUPERSONIC_LOG_INFO("AssetRepointer")
            << "Re-pointed " << result.Total() << " reference(s) in the open scene ("
            << result.componentFields << " on components, " << result.materialFields
            << " in materials, " << result.environmentFields << " in the environment)."
            << std::endl;
    }

    return result;
}

} // namespace Supersonic
