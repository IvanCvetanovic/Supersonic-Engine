#include "core/MaterialSystem.hpp"
#include "core/Log.hpp"

#include <iostream>

#include "core/Components.hpp"

namespace Supersonic {

void MaterialSystem::Sync(entt::registry& registry, MaterialLibrary& library) {
    for (auto entity : registry.view<MaterialComponent>()) {
        auto& material = registry.get<MaterialComponent>(entity);
        if (material.materialPath.empty()) continue;

        const uint32_t id = library.Acquire(material.materialPath);
        if (id == MaterialLibrary::kInvalidMaterial) {
            if (!material.warnedMissingAsset) {
                SUPERSONIC_LOG_ERROR("MaterialSystem") << "'" << material.materialPath
                          << "' could not be loaded; the entity keeps its own values."
                          << std::endl;
                material.warnedMissingAsset = true;
            }
            continue;
        }
        material.warnedMissingAsset = false;

        const MaterialAsset* asset = library.Get(id);
        if (!asset) continue;

        // Copied every frame rather than resolved once, which is what makes an
        // edit to the shared asset appear on every entity using it immediately -
        // including while the inspector is dragging a slider.
        material.albedoColor = asset->albedoColor;
        material.roughness = asset->roughness;
        material.metallic = asset->metallic;
        material.ao = asset->ao;
        material.albedoTexturePath = asset->albedoTexturePath;
        material.normalTexturePath = asset->normalTexturePath;
    }
}

void MaterialSystem::MakeUnique(entt::registry& registry, entt::entity entity,
                                MaterialLibrary& library) {
    auto* material = registry.try_get<MaterialComponent>(entity);
    if (!material || material->materialPath.empty()) return;

    // Sync has already copied the asset's values in, so simply dropping the link
    // leaves the entity looking exactly as it did - which is the behaviour
    // "make unique" should have.
    if (const MaterialAsset* asset = library.Get(library.Acquire(material->materialPath))) {
        material->albedoColor = asset->albedoColor;
        material->roughness = asset->roughness;
        material->metallic = asset->metallic;
        material->ao = asset->ao;
        material->albedoTexturePath = asset->albedoTexturePath;
        material->normalTexturePath = asset->normalTexturePath;
    }
    material->materialPath.clear();
    material->warnedMissingAsset = false;
}

bool MaterialSystem::Assign(entt::registry& registry, entt::entity entity,
                            MaterialLibrary& library, const std::string& path) {
    if (!registry.valid(entity)) return false;

    const uint32_t id = library.Acquire(path);
    if (id == MaterialLibrary::kInvalidMaterial) return false;

    auto& material = registry.get_or_emplace<MaterialComponent>(entity);
    material.materialPath = path;
    material.warnedMissingAsset = false;

    if (const MaterialAsset* asset = library.Get(id)) {
        material.albedoColor = asset->albedoColor;
        material.roughness = asset->roughness;
        material.metallic = asset->metallic;
        material.ao = asset->ao;
        material.albedoTexturePath = asset->albedoTexturePath;
        material.normalTexturePath = asset->normalTexturePath;
    }
    return true;
}

void MaterialSystem::ApplyImportedMaterial(const MeshMaterial& imported, MaterialComponent& out) {
    if (!imported.present) return;

    out.albedoColor = imported.baseColor;
    out.roughness = imported.roughness;
    out.metallic = imported.metallic;
    out.albedoTexturePath = imported.albedoTexturePath;
    out.normalTexturePath = imported.normalTexturePath;
    out.emissiveColor = imported.emissiveColor;
    out.emissiveStrength = imported.emissiveStrength;
    out.transparent = imported.transparent;
    out.alphaCutoff = imported.alphaCutoff;

    // ao is deliberately left alone: glTF carries ambient occlusion as a
    // TEXTURE, not a factor, and there is no third texture binding to put it
    // in. Overwriting an authored ao with a default would lose information to
    // no purpose.
    out.materialPath.clear();
    out.warnedMissingAsset = false;
}

} // namespace Supersonic
