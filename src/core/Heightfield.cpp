#include "core/Heightfield.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Supersonic {

namespace {

constexpr float kEpsilon = 1.0e-6f;

// How close two contacts have to be to be the same one. A hundredth of a
// millimetre in a world whose cells are one unit across: far below anything a
// solver can tell apart, and far above the last bit of a float at the edge of a
// sixty-four unit grid.
constexpr float kSamePointSquared = 1.0e-8f;

// The unit normal of one triangle, in the winding GenerateTerrainMesh uses.
//
// With kCellSize fixed at one, the cross product's y component is always
// exactly 1 before normalising, so this can never point downwards and can never
// be degenerate. The guard is for a Build() that came from somewhere else.
glm::vec3 faceNormal(const glm::vec3 triangle[3]) {
    const glm::vec3 normal = glm::cross(triangle[1] - triangle[0], triangle[2] - triangle[0]);
    const float length = glm::length(normal);
    return length > kEpsilon ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
}

// Moller-Trumbore, deliberately TWO-SIDED.
//
// A one-sided test is the usual choice and it is wrong for the job the queries
// need doing: PhysicsSystem::IsGrounded fires a ray downwards, but a body that
// has ended up inside a hill has to be able to ask which way out is, and a ray
// fired upwards from under the surface would find nothing at all.
bool rayHitsTriangle(const glm::vec3& origin, const glm::vec3& direction,
                     const glm::vec3 triangle[3], float& outDistance) {
    const glm::vec3 edge1 = triangle[1] - triangle[0];
    const glm::vec3 edge2 = triangle[2] - triangle[0];
    const glm::vec3 across = glm::cross(direction, edge2);
    const float determinant = glm::dot(edge1, across);
    if (std::fabs(determinant) < 1.0e-8f) return false;   // parallel to the plane

    const float inverseDeterminant = 1.0f / determinant;
    const glm::vec3 toOrigin = origin - triangle[0];

    const float u = glm::dot(toOrigin, across) * inverseDeterminant;
    if (u < 0.0f || u > 1.0f) return false;

    const glm::vec3 alongEdge = glm::cross(toOrigin, edge1);
    const float v = glm::dot(direction, alongEdge) * inverseDeterminant;
    if (v < 0.0f || u + v > 1.0f) return false;

    outDistance = glm::dot(edge2, alongEdge) * inverseDeterminant;
    return outDistance >= 0.0f;
}

// Decides what the collected contacts MEAN, once they are all in.
//
// A shape spanning several cells can be touching one and merely near another,
// and the two are not the same kind of report: a real overlap has to be pushed
// out, a gap must not be. So the moment anything is actually touching, the
// gaps are dropped. When nothing is, exactly ONE gap survives - the nearest -
// because the solver's speculative path removes just enough approach velocity
// to land on the surface, and applying that four times brakes four times as
// hard as it should.
void finalise(Heightfield::Manifold& manifold) {
    if (manifold.count == 0) return;

    int touching = 0;
    for (int i = 0; i < manifold.count; ++i) {
        if (manifold.points[i].penetration >= 0.0f) ++touching;
    }

    if (touching > 0) {
        int write = 0;
        for (int i = 0; i < manifold.count; ++i) {
            if (manifold.points[i].penetration >= 0.0f) manifold.points[write++] = manifold.points[i];
        }
        manifold.count = write;
        manifold.speculative = false;
        return;
    }

    int nearest = 0;
    for (int i = 1; i < manifold.count; ++i) {
        if (manifold.points[i].penetration > manifold.points[nearest].penetration) nearest = i;
    }
    manifold.points[0] = manifold.points[nearest];
    manifold.count = 1;
    manifold.speculative = true;
}

} // namespace

