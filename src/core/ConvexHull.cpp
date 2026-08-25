#include "core/ConvexHull.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace Supersonic {

namespace {

constexpr float kPlaneEpsilon = 1.0e-4f;
constexpr float kMergeNormalEpsilon = 1.0e-3f;
constexpr float kMergeOffsetEpsilon = 1.0e-4f;

// A triangle during the build. The finished hull merges these into polygons.
struct Triangle {
    uint32_t a{0};
    uint32_t b{0};
    uint32_t c{0};
    glm::vec3 normal{0.0f};
    float offset{0.0f};
};

bool makeTriangle(const std::vector<glm::vec3>& points, uint32_t a, uint32_t b, uint32_t c,
                  Triangle& out) {
    const glm::vec3 normal = glm::cross(points[b] - points[a], points[c] - points[a]);
    const float length = glm::length(normal);
    // Three collinear points have no plane. Building one anyway is a normal of
    // zero over zero, and every later comparison against the NaN is false - so
    // the point that would have been outside is reported inside and the hull
    // quietly stops growing.
    if (length < 1.0e-12f) return false;

    out.a = a;
    out.b = b;
    out.c = c;
    out.normal = normal / length;
    out.offset = glm::dot(out.normal, points[a]);
    return true;
}

// Points that are not the same point.
//
// A mesh has one vertex per corner PER FACE, so a cube arrives as twenty-four
// points in eight places. Feeding those to the hull is not wrong so much as
// wasteful - but a pair of coincident points also has no plane between them,
// which is the degenerate case above waiting to happen.
std::vector<glm::vec3> deduplicate(const std::vector<glm::vec3>& points) {
    std::vector<glm::vec3> unique;
    unique.reserve(points.size());

    for (const glm::vec3& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
            continue;
        }
        bool seen = false;
        for (const glm::vec3& kept : unique) {
            if (glm::dot(kept - point, kept - point) < 1.0e-12f) {
                seen = true;
                break;
            }
        }
        if (!seen) unique.push_back(point);
    }
    return unique;
}

// Four points that actually enclose a volume.
//
// Two extremes, the point farthest from the line between them, and the point
// farthest from the plane those three make. Each step is what stops the next
// one being degenerate.
bool findSeed(const std::vector<glm::vec3>& points, uint32_t seed[4]) {
    if (points.size() < 4) return false;

    // The two most distant points along whichever world axis spreads widest,
    // which is a cheap start and never worse than picking arbitrarily.
    uint32_t low = 0;
    uint32_t high = 0;
    float widest = -1.0f;
    for (int axis = 0; axis < 3; ++axis) {
        uint32_t minIndex = 0;
        uint32_t maxIndex = 0;
        for (uint32_t i = 1; i < points.size(); ++i) {
            if (points[i][axis] < points[minIndex][axis]) minIndex = i;
            if (points[i][axis] > points[maxIndex][axis]) maxIndex = i;
        }
        const float spread = points[maxIndex][axis] - points[minIndex][axis];
        if (spread > widest) {
            widest = spread;
            low = minIndex;
            high = maxIndex;
        }
    }
    if (low == high) return false;   // every point in the same place

    const glm::vec3 along = points[high] - points[low];
    const float alongLength = glm::length(along);
    if (alongLength < 1.0e-6f) return false;
    const glm::vec3 direction = along / alongLength;

    // Farthest from the LINE. Collinear points all give zero here, which is the
    // "a hull of a straight line" case.
    uint32_t third = points.size();
    float bestDistance = kPlaneEpsilon;
    for (uint32_t i = 0; i < points.size(); ++i) {
        if (i == low || i == high) continue;
        const glm::vec3 offset = points[i] - points[low];
        const float distance = glm::length(offset - direction * glm::dot(offset, direction));
        if (distance > bestDistance) {
            bestDistance = distance;
            third = i;
        }
    }
    if (third == points.size()) return false;

    // Farthest from the PLANE. Coplanar points all give zero, which is "a hull
    // of a flat sheet" - a real thing to ask for and not a solid.
    const glm::vec3 normal =
        glm::normalize(glm::cross(points[high] - points[low], points[third] - points[low]));
    uint32_t fourth = points.size();
    bestDistance = kPlaneEpsilon;
    for (uint32_t i = 0; i < points.size(); ++i) {
        if (i == low || i == high || i == third) continue;
        const float distance = std::fabs(glm::dot(normal, points[i] - points[low]));
        if (distance > bestDistance) {
            bestDistance = distance;
            fourth = i;
        }
    }
    if (fourth == points.size()) return false;

    seed[0] = low;
    seed[1] = high;
    seed[2] = third;
    seed[3] = fourth;
    return true;
}

} // namespace

