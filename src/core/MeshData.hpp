#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "core/Components.hpp"

namespace Supersonic {

// CPU-side mesh, shared by every generator and loader.
//
// Indices are uint32_t. They used to be uint16_t with no range check, so an
// ordinary 300x300 terrain or sphere (90,000 vertices) silently wrapped index
// 65536 back to 0 and stitched the tail of the mesh onto its own head.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Local-space AABB, used for ray picking so selection matches the geometry
    // that is actually drawn rather than a hardcoded unit cube.
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

    void clear() {
        vertices.clear();
        indices.clear();
        boundsMin = glm::vec3(0.0f);
        boundsMax = glm::vec3(0.0f);
    }

    void computeBounds() {
        if (vertices.empty()) {
            boundsMin = boundsMax = glm::vec3(0.0f);
            return;
        }
        constexpr float big = std::numeric_limits<float>::max();
        boundsMin = glm::vec3(big);
        boundsMax = glm::vec3(-big);
        for (const auto& v : vertices) {
            boundsMin = glm::min(boundsMin, v.pos);
            boundsMax = glm::max(boundsMax, v.pos);
        }
    }

    bool empty() const { return vertices.empty() || indices.empty(); }
};

} // namespace Supersonic
