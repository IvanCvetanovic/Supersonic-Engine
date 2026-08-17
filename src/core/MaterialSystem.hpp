#pragma once

#include <entt/entt.hpp>

#include "core/MaterialLibrary.hpp"

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
};

} // namespace Supersonic