void Heightfield::Manifold::Add(const Contact& contact) {
    // The same spot, reported by every triangle that shares it.
    //
    // A sphere sitting exactly over a grid vertex is nearest to that one vertex
    // on all six triangles that meet there, and a capsule end over a cell edge
    // is nearest to the same point on two. That is ONE contact, not six:
    // handing the solver copies multiplies the impulse at that spot by however
    // many triangles happen to meet there, so a body would bounce harder
    // landing on a seam than a hand's width to either side of it - which is a
    // physical difference between two places that are geometrically identical.
    //
    // Position only. Two contacts at the same point necessarily have the same
    // normal, because the normal IS the direction from that point to the shape.
    for (int i = 0; i < count; ++i) {
        const glm::vec3 apart = points[i].position - contact.position;
        if (glm::dot(apart, apart) > kSamePointSquared) continue;
        if (contact.penetration > points[i].penetration) points[i] = contact;
        return;
    }

    if (count < CollisionSAT::kMaxContactPoints) {
        points[count++] = contact;
        return;
    }

    // Full. The shallowest is the one worth losing: it is the contact doing the
    // least to hold the shape up, and a crate spanning a dozen cells should be
    // held by the four corners it is actually resting on.
    int shallowest = 0;
    for (int i = 1; i < count; ++i) {
        if (points[i].penetration < points[shallowest].penetration) shallowest = i;
    }
    if (contact.penetration > points[shallowest].penetration) points[shallowest] = contact;
}

bool Heightfield::Build(uint32_t width, uint32_t depth, float thickness,
                        std::vector<float> heights) {
    m_width = 0;
    m_depth = 0;
    m_heights.clear();
    m_minHeight = 0.0f;
    m_maxHeight = 0.0f;

    // The same floor GenerateTerrainMesh applies, for the same reason: the cell
    // loops run to width - 1, and on an unsigned type a width of zero wraps.
    if (width < 2 || depth < 2) return false;
    if (heights.size() != static_cast<size_t>(width) * static_cast<size_t>(depth)) return false;

    m_width = width;
    m_depth = depth;
    m_thickness = std::max(thickness, 0.0f);
    m_heights = std::move(heights);

    const auto extremes = std::minmax_element(m_heights.begin(), m_heights.end());
    m_minHeight = *extremes.first;
    m_maxHeight = *extremes.second;
    return true;
}

glm::vec3 Heightfield::LocalMin() const {
    if (!valid()) return glm::vec3(0.0f);
    return glm::vec3(-static_cast<float>(m_width) * 0.5f,
                     m_minHeight - m_thickness,
                     -static_cast<float>(m_depth) * 0.5f);
}

glm::vec3 Heightfield::LocalMax() const {
    if (!valid()) return glm::vec3(0.0f);
    // The LAST vertex, not the first plus the width: a 64-wide grid has its
    // vertices at -32 through +31, so its maximum is 31 and its centre is at
    // -0.5. Writing +32 here is half a cell of terrain that is not there.
    return glm::vec3(static_cast<float>(m_width - 1) - static_cast<float>(m_width) * 0.5f,
                     m_maxHeight,
                     static_cast<float>(m_depth - 1) - static_cast<float>(m_depth) * 0.5f);
}

float Heightfield::HeightIndex(uint32_t x, uint32_t z) const {
    const uint32_t cx = std::min(x, m_width - 1);
    const uint32_t cz = std::min(z, m_depth - 1);
    return m_heights[static_cast<size_t>(cz) * m_width + cx];
}

glm::vec3 Heightfield::VertexAt(uint32_t x, uint32_t z) const {
    if (!valid()) return glm::vec3(0.0f);
    const uint32_t cx = std::min(x, m_width - 1);
    const uint32_t cz = std::min(z, m_depth - 1);
    // The SAME expression GenerateTerrainMesh writes into its vertices, so the
    // collider's corners are the mesh's corners bit for bit.
    return glm::vec3(static_cast<float>(cx) - static_cast<float>(m_width) * 0.5f,
                     HeightIndex(cx, cz),
                     static_cast<float>(cz) - static_cast<float>(m_depth) * 0.5f);
}

glm::vec2 Heightfield::ToGrid(float x, float z) const {
    return glm::vec2(x + static_cast<float>(m_width) * 0.5f,
                     z + static_cast<float>(m_depth) * 0.5f);
}

