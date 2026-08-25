#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/CollisionHull.hpp"

#include "core/CollisionSAT.hpp"

namespace Supersonic {

// A collision surface described as a function of x and z.
//
// The named gap this closes: the procedurally generated terrain was scenery you
// fell through. It could not be a box - it is not one - and approximating it
// with a stack of them would need thousands, each of which the broadphase has
// to sort every step.
//
// What makes a heightfield worth writing rather than a general triangle-mesh
// collider is that it is a FUNCTION. There are no overhangs, so for any point
// in the world there is exactly one cell beneath it, no triangle is ever hidden
// behind another, and the cells a shape can possibly touch are an index range
// rather than a search. A mesh collider has to build an acceleration structure
// to get the same answer and then carry a pile of internal-edge filtering to
// stop shapes catching on seams that are not really there.
//
// Vulkan-free and registry-free on purpose, like CollisionSAT next to it: this
// is the part where a wrong sign is a character standing three units above the
// ground, and the only practical way to have any confidence in it is to be able
// to call it with hand-checkable numbers.
class Heightfield {
public:
    // One local unit per cell.
    //
    // Not a parameter, because TerrainGenerator::GenerateTerrainMesh does not
    // have one either: its vertices sit at integer offsets and the entity's
    // scale is what makes the terrain bigger. A cell size here that the mesh
    // could not honour would be a second description of the same surface, which
    // is the drift SampleHeight exists to stop. Scale the entity instead.
    static constexpr float kCellSize = 1.0f;

    // `width` and `depth` are VERTEX counts per axis, matching
    // GenerateTerrainMesh's arguments, so a grid of w x d has (w-1) x (d-1)
    // cells and both must be at least 2. `heights` is row-major with z outer,
    // which is the order the mesh writes its vertices in.
    //
    // Returns false and leaves the field empty rather than throwing: a scene
    // asking for a 1 x 1 terrain should have no collider, not no process.
    bool Build(uint32_t width, uint32_t depth, float thickness, std::vector<float> heights);

    bool valid() const { return m_width >= 2 && m_depth >= 2; }
    uint32_t width() const { return m_width; }
    uint32_t depth() const { return m_depth; }
    float thickness() const { return m_thickness; }
    const std::vector<float>& heights() const { return m_heights; }

    // The grid is centred the way the mesh is: vertex (x, z) sits at
    // (x - width/2, h, z - depth/2). For an even vertex count that is NOT
    // symmetric about the origin - a 64-wide grid spans -32 to +31 - and the
    // collider has to reproduce it exactly or it sits half a unit off the thing
    // you can see.
    glm::vec3 LocalMin() const;
    glm::vec3 LocalMax() const;

    // Local position of one grid vertex. Out-of-range indices are clamped.
    glm::vec3 VertexAt(uint32_t x, uint32_t z) const;

    // The surface height directly above or below (x, z), in local space.
    // False outside the grid's footprint, where there is no surface at all.
    bool HeightAt(float x, float z, float& outHeight) const;

    // The one triangle under (x, z): its corners in the mesh's winding order
    // and its unit face normal, which always has a positive y.
    bool TriangleAt(float x, float z, glm::vec3 outCorners[3], glm::vec3& outNormal) const;

    // ---- Contacts ---------------------------------------------------------
    //
    // Every query is in the heightfield's LOCAL space; the caller takes the
    // shape in and the normals back out. Normals point OUT of the surface,
    // toward the shape.

    struct Contact {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};

        // Negative for a speculative contact: the shape is APART by that much
        // and near enough to close the gap inside a step.
        float penetration{0.0f};
    };

    // Per-point NORMALS, unlike CollisionSAT::Manifold, which carries one for
    // the whole manifold. Two boxes touching face to face really do share a
    // normal; the two ends of a capsule lying across a ridge do not, and giving
    // them the ridge's average would push both the wrong way.
    struct Manifold {
        int count{0};
        Contact points[CollisionSAT::kMaxContactPoints];