bool ConvexHull::Build(const std::vector<glm::vec3>& points) {
    m_vertices.clear();
    m_faces.clear();
    m_indices.clear();
    m_edges.clear();
    m_residual = 0.0f;
    m_boundsMin = glm::vec3(0.0f);
    m_boundsMax = glm::vec3(0.0f);

    const std::vector<glm::vec3> cloud = deduplicate(points);

    uint32_t seed[4];
    if (!findSeed(cloud, seed)) return false;

    // ---- The starting tetrahedron -------------------------------------------
    //
    // Each face is wound so its normal points AWAY from the fourth point, which
    // is what makes "outside" mean the same thing for all four and for every
    // face added after them.
    std::vector<Triangle> triangles;
    const auto addTriangle = [&](uint32_t a, uint32_t b, uint32_t c, const glm::vec3& inside) {
        Triangle triangle;
        if (!makeTriangle(cloud, a, b, c, triangle)) return;
        if (glm::dot(triangle.normal, inside) > triangle.offset) {
            std::swap(triangle.b, triangle.c);
            triangle.normal = -triangle.normal;
            triangle.offset = -triangle.offset;
        }
        triangles.push_back(triangle);
    };

    const glm::vec3 centroid =
        (cloud[seed[0]] + cloud[seed[1]] + cloud[seed[2]] + cloud[seed[3]]) * 0.25f;
    addTriangle(seed[0], seed[1], seed[2], centroid);
    addTriangle(seed[0], seed[1], seed[3], centroid);
    addTriangle(seed[0], seed[2], seed[3], centroid);
    addTriangle(seed[1], seed[2], seed[3], centroid);
    if (triangles.size() != 4) return false;

    std::set<uint32_t> used{seed[0], seed[1], seed[2], seed[3]};

    // ---- Grow it, farthest point first --------------------------------------
    //
    // Farthest-first is not an optimisation, it is what gives the vertex cap a
    // meaning: stopping early leaves a hull whose worst error is the distance
    // of the next point that would have been added, which is exactly what
    // `residual` reports.
    while (used.size() < kMaxVertices) {
        uint32_t farthest = cloud.size();
        float worst = kPlaneEpsilon;

        for (uint32_t i = 0; i < cloud.size(); ++i) {
            if (used.count(i) != 0) continue;
            float outside = 0.0f;
            for (const Triangle& triangle : triangles) {
                outside = std::max(outside, glm::dot(triangle.normal, cloud[i]) - triangle.offset);
            }
            if (outside > worst) {
                worst = outside;
                farthest = i;
            }
        }
        if (farthest == cloud.size()) break;   // everything is inside

        const glm::vec3 apex = cloud[farthest];

        // Every face this point can see. Those go; the rim they leave behind is
        // the horizon.
        std::vector<Triangle> hidden;
        std::vector<Triangle> kept;
        for (const Triangle& triangle : triangles) {
            if (glm::dot(triangle.normal, apex) > triangle.offset + kPlaneEpsilon) {
                hidden.push_back(triangle);
            } else {
                kept.push_back(triangle);
            }
        }
        if (hidden.empty()) {
            used.insert(farthest);
            continue;
        }

        // A directed edge of a visible face is on the horizon when its reverse
        // does NOT also belong to a visible face - which needs no adjacency
        // structure at all, only the set of directed edges being removed.
        std::set<std::pair<uint32_t, uint32_t>> directed;
        for (const Triangle& triangle : hidden) {
            directed.insert({triangle.a, triangle.b});
            directed.insert({triangle.b, triangle.c});
            directed.insert({triangle.c, triangle.a});
        }

        triangles = kept;
        for (const auto& edge : directed) {
            if (directed.count({edge.second, edge.first}) != 0) continue;

            // Wound from the horizon edge, so the new face keeps the outward
            // direction the face it replaced had.
            Triangle triangle;
            if (!makeTriangle(cloud, edge.first, edge.second, farthest, triangle)) continue;
            triangles.push_back(triangle);
        }

        used.insert(farthest);
    }

    if (triangles.size() < 4) return false;

    // What the cap cost, if it cost anything.
    for (uint32_t i = 0; i < cloud.size(); ++i) {
        float outside = 0.0f;
        for (const Triangle& triangle : triangles) {
            outside = std::max(outside, glm::dot(triangle.normal, cloud[i]) - triangle.offset);
        }
        m_residual = std::max(m_residual, outside);
    }

    // ---- Compact the vertices that survived ---------------------------------
    std::map<uint32_t, uint32_t> remap;
    for (const Triangle& triangle : triangles) {
        for (const uint32_t index : {triangle.a, triangle.b, triangle.c}) {
            if (remap.find(index) == remap.end()) {
                remap.emplace(index, static_cast<uint32_t>(m_vertices.size()));
                m_vertices.push_back(cloud[index]);
            }
        }
    }
    if (m_vertices.size() < 4) return false;

    // ---- Merge coplanar triangles into polygons -----------------------------
    //
    // The reason a Face is a polygon. SAT clips an incident face against a
    // reference face, and a triangular reference face on the flat side of a
    // crate gives a contact patch a third of the size it should be - so the
    // crate rests on a sliver and rocks about it.
    std::vector<bool> taken(triangles.size(), false);
    for (size_t i = 0; i < triangles.size(); ++i) {
        if (taken[i]) continue;

        const glm::vec3 normal = triangles[i].normal;
        const float offset = triangles[i].offset;

        // Every triangle in this plane, gathered so the rim can be walked.
        std::vector<std::pair<uint32_t, uint32_t>> rim;
        for (size_t j = i; j < triangles.size(); ++j) {
            if (taken[j]) continue;
            if (glm::dot(triangles[j].normal, normal) < 1.0f - kMergeNormalEpsilon) continue;
            if (std::fabs(triangles[j].offset - offset) > kMergeOffsetEpsilon) continue;

            taken[j] = true;
            rim.push_back({remap[triangles[j].a], remap[triangles[j].b]});
            rim.push_back({remap[triangles[j].b], remap[triangles[j].c]});
            rim.push_back({remap[triangles[j].c], remap[triangles[j].a]});
        }

        // An edge INSIDE the merged face appears twice, once in each direction.
        // What is left once those cancel is the boundary.
        std::vector<std::pair<uint32_t, uint32_t>> boundary;
        for (const auto& edge : rim) {
            const auto twin = std::find(rim.begin(), rim.end(),
                                        std::make_pair(edge.second, edge.first));
            if (twin == rim.end()) boundary.push_back(edge);
        }
        if (boundary.size() < 3) continue;

        // Walked into a loop, which is what makes it a polygon rather than a
        // bag of edges. A rim that does not close - which a merge across a
        // non-convex group would produce - is dropped rather than emitted as a
        // face nothing can clip against.
        Face face;
        face.normal = normal;
        face.offset = offset;
        face.first = static_cast<uint32_t>(m_indices.size());

        uint32_t start = boundary.front().first;
        uint32_t cursor = start;
        std::vector<bool> walked(boundary.size(), false);
        bool closed = false;

        for (size_t step = 0; step < boundary.size(); ++step) {
            m_indices.push_back(cursor);

            bool advanced = false;
            for (size_t e = 0; e < boundary.size(); ++e) {
                if (walked[e] || boundary[e].first != cursor) continue;
                walked[e] = true;
                cursor = boundary[e].second;
                advanced = true;
                break;
            }
            if (!advanced) break;
            if (cursor == start) {
                closed = true;
                break;
            }
        }

        if (!closed) {
            m_indices.resize(face.first);
            continue;
        }

        face.count = static_cast<uint32_t>(m_indices.size()) - face.first;
        if (face.count < 3) {
            m_indices.resize(face.first);
            continue;
        }
        m_faces.push_back(face);
    }

    if (m_faces.size() < 4) return false;

    // ---- One entry per undirected edge --------------------------------------
    std::set<std::pair<uint32_t, uint32_t>> unique;
    for (const Face& face : m_faces) {
        for (uint32_t i = 0; i < face.count; ++i) {
            const uint32_t a = m_indices[face.first + i];
            const uint32_t b = m_indices[face.first + (i + 1) % face.count];
            unique.insert({std::min(a, b), std::max(a, b)});
        }
    }
    m_edges.reserve(unique.size());
    for (const auto& edge : unique) m_edges.push_back(Edge{edge.first, edge.second});

    m_boundsMin = m_vertices.front();
    m_boundsMax = m_vertices.front();
    for (const glm::vec3& vertex : m_vertices) {
        m_boundsMin = glm::min(m_boundsMin, vertex);
        m_boundsMax = glm::max(m_boundsMax, vertex);
    }

    return true;
}