void Heightfield::CellAt(const glm::vec2& grid, uint32_t& outX, uint32_t& outZ,
                         float& outU, float& outV) const {
    const float lastX = static_cast<float>(m_width - 2);
    const float lastZ = static_cast<float>(m_depth - 2);
    // Clamped to the LAST cell rather than the last vertex, or a point exactly
    // on the far edge indexes a cell whose other two corners do not exist.
    const float cellX = std::clamp(std::floor(grid.x), 0.0f, lastX);
    const float cellZ = std::clamp(std::floor(grid.y), 0.0f, lastZ);

    outX = static_cast<uint32_t>(cellX);
    outZ = static_cast<uint32_t>(cellZ);
    outU = grid.x - cellX;
    outV = grid.y - cellZ;
}

void Heightfield::CellTriangles(uint32_t cellX, uint32_t cellZ, glm::vec3 out[2][3]) const {
    const glm::vec3 topLeft = VertexAt(cellX, cellZ);
    const glm::vec3 topRight = VertexAt(cellX + 1, cellZ);
    const glm::vec3 bottomLeft = VertexAt(cellX, cellZ + 1);
    const glm::vec3 bottomRight = VertexAt(cellX + 1, cellZ + 1);

    // The winding GenerateTerrainMesh emits, corner for corner: (TL, BL, TR)
    // then (TR, BL, BR). The shared edge is therefore BL-TR, the diagonal
    // u + v = 1, and getting it the other way round tilts every slope in the
    // collider a cell's worth away from the one you can see.
    out[0][0] = topLeft;
    out[0][1] = bottomLeft;
    out[0][2] = topRight;

    out[1][0] = topRight;
    out[1][1] = bottomLeft;
    out[1][2] = bottomRight;
}

bool Heightfield::HeightAt(float x, float z, float& outHeight) const {
    if (!valid()) return false;

    const glm::vec2 grid = ToGrid(x, z);
    if (grid.x < 0.0f || grid.x > static_cast<float>(m_width - 1)) return false;
    if (grid.y < 0.0f || grid.y > static_cast<float>(m_depth - 1)) return false;

    uint32_t cellX = 0;
    uint32_t cellZ = 0;
    float u = 0.0f;
    float v = 0.0f;
    CellAt(grid, cellX, cellZ, u, v);

    const float topLeft = HeightIndex(cellX, cellZ);
    const float topRight = HeightIndex(cellX + 1, cellZ);
    const float bottomLeft = HeightIndex(cellX, cellZ + 1);
    const float bottomRight = HeightIndex(cellX + 1, cellZ + 1);

    // Barycentric within the triangle the point is actually in, NOT a bilinear
    // blend of the four corners. Bilinear is the obvious thing to write and it
    // does not lie on the surface: it is a curved patch through the four
    // corners, and the mesh is two flat triangles. On a saddle cell the two
    // disagree by a visible amount right down the middle of the diagonal.
    if (u + v <= 1.0f) {
        outHeight = topLeft + u * (topRight - topLeft) + v * (bottomLeft - topLeft);
    } else {
        outHeight = bottomRight + (1.0f - u) * (bottomLeft - bottomRight) +
                    (1.0f - v) * (topRight - bottomRight);
    }
    return true;
}

bool Heightfield::TriangleAt(float x, float z, glm::vec3 outCorners[3],
                             glm::vec3& outNormal) const {
    if (!valid()) return false;

    const glm::vec2 grid = ToGrid(x, z);
    if (grid.x < 0.0f || grid.x > static_cast<float>(m_width - 1)) return false;
    if (grid.y < 0.0f || grid.y > static_cast<float>(m_depth - 1)) return false;

    uint32_t cellX = 0;
    uint32_t cellZ = 0;
    float u = 0.0f;
    float v = 0.0f;
    CellAt(grid, cellX, cellZ, u, v);

    glm::vec3 triangles[2][3];
    CellTriangles(cellX, cellZ, triangles);

    const int which = (u + v <= 1.0f) ? 0 : 1;
    for (int i = 0; i < 3; ++i) outCorners[i] = triangles[which][i];
    outNormal = faceNormal(triangles[which]);
    return true;
}

