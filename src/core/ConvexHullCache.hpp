#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include <entt/entt.hpp>

#include "core/ConvexDecomposition.hpp"
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
    // A DECOMPOSITION, not a hull. A shape that is already convex comes back as
    // one piece equal to its hull, so the common case is unchanged in
    // everything but the type.
    const ConvexDecomposition* Get(const std::string& primitive, const std::string& path);

    // Resolves the collider against the entity's own mesh when it names no
    // source of its own, which is what an author wants nine times in ten: the
    // collider is the shape you can see.
    const ConvexDecomposition* Get(entt::registry& registry, entt::entity entity,
                                   const ConvexHullColliderComponent& collider);

    // Drops every hull built from one file, so the next step rebuilds it.
    // Returns how many went. A path that built nothing is still worth dropping:
    // failures are cached, so a model that could not be read stays a cached
    // failure until somebody fixes it - and fixing it is exactly the event this
    // reacts to.
    //
    // Safe from the asset-watcher callback for the reason Trim is safe at the
    // top of a step: both invalidate every pointer Get has handed out, and both
    // run between steps rather than part way through a gather. The poll is at
    // SupersonicApp.cpp:906 and the fixed step begins at :963.
    size_t Invalidate(const std::string& path);

    // Drops what the last step did not use, once the map has grown past the
    // cap.
    //
    // It used to drop EVERYTHING past the cap, which is a backstop until the
    // day a scene references the cap's worth of hulls - and then it is a cache
    // that empties itself at the top of every step and rebuilds every hull,
    // from disk, through quickhull, inside the step, forever. A build that
    // failed counts too: a scene with thirty-two broken references thrashed
    // the same way. Each entry now carries the step it was last asked for,
    // and a trim past the cap keeps the ones the previous step touched. A
    // rebuilt hull is the same hull, so nothing the solver sees changes;
    // what changes is that a scene which needs more than the cap keeps them.
    //
    // Called at the TOP of a step, before any pointer has been handed out, for
    // the reason HeightfieldCache::Trim is: dropping invalidates every pointer
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
    };

    // What a lookup is made with: the caller's strings, borrowed. A Key copies
    // both, and the step looked a hull up once per hull collider per step and
    // once per query - two allocations each for any path longer than the
    // small-string limit, which an asset path is.
    struct KeyView {
        std::string_view primitive;
        std::string_view path;
    };

    // One ordering for keys and views alike (std::less<> finds it for either
    // side), so a view can search the map without becoming a Key first.
    struct KeyLess {
        using is_transparent = void;
        static KeyView view(const Key& key) { return KeyView{key.primitive, key.path}; }
        static KeyView view(const KeyView& key) { return key; }
        template <typename L, typename R>
        bool operator()(const L& lhs, const R& rhs) const {
            const KeyView l = view(lhs);
            const KeyView r = view(rhs);
            if (l.primitive != r.primitive) return l.primitive < r.primitive;
            return l.path < r.path;
        }
    };

    struct Entry {
        ConvexDecomposition hull;
        // The Trim generation this entry was last asked for in.
        uint64_t lastUsed{0};
    };

    // By value, so a map node holding a hull does not move and a caller's
    // pointer survives every later insertion. A key whose build failed holds an
    // INVALID hull, which is the cached failure with nothing extra to represent
    // it.
    std::map<Key, Entry, KeyLess> m_hulls;

    // Advanced by every Trim; what Entry::lastUsed is measured in.
    uint64_t m_generation{0};
};

} // namespace Supersonic