glm::vec3 ConvexHull::Support(const glm::vec3& direction) const {
    if (m_vertices.empty()) return glm::vec3(0.0f);

    // Linear. A hill-climb over the edge list is the usual improvement and is
    // only worth it well past the vertex cap this hull enforces: sixty-four
    // dot products is less work than the bookkeeping that would replace them.
    size_t best = 0;
    float furthest = glm::dot(m_vertices[0], direction);
    for (size_t i = 1; i < m_vertices.size(); ++i) {
        const float along = glm::dot(m_vertices[i], direction);
        if (along > furthest) {
            furthest = along;
            best = i;
        }
    }
    return m_vertices[best];
}

bool ConvexHull::IsConvex(float tolerance) const {
    if (!valid()) return false;
    for (const Face& face : m_faces) {
        for (const glm::vec3& vertex : m_vertices) {
            if (glm::dot(face.normal, vertex) > face.offset + tolerance) return false;
        }
    }
    return true;
}

bool ConvexHull::SatisfiesEulerFormula() const {
    if (!valid()) return false;
    const long long v = static_cast<long long>(m_vertices.size());
    const long long e = static_cast<long long>(m_edges.size());
    const long long f = static_cast<long long>(m_faces.size());
    return v - e + f == 2;
}

} // namespace Supersonic