void Heightfield::CollectSphere(const glm::vec3& centre, float radius, float margin,
                                Manifold& out) const {
    if (!valid()) return;
    const float reach = radius + std::max(margin, 0.0f);

    // ---- Inside the solid ----
    //
    // Done FIRST and exclusively, because a centre under the surface is a
    // different question from one above it. The way out is straight up through
    // the top; letting the closest-point loop below also fire would have the
    // neighbouring cells push it sideways into more terrain at the same time,
    // and a body wedged inside a hill would grind along it instead of surfacing.
    float surface = 0.0f;
    if (HeightAt(centre.x, centre.z, surface)) {
        const float below = surface - centre.y;
        // Past the bottom of the slab it has left the terrain, and pushing it
        // back up through the whole hill would be a worse answer than letting
        // it go. That is what `thickness` is for.
        if (below > 0.0f && below <= m_thickness) {
            glm::vec3 corners[3];
            glm::vec3 normal(0.0f, 1.0f, 0.0f);
            TriangleAt(centre.x, centre.z, corners, normal);

            Contact contact;
            contact.position = glm::vec3(centre.x, surface, centre.z);
            contact.normal = normal;
            contact.penetration = radius + below;
            out.Add(contact);
            return;
        }
    }

    // ---- Above the surface ----
    //
    // Closest point on every triangle the reach overlaps. The front-side check
    // is one dot product, and it is one rather than an acceleration structure
    // because the shape is a function: there is no triangle hidden behind
    // another and no question about which side of the surface is out.
    //
    // What that does NOT get you for free is the seam problem, and it is worth
    // being exact about why, because the obvious version of this loop is wrong
    // on FLAT ground. A sphere sitting over one triangle is also within reach
    // of its neighbour, whose nearest point is the shared edge - so the
    // neighbour reports a second contact whose normal leans sideways out of a
    // surface that is not bent at all. A ball rolling across a field would be
    // shoved at every cell boundary.
    //
    // The rule below sorts it. A contact is a FACE contact when the nearest
    // point is the perpendicular foot, and an EDGE contact otherwise. An edge
    // contact is only ever real where the surface actually creases upwards -
    // the crest of a ridge, the rim of the grid - and in exactly those places
    // there is no face contact to be had. So: if this point has a face contact,
    // its edge contacts are the artefact and are dropped; if it has none, they
    // are the only thing holding it up and are kept.
    //
    // Per query POINT, not per manifold. A capsule with one cap on flat ground
    // and the other on a ridge needs the flat one's face contact and the
    // ridge's edge contact at the same time.
    const int lastCellX = static_cast<int>(m_width) - 2;
    const int lastCellZ = static_cast<int>(m_depth) - 2;

    const glm::vec2 grid = ToGrid(centre.x, centre.z);
    int minX = static_cast<int>(std::floor(grid.x - reach));
    int maxX = static_cast<int>(std::floor(grid.x + reach));
    int minZ = static_cast<int>(std::floor(grid.y - reach));
    int maxZ = static_cast<int>(std::floor(grid.y + reach));
    if (maxX < 0 || minX > lastCellX || maxZ < 0 || minZ > lastCellZ) return;

    // Clamped rather than rejected: a sphere hanging over the edge is still
    // stopped by the last row of cells, and the closest-point test gets the
    // overhang right on its own by returning a point on the boundary edge.
    minX = std::max(minX, 0);
    maxX = std::min(maxX, lastCellX);
    minZ = std::max(minZ, 0);
    maxZ = std::min(maxZ, lastCellZ);

    const float reachSquared = reach * reach;

    Manifold faces;
    Manifold edges;

    for (int cellZ = minZ; cellZ <= maxZ; ++cellZ) {
        for (int cellX = minX; cellX <= maxX; ++cellX) {
            glm::vec3 triangles[2][3];
            CellTriangles(static_cast<uint32_t>(cellX), static_cast<uint32_t>(cellZ), triangles);

            for (int which = 0; which < 2; ++which) {
                const glm::vec3* triangle = triangles[which];
                const glm::vec3 closest = CollisionSAT::ClosestPointOnTriangle(
                    triangle[0], triangle[1], triangle[2], centre);

                const glm::vec3 delta = centre - closest;
                const float distanceSquared = glm::dot(delta, delta);
                if (distanceSquared > reachSquared) continue;

                const glm::vec3 normal = faceNormal(triangle);
                const float alongNormal = glm::dot(delta, normal);
                // Front side only. Behind a triangle is the inside of the
                // solid, which the block above already answered.
                if (alongNormal <= 0.0f && distanceSquared > kEpsilon) continue;

                const float distance = std::sqrt(distanceSquared);

                Contact contact;
                contact.position = closest;
                // Toward the sphere from the nearest point on the surface. On
                // a face that is the face normal; on the crest of a ridge it is
                // not, and using the face normal there would push a ball
                // sideways off the top of a hill it is balanced on.
                contact.normal = distance > kEpsilon ? delta / distance : normal;
                contact.penetration = radius - distance;

                // The nearest point is the perpendicular foot exactly when the
                // separation is entirely along the normal, which is what makes
                // this a face contact rather than an edge one.
                const bool onFace =
                    alongNormal * alongNormal >= distanceSquared * (1.0f - 1.0e-4f);
                if (onFace) {
                    faces.Add(contact);
                } else {
                    edges.Add(contact);
                }
            }
        }
    }

    const Manifold& kept = faces.count > 0 ? faces : edges;
    for (int i = 0; i < kept.count; ++i) out.Add(kept.points[i]);
}

