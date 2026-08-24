#pragma once

#include <map>

#include <entt/entt.hpp>

#include "core/Heightfield.hpp"

namespace Supersonic {

struct HeightfieldColliderComponent;

// Built heightfields, kept per registry and keyed by the numbers that describe
// one.
//
// The standard terrain is 4096 sines and the fixed step runs several times a
// frame, so rebuilding the grid per step would be by a wide margin the most
// expensive thing in the solver - for a surface that never changes. Keyed by
// the DESCRIPTOR rather than by entity, so a scene laid out from fifty
// identical terrain tiles holds one grid rather than fifty.
//
// It lives in the registry's context next to PhysicsSettings, and for the same
// reason: the systems here are stateless free functions over a registry, and a
// file-static cache would outlive the scene that filled it and hand the next
// one somebody else's hills.
class HeightfieldCache {
public:
    // Null when the descriptor cannot make a grid - a width below two, a
    // thickness that is not a number. The failure is CACHED against the same
    // key, so a broken collider costs one rejected build rather than one per
    // step forever.
    //
    // The returned pointer stays valid until the next Trim: std::map leaves
    // its values where it put them, so every later Get in the same step - and
    // there is one per terrain entity - leaves earlier answers exactly where
    // they were.
    const Heightfield* Get(const HeightfieldColliderComponent& collider);

    // Drops everything once the map has grown past the cap.
    //
    // Called at the TOP of a step, before any pointer has been handed out, and
    // that is the whole of why it is a separate method rather than a check
    // inside Get. Clearing invalidates every pointer the map has ever returned;
    // doing it part way through a gather would leave the terrains collected so
    // far pointing at freed grids, which is a crash that needs more than
    // thirty-two distinct terrains in one scene to reproduce and would
    // therefore never be seen until it was.
    void Trim();

    // The registry's cache, created on first use.
    static HeightfieldCache& For(entt::registry& registry);

    // How many distinct descriptors are kept before Trim drops the lot.
    //
    // Not a memory budget - it is the inspector. Dragging the Height Scale
    // slider writes a new float every frame it moves, and each distinct value
    // is a distinct descriptor, so a few seconds of dragging is hundreds of
    // permanent 16 KB grids and the 4096 sines each of them costs. The cap is
    // far above any real scene: a level built from distinct terrain tiles has a
    // handful, not thirty-two.
    static constexpr size_t kMaxFields = 32;

private:
    struct Key {
        uint32_t width{0};
        uint32_t depth{0};
        float heightScale{0.0f};
        float thickness{0.0f};

        bool operator<(const Key& other) const {
            if (width != other.width) return width < other.width;
            if (depth != other.depth) return depth < other.depth;
            if (heightScale != other.heightScale) return heightScale < other.heightScale;
            return thickness < other.thickness;
        }
    };

    // By value rather than behind a pointer. A map node holding a Heightfield
    // does not move, so a caller's pointer survives every later insertion, and
    // a key whose build failed simply holds an INVALID field - which is the
    // cached failure, with nothing extra to represent it.
    std::map<Key, Heightfield> m_fields;
};

} // namespace Supersonic
