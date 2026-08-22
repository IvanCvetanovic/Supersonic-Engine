#pragma once

#include <entt/entt.hpp>

#include "core/MaterialLibrary.hpp"
#include "core/MeshData.hpp"

namespace Supersonic {

// Resolves shared material assets onto the components the renderer reads.
//
// Nothing downstream knows about assets: RenderSystem still reads
// MaterialComponent's own fields, and this copies the shared asset's values into
// them each frame. That is what makes one edit to an asset show up on every
// entity using it without the render path caring how the numbers got there.
class MaterialSystem {
public:
    static void Sync(entt::registry& registry, MaterialLibrary& library);

    // Copies the asset's values into the component and clears the link, so the
    // entity keeps the look it had but stops following the asset.
    static void MakeUnique(entt::registry& registry, entt::entity entity, MaterialLibrary& library);

    // Points an entity at an asset, loading it if this is its first use.
    // Returns false when the asset cannot be read, leaving the entity alone.
    static bool Assign(entt::registry& registry, entt::entity entity, MaterialLibrary& library,
                       const std::string& path);

    // Copies what an imported file said its surface is onto a component.
    //
    // A free function rather than inline at the one call site because the call
    // site is inside RenderSystem::SyncResources, which needs a Vulkan device
    // to reach - and the suites deliberately touch no Vulkan entry point, so
    // the rule about what an import does to an authored material could not
    // otherwise be tested.
    //
    // Clears materialPath: the file describes THIS entity's surface, not a
    // shared .material asset, so a linked entity is detached rather than having
    // its asset silently rewritten for everything else using it.
    //
    // Does nothing when the file named no material, which is the difference
    // between "the file said white" and "the file said nothing" - a procedural
    // primitive must not blank a material somebody authored.
    static void ApplyImportedMaterial(const MeshMaterial& imported, MaterialComponent& out);
};

} // namespace Supersonic