Heightfield::Manifold Heightfield::CollideSphere(const glm::vec3& centre, float radius,
                                                 float speculativeMargin) const {
    Manifold manifold;
    CollectSphere(centre, radius, speculativeMargin, manifold);
    finalise(manifold);
    return manifold;
}

Heightfield::Manifold Heightfield::CollideCapsule(const glm::vec3& a, const glm::vec3& b,
                                                  float radius, float speculativeMargin) const {
    Manifold manifold;
    CollectSphere(a, radius, speculativeMargin, manifold);

    // A capsule of no length is a sphere, and collecting the same end twice
    // would hand the solver two identical constraints and twice the impulse.
    const glm::vec3 along = b - a;
    if (glm::dot(along, along) > kEpsilon) {
        CollectSphere(b, radius, speculativeMargin, manifold);
    }

    finalise(manifold);
    return manifold;
}

Heightfield::Manifold Heightfield::CollideObb(const CollisionSAT::Obb& box,
                                              float speculativeMargin) const {
    Manifold manifold;
    if (!valid()) return manifold;

    // ---- The box's corners against the surface ----
    //
    // As spheres of no radius, so the same contact model answers all three
    // shapes rather than a fourth one written specially. With no radius the
    // only thing that can report is a corner that has actually gone under, and
    // it comes back pushed straight up by exactly how far it sank - which is
    // what makes a crate settle flat onto a slope rather than on one edge.
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 position =
            box.centre +
            box.axes[0] * ((corner & 1) ? box.halfExtent.x : -box.halfExtent.x) +
            box.axes[1] * ((corner & 2) ? box.halfExtent.y : -box.halfExtent.y) +
            box.axes[2] * ((corner & 4) ? box.halfExtent.z : -box.halfExtent.z);
        CollectSphere(position, 0.0f, speculativeMargin, manifold);
    }

    // ---- The surface's vertices against the box ----
    //
    // The dual, and not optional: a crate wider than a cell straddling a bump
    // has none of its corners under the ground and the bump straight through
    // its floor. Corners alone is the shortcut that makes boxes look like they
    // work until somebody builds a hill.
    const glm::vec3 extent =
        glm::abs(box.axes[0]) * box.halfExtent.x +
        glm::abs(box.axes[1]) * box.halfExtent.y +
        glm::abs(box.axes[2]) * box.halfExtent.z;

    const glm::vec2 minGrid = ToGrid(box.centre.x - extent.x, box.centre.z - extent.z);
    const glm::vec2 maxGrid = ToGrid(box.centre.x + extent.x, box.centre.z + extent.z);

    const int lastVertexX = static_cast<int>(m_width) - 1;
    const int lastVertexZ = static_cast<int>(m_depth) - 1;
    const int minX = std::max(static_cast<int>(std::ceil(minGrid.x)), 0);
    const int maxX = std::min(static_cast<int>(std::floor(maxGrid.x)), lastVertexX);
    const int minZ = std::max(static_cast<int>(std::ceil(minGrid.y)), 0);
    const int maxZ = std::min(static_cast<int>(std::floor(maxGrid.y)), lastVertexZ);

    // The box's axes are orthonormal, so its transpose is its inverse and
    // taking a point into its frame is three dot products.
    const glm::mat3 intoBox = glm::transpose(box.axes);

    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            const glm::vec3 vertex = VertexAt(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
            const glm::vec3 local = intoBox * (vertex - box.centre);

            const glm::vec3 remaining = box.halfExtent - glm::abs(local);
            if (remaining.x <= 0.0f || remaining.y <= 0.0f || remaining.z <= 0.0f) continue;

            // The face the vertex is nearest to leaving through, which is the
            // smallest translation that separates them. Any other axis would
            // push the box further than the overlap requires.
            int axis = 0;
            if (remaining.y < remaining[axis]) axis = 1;
            if (remaining.z < remaining[axis]) axis = 2;

            Contact contact;
            contact.position = vertex;
            // The vertex leaves along sign(local); the box therefore moves the
            // other way, and the manifold's normal is what the BOX is pushed
            // along.
            contact.normal = box.axes[axis] * (local[axis] < 0.0f ? 1.0f : -1.0f);
            contact.penetration = remaining[axis];
            manifold.Add(contact);
        }
    }

    finalise(manifold);
    return manifold;
}

