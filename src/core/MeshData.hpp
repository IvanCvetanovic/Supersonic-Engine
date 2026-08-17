#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "core/Components.hpp"
#include "core/JobSystem.hpp"

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

    // Per-vertex tangent basis derived from UV derivatives, accumulated across
    // shared triangles and orthonormalised against the vertex normal.
    //
    // Needed by normal mapping: a normal map stores tangent-space directions,
    // so without this there is no basis to rotate them into world space.
    void computeTangents() {
        if (vertices.empty() || indices.size() < 3) return;

        std::vector<glm::vec3> tan(vertices.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> bitan(vertices.size(), glm::vec3(0.0f));

        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;

            const glm::vec3 e1 = vertices[i1].pos - vertices[i0].pos;
            const glm::vec3 e2 = vertices[i2].pos - vertices[i0].pos;
            const glm::vec2 d1 = vertices[i1].texCoord - vertices[i0].texCoord;
            const glm::vec2 d2 = vertices[i2].texCoord - vertices[i0].texCoord;

            // Degenerate UVs (a face with no texture area) give no usable
            // direction; skip rather than dividing by zero into NaN.
            const float det = d1.x * d2.y - d2.x * d1.y;
            if (std::abs(det) < 1e-12f) continue;
            const float r = 1.0f / det;

            const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
            const glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;

            tan[i0] += t; tan[i1] += t; tan[i2] += t;
            bitan[i0] += b; bitan[i1] += b; bitan[i2] += b;
        }

        // The accumulation loop above cannot be parallelised: several triangles
        // add into the same vertex slot. This one can - each iteration reads its
        // own accumulator and writes its own vertex, touching nothing shared.
        // A 512x512 terrain is a quarter of a million iterations of normalize
        // and cross.
        JobSystem::Dispatch(static_cast<uint32_t>(vertices.size()), 2048u,
                            [this, &tan, &bitan](JobSystem::JobArgs args) {
            const size_t i = args.jobIndex;
            const glm::vec3 n = vertices[i].normal;
            glm::vec3 t = tan[i];

            // A vertex touched only by degenerate-UV faces has no accumulated
            // direction; pick any axis perpendicular to the normal.
            if (glm::dot(t, t) < 1e-12f) {
                const glm::vec3 axis = std::abs(n.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f)
                                                            : glm::vec3(0.0f, 1.0f, 0.0f);
                t = glm::normalize(glm::cross(axis, n));
            } else {
                // Gram-Schmidt: remove the component along the normal so the
                // basis stays orthogonal after normal interpolation.
                t = glm::normalize(t - n * glm::dot(n, t));
            }

            const float handedness = (glm::dot(glm::cross(n, t), bitan[i]) < 0.0f) ? -1.0f : 1.0f;
            vertices[i].tangent = glm::vec4(t, handedness);
        });
    }

    bool empty() const { return vertices.empty() || indices.empty(); }
};

} // namespace Supersonic