        // True when NOTHING is actually touching and the deepest entry is a gap
        // the shape may still cross this step.
        bool speculative{false};

        // Keeps the deepest kMaxContactPoints, so a shape spanning a dozen
        // cells is held by the four that matter rather than the four the loop
        // happened to reach first.
        void Add(const Contact& contact);
    };

    // `speculativeMargin` is how far away the shape may be and still produce a
    // contact, exactly as in CollisionSAT: zero gives ordinary touching-only
    // collision. Terrain is the shape most exposed to tunnelling - it is a
    // surface, not a volume - so the margin matters more here than anywhere.
    Manifold CollideSphere(const glm::vec3& centre, float radius,
                           float speculativeMargin = 0.0f) const;

    // Both ends as spheres, which is what CollideCapsuleObb documents doing and
    // is right for the shape's purpose: a character stands on its lower cap.
    //
    // The limitation, stated rather than solved: a capsule lying horizontally
    // across a ridge that touches neither end rests on nothing. Characters
    // freeze rotation and stand upright, and a capsule used as loose debris is
    // held by whichever cap is lower, which is the case that would otherwise
    // sink.
    Manifold CollideCapsule(const glm::vec3& a, const glm::vec3& b, float radius,
                            float speculativeMargin = 0.0f) const;

    // The box's eight corners against the surface, AND the surface's vertices
    // against the box.
    //
    // Corners alone is the usual shortcut and it is wrong in a way that is easy
    // to miss: a crate wider than a cell, straddling a bump, rests on its
    // corners and the bump passes through its underside. The second half is the
    // dual test and costs a point-in-box check per vertex of the footprint.
    Manifold CollideObb(const CollisionSAT::Obb& box, float speculativeMargin = 0.0f) const;

    // A convex hull against the surface, and the surface's vertices against
    // the hull. The same two halves as CollideObb, for the same reasons.
    //
    // Not a fourth contact model. CollideObb IS this function specialised to a
    // cube: its eight corners are the cube hull's eight vertices, and its
    // least-exit-axis is what ClosestPointOnHull computes for a point inside
    // any convex shape. Run a cube hull through here and it executes the same
    // arithmetic and must produce the same manifold, which is what makes that
    // comparison a test rather than a gesture.
    //
    // Until this existed a hull on terrain collided as its world bounding box,
    // so a wedge on a hill floated on the corner of a box nobody could see.
    Manifold CollideHull(const CollisionHull::Instance& hull,
                         float speculativeMargin = 0.0f) const;

    // Nearest surface hit along a local-space ray. `direction` must be unit
    // length. The march is over CELLS rather than a fixed step, so a ray that
    // grazes a ridge cannot step over it.
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                 float& outDistance, glm::vec3& outNormal) const;

private:
    // Grid coordinates of a local point: local x plus half the width, so
    // (0, 0) is the corner vertex rather than the centre.
    glm::vec2 ToGrid(float x, float z) const;

    // The cell containing a grid coordinate, clamped into range, with the
    // fractional part left in outU/outV.
    void CellAt(const glm::vec2& grid, uint32_t& outX, uint32_t& outZ,
                float& outU, float& outV) const;

    float HeightIndex(uint32_t x, uint32_t z) const;

    // The two triangles of one cell, in the mesh's winding order.
    void CellTriangles(uint32_t cellX, uint32_t cellZ, glm::vec3 out[2][3]) const;

    // One sphere against the cells its reach overlaps. The whole contact model
    // lives here; the capsule and box paths are callers.
    void CollectSphere(const glm::vec3& centre, float radius, float margin,
                       Manifold& out) const;

    uint32_t m_width{0};
    uint32_t m_depth{0};
    float m_thickness{4.0f};
    float m_minHeight{0.0f};
    float m_maxHeight{0.0f};
    std::vector<float> m_heights;
};

} // namespace Supersonic