Heightfield::Manifold Heightfield::CollideHull(const CollisionHull::Instance& hull,
                                              float speculativeMargin) const {
    Manifold manifold;
    if (!valid() || !hull.hull || !hull.hull->valid()) return manifold;

    const std::vector<glm::vec3>& vertices = hull.hull->vertices();

    // ---- The hull's vertices against the surface ----
    //
    // As spheres of no radius, exactly as the box passes its eight corners. The
    // whole contact model - the inside-the-solid branch, the front-side test,
    // the face-versus-edge rule - is CollectSphere's and is not re-derived here.
    glm::vec3 low(std::numeric_limits<float>::max());
    glm::vec3 high(std::numeric_limits<float>::lowest());

    for (const glm::vec3& local : vertices) {
        const glm::vec3 world = hull.origin + hull.basis * local;
        CollectSphere(world, 0.0f, speculativeMargin, manifold);

        // The hull's world AABB is the AABB of its transformed vertices, and
        // this loop is already visiting every one of them.
        low = glm::min(low, world);
        high = glm::max(high, world);
    }

    // ---- The surface's vertices against the hull ----
    //
    // The dual, and not optional for the same reason it is not optional for a
    // box: a hull wider than a cell straddling a bump has no vertex under the
    // ground and the bump straight through its underside.
    const glm::vec2 minGrid = ToGrid(low.x, low.z);
    const glm::vec2 maxGrid = ToGrid(high.x, high.z);

    const int lastVertexX = static_cast<int>(m_width) - 1;
    const int lastVertexZ = static_cast<int>(m_depth) - 1;
    const int minX = std::max(static_cast<int>(std::ceil(minGrid.x)), 0);
    const int maxX = std::min(static_cast<int>(std::floor(maxGrid.x)), lastVertexX);
    const int minZ = std::max(static_cast<int>(std::ceil(minGrid.y)), 0);
    const int maxZ = std::min(static_cast<int>(std::floor(maxGrid.y)), lastVertexZ);

    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            const glm::vec3 vertex = VertexAt(static_cast<uint32_t>(x), static_cast<uint32_t>(z));

            // Y first, and before the hull test rather than inside it. On a
            // hill almost every vertex of the xz footprint is far above or far
            // below the hull, and ClosestPointOnHull walks every face.
            if (vertex.y < low.y || vertex.y > high.y) continue;

            bool inside = false;
            const glm::vec3 exit = CollisionHull::ClosestPointOnHull(hull, vertex, inside);
            if (!inside) continue;

            const glm::vec3 delta = exit - vertex;
            const float depth = glm::length(delta);

            // A vertex exactly on the surface separates nothing and has no
            // direction to be separated along. The box path drops the same case
            // with its strict `remaining <= 0` test.
            if (depth <= 1.0e-6f) continue;

            Contact contact;
            contact.position = vertex;
            // The vertex leaves along `delta`, so the HULL is pushed the other
            // way - the manifold's normal is what the hull moves along, which
            // is the convention CollideObb uses for a box.
            contact.normal = -delta / depth;
            contact.penetration = depth;
            manifold.Add(contact);
        }
    }

    finalise(manifold);
    return manifold;
}

