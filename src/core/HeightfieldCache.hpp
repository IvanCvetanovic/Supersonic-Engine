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
    // The returned pointer stays valid for the life of the cache: std::map
    // leaves its values where it put them, however many terrains are added
    // afterwards.
    const Heightfield* Get(const HeightfieldColliderComponent& collider);

    // The registry's cache, created on first use.
    static HeightfieldCache& For(entt::registry& registry);

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
