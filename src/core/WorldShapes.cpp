#include "core/WorldShapes.hpp"

#include <algorithm>
#include <cmath>

namespace Supersonic {

namespace {

// Two unit vectors spanning the plane whose normal this is.
//
// The seed axis is chosen to be the one LEAST aligned with the normal, which is
// what stops the cross product collapsing: seeding with a fixed up vector gives
// a zero-length tangent for a circle whose normal IS up, and that is the ground
// plane - the single most common case there is.
void planeBasis(const glm::vec3& normal, glm::vec3& outU, glm::vec3& outV) {
    const glm::vec3 n = glm::length(normal) > 1e-6f ? glm::normalize(normal)
                                                    : glm::vec3(0.0f, 1.0f, 0.0f);

    const glm::vec3 seed = (std::fabs(n.x) < std::fabs(n.y) && std::fabs(n.x) < std::fabs(n.z))
                               ? glm::vec3(1.0f, 0.0f, 0.0f)
                               : (std::fabs(n.y) < std::fabs(n.z) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                                  : glm::vec3(0.0f, 0.0f, 1.0f));

    outU = glm::normalize(glm::cross(n, seed));
    outV = glm::cross(n, outU);
}

} // namespace

bool WorldShapes::Reserve(size_t additional) {
    if (m_vertices.size() + additional > kMaxVertices) {
        m_dropped += additional;
        return false;
    }
    return true;
}

void WorldShapes::AddLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color) {
    if (!Reserve(2)) return;
    m_vertices.push_back(Vertex{from, color});
    m_vertices.push_back(Vertex{to, color});
}

void WorldShapes::AddCircle(const glm::vec3& center, float radius, const glm::vec4& color,
                            int segments, const glm::vec3& normal) {
    // Three is the fewest that is a ring rather than a line drawn twice.
    const int count = std::max(segments, 3);

    if (!Reserve(static_cast<size_t>(count) * 2)) return;

    glm::vec3 u(0.0f);
    glm::vec3 v(0.0f);
    planeBasis(normal, u, v);

    // The first point is computed once and reused as the LAST point, so the
    // ring closes exactly rather than to within a float of exactly. Computing
    // it twice from the angle leaves a hairline gap that shows at large radii.
    const glm::vec3 first = center + u * radius;
    glm::vec3 previous = first;

    for (int i = 1; i < count; ++i) {
        const float angle = 6.283185307179586f * static_cast<float>(i) / static_cast<float>(count);
        const glm::vec3 point =
            center + (u * std::cos(angle) + v * std::sin(angle)) * radius;

        m_vertices.push_back(Vertex{previous, color});
        m_vertices.push_back(Vertex{point, color});
        previous = point;
    }

    m_vertices.push_back(Vertex{previous, color});
    m_vertices.push_back(Vertex{first, color});
}

void WorldShapes::AddGroundRect(const glm::vec3& min, const glm::vec3& max,
                                const glm::vec4& color) {
    if (!Reserve(8)) return;

    // The height of the FIRST corner for all four, so a rectangle given two
    // corners at different heights is still flat. A caller that wants a
    // quadrilateral in space wants four lines, not this.
    const float y = min.y;
    const glm::vec3 a(min.x, y, min.z);
    const glm::vec3 b(max.x, y, min.z);
    const glm::vec3 c(max.x, y, max.z);
    const glm::vec3 d(min.x, y, max.z);

    AddLine(a, b, color);
    AddLine(b, c, color);
    AddLine(c, d, color);
    AddLine(d, a, color);
}

void WorldShapes::AddBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color) {
    if (!Reserve(24)) return;

    const glm::vec3 corners[8] = {
        {min.x, min.y, min.z}, {max.x, min.y, min.z}, {max.x, min.y, max.z}, {min.x, min.y, max.z},
        {min.x, max.y, min.z}, {max.x, max.y, min.z}, {max.x, max.y, max.z}, {min.x, max.y, max.z},
    };

    // Four along the bottom, four along the top, four uprights.
    for (int i = 0; i < 4; ++i) {
        AddLine(corners[i], corners[(i + 1) % 4], color);
        AddLine(corners[i + 4], corners[((i + 1) % 4) + 4], color);
        AddLine(corners[i], corners[i + 4], color);
    }
}

} // namespace Supersonic
