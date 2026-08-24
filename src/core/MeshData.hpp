#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "core/Components.hpp"
#include "core/JobSystem.hpp"

namespace Supersonic {

// What an imported file says its surface looks like.
//
// A glTF carries this and the engine threw all of it away: GltfLoader resolved
// the base colour, the metallic and roughness factors and the texture path, and
// MeshRegistry::Acquire copied vertices and indices out of the submesh and
// dropped the wrapper holding the rest. So a model imported from a file that
// describes a rough gold surface with a texture on it arrived as untextured
// white plastic, and the only way to get it back was to retype it by hand into
// the inspector - against a file that had said all of it already.
//
// Deliberately NOT the same struct as MaterialComponent. This is what a FILE
// said; the component is what the ENTITY is, which the user may have since
// edited. The import populates the component once, when the mesh is assigned,
// and never touches it again - so an authored value is never overwritten by a
// reimport, and a scene that has been saved carries its own answer.
struct MeshMaterial {
    // False for procedural primitives and for a file whose primitive names no
    // material at all, which is the difference between "the file said white"
    // and "the file said nothing".
    bool present{false};

    glm::vec4 baseColor{1.0f};
    float roughness{0.5f};
    float metallic{0.0f};

    // Resolved next to the source file, so the path-keyed texture cache can
    // open them. Empty when the file names no texture, or names one this
    // importer cannot reach - an image embedded in a .glb, which arrives as
    // bytes rather than as a file on disk.
    std::string albedoTexturePath;
    std::string normalTexturePath;

    // glTF splits these across two texture slots and this engine packs them
    // into one, because that is how an exporter writes them: occlusion in R,
    // roughness in G, metallic in B, usually all in the same image.
    std::string ormTexturePath;

    // Zero when the packed map's red channel is not occlusion - which is the
    // ordinary case for a glTF that has a metallic-roughness texture and no
    // occlusion texture, where the red channel is explicitly undefined.
    float occlusionStrength{1.0f};

    glm::vec3 emissiveColor{0.0f};
    float emissiveStrength{0.0f};

    // From glTF alphaMode. BLEND becomes a transparent material; MASK becomes
    // an alpha cutoff, which is what MASK actually asks for - a hard edge,
    // rather than a place in a sorted blend it would fight with itself in.
    bool transparent{false};
    float alphaCutoff{0.0f};
};

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

        // Not optional. The jobs capture `tan`, `bitan` and `this` by reference,
        // and all three are gone the moment this function returns - so without
        // the fence the workers write into freed memory, and every caller reads
        // `vertices` while they are still writing it. Nothing caught this
        // locally: the tests never start a pool, and no mesh in the sample scene
        // exceeds one group, so both paths fell back to the inline loop.
        JobSystem::Wait();
    }

    bool empty() const { return vertices.empty() || indices.empty(); }
};

} // namespace Supersonic
