#include "core/ConvexDecomposition.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace Supersonic {

namespace {

constexpr float kOnPlane = 1.0e-5f;

// One triangle, carried by value through the recursion. A split rewrites the
// list rather than indexing into a shared one, because a cut introduces
// vertices that exist on one side and not the other.
struct Triangle {
    glm::vec3 v[3];
};

float triangleVolume(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return glm::dot(a, glm::cross(b, c)) / 6.0f;
}

float volumeOf(const std::vector<Triangle>& mesh) {
    float total = 0.0f;
    for (const Triangle& t : mesh) total += triangleVolume(t.v[0], t.v[1], t.v[2]);
    return total;
}

void boundsOf(const std::vector<Triangle>& mesh, glm::vec3& low, glm::vec3& high) {
    low = glm::vec3(std::numeric_limits<float>::max());
    high = glm::vec3(std::numeric_limits<float>::lowest());
    for (const Triangle& t : mesh) {
        for (const glm::vec3& p : t.v) {
            low = glm::min(low, p);
            high = glm::max(high, p);
        }
    }
}

std::vector<glm::vec3> pointsOf(const std::vector<Triangle>& mesh) {
    std::vector<glm::vec3> points;
    points.reserve(mesh.size() * 3);
    for (const Triangle& t : mesh) {
        points.push_back(t.v[0]);
        points.push_back(t.v[1]);
        points.push_back(t.v[2]);
    }
    return points;
}

// Splits one triangle by an axis-aligned plane, appending whole or clipped
// pieces to each side.
//
// The cut points go to BOTH sides, which is what keeps each half closed: the
// two halves share the cut edge exactly, so neither has a gap along it and each
// still encloses a volume that can be measured. That is the whole reason this
// is a BSP and not a voxel method - closed halves are what make the volumes
// add, and volumes that add are what make the result checkable.
void clipTriangle(const Triangle& tri, int axis, float where,
                  std::vector<Triangle>& below, std::vector<Triangle>& above) {
    float d[3];
    for (int i = 0; i < 3; ++i) d[i] = tri.v[i][axis] - where;

    const bool anyBelow = d[0] < -kOnPlane || d[1] < -kOnPlane || d[2] < -kOnPlane;
    const bool anyAbove = d[0] > kOnPlane || d[1] > kOnPlane || d[2] > kOnPlane;

    if (!anyAbove) {
        below.push_back(tri);
        return;
    }
    if (!anyBelow) {
        above.push_back(tri);
        return;
    }

    std::vector<glm::vec3> lowSide;
    std::vector<glm::vec3> highSide;

    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        const glm::vec3& p = tri.v[i];
        const glm::vec3& q = tri.v[j];

        // A vertex ON the plane joins both polygons, which is why these are
        // signed comparisons and not strict ones.
        if (d[i] <= kOnPlane) lowSide.push_back(p);
        if (d[i] >= -kOnPlane) highSide.push_back(p);

        const bool crosses = (d[i] < -kOnPlane && d[j] > kOnPlane) ||
                             (d[i] > kOnPlane && d[j] < -kOnPlane);
        if (!crosses) continue;

        const float t = d[i] / (d[i] - d[j]);
        glm::vec3 cut = p + (q - p) * t;
        // Snapped onto the plane rather than left where the interpolation put
        // it. The two sides have to agree about where the cut is to the last
        // bit, or the shared edge develops a sliver and the volumes stop
        // adding - which would quietly break the only oracle this has.
        cut[axis] = where;
        lowSide.push_back(cut);
        highSide.push_back(cut);
    }

    const auto fan = [](const std::vector<glm::vec3>& polygon, std::vector<Triangle>& out) {
        for (size_t i = 2; i < polygon.size(); ++i) {
            out.push_back(Triangle{{polygon[0], polygon[i - 1], polygon[i]}});
        }
    };
    fan(lowSide, below);
    fan(highSide, above);
}

// The plane that leaves the least total hull volume, out of the three axis
// midpoints.
//
// Judged on HULL volume alone, never on the halves' own mesh volume. Clipping
// leaves each half OPEN where the cut went through it - the walls are there and
// the cap over the cut is not - and the divergence theorem gives an open
// surface a number that looks like a volume and is not one. Hull volume needs
// no such thing: it is computed from the piece's own closed hull.
//
// The bound the tests rest on survives this. hull(below) lies entirely in
// x <= where and hull(above) entirely in x >= where, so they meet only in the
// plane and their volumes still add.
//
// Axis-aligned, and only three candidates. An arbitrary plane search is where a
// decomposition turns into a research project, and the shapes this exists for -
// a chair, an L, a doughnut - are split correctly by an axis anyway.
bool chooseSplit(const std::vector<Triangle>& mesh, const glm::vec3& low, const glm::vec3& high,
                 int& outAxis, float& outWhere, float& outVolume) {
    float best = std::numeric_limits<float>::max();
    bool found = false;

    for (int axis = 0; axis < 3; ++axis) {
        if (high[axis] - low[axis] <= kOnPlane * 10.0f) continue;
        const float where = (low[axis] + high[axis]) * 0.5f;

        std::vector<Triangle> below;
        std::vector<Triangle> above;
        for (const Triangle& t : mesh) clipTriangle(t, axis, where, below, above);
        if (below.empty() || above.empty()) continue;

        ConvexHull hullBelow;
        ConvexHull hullAbove;
        if (!hullBelow.Build(pointsOf(below)) || !hullAbove.Build(pointsOf(above))) continue;

        const float total = HullVolume(hullBelow) + HullVolume(hullAbove);
        if (total < best) {
            best = total;
            outAxis = axis;
            outWhere = where;
            outVolume = total;
            found = true;
        }
    }
    return found;
}

