#pragma once

#include <map>
#include <string>

#include <entt/entt.hpp>

#include "core/ConvexHull.hpp"

namespace Supersonic {

struct ConvexHullColliderComponent;

// Hulls built from mesh assets, kept per registry and keyed by the asset.
//
// The same shape as HeightfieldCache and for the same reasons: building a hull
// is a quickhull over every vertex of a model and the fixed step runs several
// times a frame, so it happens once per distinct asset rather than once per
// step. Keyed by the SOURCE rather than by entity, so a scene of fifty identical
// rocks holds one hull.
//
// It exists at all because the CPU-side vertices are gone by the time physics
// wants them: MeshRegistry uploads a mesh to the GPU and keeps only the buffers
// and the bounds. It is also renderer-side, and physics has no business
// depending on it - so the hull loads the asset itself, through the same
// ModelLoader and GltfLoader entry points, with the primitive sizes pinned in
// ModelLoader so the two cannot drift.
class ConvexHullCache {
public:
    // Null when the asset cannot be read, or describes nothing a hull can be
    // made of - a flat sheet, a line, fewer than four distinct points. The
    // failure is CACHED, so a broken reference costs one load rather than one
    // per step.
    //
    // The returned pointer stays valid until the next Trim: std::map leaves its
    // values where it put them, so every later Get in the same step leaves
    // earlier answers exactly where they were.
    const ConvexHull* Get(const std::string& primitive, const std::string& path);

    // Resolves the collider against the entity's own mesh when it names no
    // source of its own, which is what an author wants nine times in ten: the
    // collider is the shape you can see.
    const ConvexHull* Get(entt::registry& registry, entt::entity entity,
                          const ConvexHullColliderComponent& collider);

    // Drops everything once the map has grown past the cap.
    //
    // Called at the TOP of a step, before any pointer has been handed out, for
    // the reason HeightfieldCache::Trim is: clearing invalidates every pointer
    // the map has returned, and doing it part way through a gather would leave
    // the colliders gathered so far pointing at freed hulls.
    void Trim();

    static ConvexHullCache& For(entt::registry& registry);

    // Far above any real scene. A level built from distinct hull assets has a
    // handful, not thirty-two - and unlike a terrain descriptor, nothing here
    // is edited by a slider, so this is a backstop rather than a working limit.
    static constexpr size_t kMaxHulls = 32;

    size_t size() const { return m_hulls.size(); }
    void Clear() { m_hulls.clear(); }

private:
    struct Key {
        std::string primitive;
        std::string path;

        bool operator<(const Key& other) const {
            if (primitive != other.primitive) return primitive < other.primitive;
            return path < other.path;
        }
    };

    // By value, so a map node holding a hull does not move and a caller's
    // pointer survives every later insertion. A key whose build failed holds an
    // INVALID hull, which is the cached failure with nothing extra to represent
    // it.
    std::map<Key, ConvexHull> m_hulls;
};

} // namespace Supersonic
