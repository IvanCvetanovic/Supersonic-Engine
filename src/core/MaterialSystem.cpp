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
        material.ormTexturePath = asset->ormTexturePath;
        material.occlusionStrength = asset->occlusionStrength;
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
        material->ormTexturePath = asset->ormTexturePath;
        material->occlusionStrength = asset->occlusionStrength;
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
        material.ormTexturePath = asset->ormTexturePath;
        material.occlusionStrength = asset->occlusionStrength;
    }
    return true;
}

uint32_t MaterialSystem::GatherUvTransforms(entt::registry& registry,
                                           std::vector<UvTransform>& out,
                                           uint32_t capacity,
                                           uint32_t* outDropped) {
    out.clear();
    uint32_t dropped = 0;
    const auto report = [&] { if (outDropped) *outDropped = dropped; };

    // Slot 0, unconditionally and first. Every other slot is an offset from it
    // and every draw that never asked for a transform points at it.
    out.push_back(UvTransform{});
    if (capacity == 0) { report(); return 1; }  // cannot happen; one compare to say so

    for (auto entity : registry.view<MaterialComponent>()) {
        auto& material = registry.get<MaterialComponent>(entity);

        if (!material.HasUvTransform()) {
            // Reset rather than leave alone. A material that scrolled last
            // frame and stopped this one would otherwise keep pointing at a
            // slot that now holds somebody else's transform.
            material.uvSlot = 0;
            continue;
        }

        if (static_cast<uint32_t>(out.size()) >= capacity) {
            // Untransformed rather than out of bounds. robustBufferAccess is
            // not enabled on this device, so a slot past the end is a device
            // loss and not a zeroed read.
            //
            // COUNTED, because "untransformed" is not a small wrongness here: a
            // material that asked for one cell of an atlas and is handed the
            // identity draws the entire atlas. The caller turns this into a log
            // line - see the header for why it is worth one.
            material.uvSlot = 0;
            ++dropped;
            continue;
        }

        material.uvSlot = static_cast<int32_t>(out.size());
        out.push_back(MakeUvTransform(material.uvScale, material.uvRotation,
                                      material.uvOffset));
    }

    report();
    return static_cast<uint32_t>(out.size());
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

    out.ormTexturePath = imported.ormTexturePath;
    out.occlusionStrength = imported.occlusionStrength;

    // The ao FACTOR is still deliberately left alone, and the reason has
    // changed shape rather than gone away. glTF carries ambient occlusion as a
    // texture and never as a factor, so the file has nothing to say about this
    // number - and it is now a master control over the map that was just
    // imported, so overwriting an authored value would darken a surface the
    // author had already balanced.
    out.materialPath.clear();
    out.warnedMissingAsset = false;
}

} // namespace Supersonic
