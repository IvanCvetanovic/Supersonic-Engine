#include "core/HeightfieldCache.hpp"

#include <cmath>

#include "core/Components.hpp"
#include "core/TerrainGenerator.hpp"

namespace Supersonic {

// The component's defaults and the mesh primitive's numbers are the same
// numbers, and this is where that is enforced rather than hoped for. A collider
// built for a 64 x 64 grid against a mesh generated at 32 x 32 is a surface
// half the size of the one you can see, and nothing at runtime would say so.
static_assert(HeightfieldColliderComponent{}.width == TerrainGenerator::kPrimitiveWidth,
              "the default collider width must be the width the mesh primitive generates");
static_assert(HeightfieldColliderComponent{}.depth == TerrainGenerator::kPrimitiveDepth,
              "the default collider depth must be the depth the mesh primitive generates");
static_assert(HeightfieldColliderComponent{}.heightScale == TerrainGenerator::kPrimitiveHeightScale,
              "the default collider height scale must be the mesh primitive's");

const Heightfield* HeightfieldCache::Get(const HeightfieldColliderComponent& collider) {
    Key key;
    key.width = collider.width;
    key.depth = collider.depth;
    key.heightScale = collider.heightScale;
    key.thickness = collider.thickness;

    // A NaN in the key would compare false against everything including itself,
    // so the map would grow by one entry per step and never find a hit. Caught
    // here rather than trusted to the inspector, which is not the only thing
    // that can write these.
    if (!std::isfinite(key.heightScale) || !std::isfinite(key.thickness)) return nullptr;

    const auto existing = m_fields.find(key);
    if (existing != m_fields.end()) {
        // An entry that failed to build is left INVALID rather than removed, so
        // a collider with a bad width costs one rejected build instead of one
        // per fixed step forever.
        return existing->second.valid() ? &existing->second : nullptr;
    }

    Heightfield& field = m_fields[key];
    if (!TerrainGenerator::GenerateHeightfield(key.width, key.depth, key.heightScale,
                                               key.thickness, field)) {
        return nullptr;
    }
    return &field;
}

void HeightfieldCache::Trim() {
    // Everything, rather than one entry at a time. These are cheap to rebuild
    // and there is no sensible recency order among four terrains, so the
    // simplest rule that cannot grow without bound is the right one.
    if (m_fields.size() >= kMaxFields) m_fields.clear();
}

HeightfieldCache& HeightfieldCache::For(entt::registry& registry) {
    if (auto* existing = registry.ctx().find<HeightfieldCache>()) return *existing;
    return registry.ctx().emplace<HeightfieldCache>();
}

} // namespace Supersonic