bool Heightfield::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                          float& outDistance, glm::vec3& outNormal) const {
    if (!valid() || maxDistance <= 0.0f) return false;

    const glm::vec2 gridOrigin = ToGrid(origin.x, origin.z);
    const glm::vec2 gridDirection(direction.x, direction.z);

    // ---- The span of the ray that is over the grid at all ----
    //
    // Without this the march would start at the cell under the ray's ORIGIN,
    // which for a camera pick is usually nowhere near the terrain, and would
    // then walk every cell in between. Clipping first makes the first cell
    // tested the first one the ray actually crosses.
    float enter = 0.0f;
    float exit = maxDistance;
    const float extent[2] = {static_cast<float>(m_width - 1), static_cast<float>(m_depth - 1)};
    for (int axis = 0; axis < 2; ++axis) {
        const float from = gridOrigin[axis];
        const float along = gridDirection[axis];
        if (std::fabs(along) < kEpsilon) {
            // Parallel to this axis: either it is inside the span for the whole
            // ray or it never enters at all.
            if (from < 0.0f || from > extent[axis]) return false;
            continue;
        }
        float first = (0.0f - from) / along;
        float last = (extent[axis] - from) / along;
        if (first > last) std::swap(first, last);
        enter = std::max(enter, first);
        exit = std::min(exit, last);
    }
    if (enter > exit) return false;

    const glm::vec2 entry = gridOrigin + gridDirection * enter;
    int cellX = std::clamp(static_cast<int>(std::floor(entry.x)), 0, static_cast<int>(m_width) - 2);
    int cellZ = std::clamp(static_cast<int>(std::floor(entry.y)), 0, static_cast<int>(m_depth) - 2);

    const int stepX = gridDirection.x > 0.0f ? 1 : (gridDirection.x < 0.0f ? -1 : 0);
    const int stepZ = gridDirection.y > 0.0f ? 1 : (gridDirection.y < 0.0f ? -1 : 0);

    // A CELL march, not a fixed step. A ray sampled every so many units steps
    // straight over a ridge whenever the ridge is thinner than the step, and
    // the symptom - a shot that passes through a hill about one time in ten -
    // is close to impossible to reproduce on purpose.
    const float huge = std::numeric_limits<float>::max();
    const float deltaX = stepX != 0 ? std::fabs(1.0f / gridDirection.x) : huge;
    const float deltaZ = stepZ != 0 ? std::fabs(1.0f / gridDirection.y) : huge;

    const auto toBoundary = [](float position, int step) {
        return step > 0 ? (std::floor(position) + 1.0f - position) : (position - std::floor(position));
    };
    float nextX = stepX != 0 ? enter + toBoundary(entry.x, stepX) / std::fabs(gridDirection.x) : huge;
    float nextZ = stepZ != 0 ? enter + toBoundary(entry.y, stepZ) / std::fabs(gridDirection.y) : huge;

    for (;;) {
        glm::vec3 triangles[2][3];
        CellTriangles(static_cast<uint32_t>(cellX), static_cast<uint32_t>(cellZ), triangles);

        float nearest = -1.0f;
        glm::vec3 nearestNormal(0.0f, 1.0f, 0.0f);
        for (int which = 0; which < 2; ++which) {
            float distance = 0.0f;
            if (!rayHitsTriangle(origin, direction, triangles[which], distance)) continue;
            if (distance > maxDistance) continue;
            if (nearest < 0.0f || distance < nearest) {
                nearest = distance;
                nearestNormal = faceNormal(triangles[which]);
            }
        }

        // The cells are visited in order along the ray, so the first cell with
        // a hit holds the nearest one and there is nothing further to check.
        if (nearest >= 0.0f) {
            outDistance = nearest;
            outNormal = nearestNormal;
            return true;
        }

        if (nextX <= nextZ) {
            if (nextX > exit) break;
            cellX += stepX;
            nextX += deltaX;
        } else {
            if (nextZ > exit) break;
            cellZ += stepZ;
            nextZ += deltaZ;
        }

        if (cellX < 0 || cellX > static_cast<int>(m_width) - 2) break;
        if (cellZ < 0 || cellZ > static_cast<int>(m_depth) - 2) break;
    }

    return false;
}

} // namespace Supersonic