void split(const std::vector<Triangle>& mesh, const ConvexDecomposition::Options& options,
           uint32_t budget, std::vector<ConvexHull>& out) {
    ConvexHull hull;
    if (!hull.Build(pointsOf(mesh))) return;

    const float hullVolume = HullVolume(hull);
    if (budget <= 1 || hullVolume <= 0.0f) {
        out.push_back(std::move(hull));
        return;
    }

    glm::vec3 low(0.0f);
    glm::vec3 high(0.0f);
    boundsOf(mesh, low, high);

    int axis = 0;
    float where = 0.0f;
    float afterSplit = 0.0f;
    if (!chooseSplit(mesh, low, high, axis, where, afterSplit)) {
        out.push_back(std::move(hull));
        return;
    }

    // Stop when the split no longer buys enough. Splitting a piece that is
    // already convex removes nothing at all - its two halves are their own
    // hulls and their volumes sum to what the parent already had - so this is
    // also what makes a convex input come back as exactly one piece.
    if (afterSplit >= hullVolume * (1.0f - options.concavityFraction)) {
        out.push_back(std::move(hull));
        return;
    }

    std::vector<Triangle> below;
    std::vector<Triangle> above;
    for (const Triangle& t : mesh) clipTriangle(t, axis, where, below, above);
    if (below.empty() || above.empty()) {
        out.push_back(std::move(hull));
        return;
    }

    // Halved rather than decremented, so a shape that keeps splitting cannot
    // spend the whole allowance down one branch and leave the other as a single
    // block.
    const uint32_t half = budget / 2;
    split(below, options, half, out);
    split(above, options, budget - half, out);
}

} // namespace

float SignedVolume(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices) {
    float total = 0.0f;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        if (indices[i] >= positions.size() || indices[i + 1] >= positions.size() ||
            indices[i + 2] >= positions.size()) {
            continue;
        }
        total += triangleVolume(positions[indices[i]], positions[indices[i + 1]],
                                positions[indices[i + 2]]);
    }
    return total;
}

float HullVolume(const ConvexHull& hull) {
    if (!hull.valid()) return 0.0f;

    const std::vector<glm::vec3>& vertices = hull.vertices();
    const std::vector<uint32_t>& indices = hull.indices();

    float total = 0.0f;
    for (const ConvexHull::Face& face : hull.faces()) {
        // Every face is a polygon in the index list. Fanning it from its own
        // first vertex is valid because a hull's faces are convex by
        // construction.
        for (uint32_t i = 2; i < face.count; ++i) {
            const uint32_t a = indices[face.first];
            const uint32_t b = indices[face.first + i - 1];
            const uint32_t c = indices[face.first + i];
            if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) continue;
            total += triangleVolume(vertices[a], vertices[b], vertices[c]);
        }
    }
    return std::fabs(total);
}

bool IsClosed(const std::vector<uint32_t>& indices) {
    if (indices.empty() || indices.size() % 3 != 0) return false;

    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        for (int e = 0; e < 3; ++e) {
            const uint32_t a = indices[i + e];
            const uint32_t b = indices[i + (e + 1) % 3];
            ++edges[std::make_pair(a, b)];
        }
    }

    for (const auto& entry : edges) {
        // Exactly once in this direction, and exactly once in the other. A
        // shared edge traversed the same way twice is two triangles facing
        // opposite ways, which encloses nothing however closed it looks.
        if (entry.second != 1) return false;
        const auto opposite = edges.find(std::make_pair(entry.first.second, entry.first.first));
        if (opposite == edges.end() || opposite->second != 1) return false;
    }
    return true;
}

bool ConvexDecomposition::Build(const std::vector<glm::vec3>& positions,
                                const std::vector<uint32_t>& indices, const Options& options) {
    m_pieces.clear();
    m_invented = 0.0f;
    m_meshVolume = 0.0f;

    if (positions.empty() || indices.size() < 3) return false;

    std::vector<Triangle> mesh;
    mesh.reserve(indices.size() / 3);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        if (indices[i] >= positions.size() || indices[i + 1] >= positions.size() ||
            indices[i + 2] >= positions.size()) {
            continue;
        }
        mesh.push_back(
            Triangle{{positions[indices[i]], positions[indices[i + 1]], positions[indices[i + 2]]}});
    }
    if (mesh.empty()) return false;

    m_meshVolume = std::fabs(volumeOf(mesh));

    split(mesh, options, std::max(options.maxPieces, 1u), m_pieces);
    if (m_pieces.empty()) return false;

    float pieceVolume = 0.0f;
    for (const ConvexHull& piece : m_pieces) pieceVolume += HullVolume(piece);
    m_invented = std::max(pieceVolume - m_meshVolume, 0.0f);

    boundsOf(mesh, m_boundsMin, m_boundsMax);
    return true;
}

} // namespace Supersonic
