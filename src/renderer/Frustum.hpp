#pragma once

#include <array>
#include <cmath>

#include <glm/glm.hpp>

namespace Supersonic {

// View frustum as six planes, for culling.
//
// Nothing culled before this: every entity was recorded twice per frame (shadow
// pass and scene pass) regardless of whether it was on screen.
class Frustum {
public:
    // Gribb/Hartmann plane extraction straight from the combined matrix. Works
    // for the camera's projection*view and equally for the light's orthographic
    // matrix, so the shadow pass can use the same code.
    static Frustum FromMatrix(const glm::mat4& m) {
        Frustum f;
        // Rows of the matrix; glm is column-major, so m[col][row].
        const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
        const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
        const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
        const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);

        f.m_planes[0] = row3 + row0; // left
        f.m_planes[1] = row3 - row0; // right
        f.m_planes[2] = row3 + row1; // bottom
        f.m_planes[3] = row3 - row1; // top
        // Zero-to-one depth range, so the near plane is row2 alone rather than
        // row3 + row2 as it would be under the OpenGL convention.
        f.m_planes[4] = row2;        // near
        f.m_planes[5] = row3 - row2; // far

        for (auto& plane : f.m_planes) {
            const float length = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
            if (length > 1e-8f) plane /= length;
        }
        return f;
    }

    // Conservative test against a world-space AABB: rejects only boxes fully
    // outside a plane, so a visible object is never wrongly culled.
    bool IntersectsAABB(const glm::vec3& min, const glm::vec3& max) const {
        for (const auto& plane : m_planes) {
            // The box corner furthest along the plane normal. If even that is
            // behind the plane, every corner is.
            const glm::vec3 positive(
                plane.x >= 0.0f ? max.x : min.x,
                plane.y >= 0.0f ? max.y : min.y,
                plane.z >= 0.0f ? max.z : min.z);

            if (plane.x * positive.x + plane.y * positive.y + plane.z * positive.z + plane.w < 0.0f) {
                return false;
            }
        }
        return true;
    }

    // World-space AABB of a local box under a transform. Transforming the
    // centre and the extent separately keeps this exact for rotation and scale
    // without testing all eight corners.
    static void TransformAABB(const glm::mat4& transform,
                              const glm::vec3& localMin, const glm::vec3& localMax,
                              glm::vec3& outMin, glm::vec3& outMax) {
        const glm::vec3 centre = (localMin + localMax) * 0.5f;
        const glm::vec3 extent = (localMax - localMin) * 0.5f;

        const glm::vec3 worldCentre = glm::vec3(transform * glm::vec4(centre, 1.0f));

        const glm::mat3 basis(transform);
        const glm::vec3 worldExtent(
            std::abs(basis[0].x) * extent.x + std::abs(basis[1].x) * extent.y + std::abs(basis[2].x) * extent.z,
            std::abs(basis[0].y) * extent.x + std::abs(basis[1].y) * extent.y + std::abs(basis[2].y) * extent.z,
            std::abs(basis[0].z) * extent.x + std::abs(basis[1].z) * extent.y + std::abs(basis[2].z) * extent.z);

        outMin = worldCentre - worldExtent;
        outMax = worldCentre + worldExtent;
    }

    const std::array<glm::vec4, 6>& Planes() const { return m_planes; }

private:
    std::array<glm::vec4, 6> m_planes{};
};

} // namespace Supersonic
