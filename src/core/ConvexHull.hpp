#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Supersonic {

// The convex hull of a point cloud, as something the narrowphase can collide.
//
// The gap this closes was the last shape on the README's list: the narrowphase
// took a box, a sphere, a capsule and a heightfield, so anything else had to be
// approximated by a group of those. A ramp with a bevel, a rock, a wedge, a
// wing - each is one convex shape and each needed three or four boxes that
// never quite fit.
//
// Vulkan-free and registry-free, like CollisionSAT and Heightfield beside it.
//
// There is no closed form to check this against, unlike the heightfield's
// surface, so the suite leans on two other things instead: STRUCTURAL
// invariants that hold for every convex polyhedron - Euler's V - E + F = 2, and
// every vertex behind every face plane - and a DIFFERENTIAL check that a hull
// built from a cube's eight corners collides exactly the way the box path
// already does.
class ConvexHull {
public:
    // A polygon, not a triangle.
    //
    // The build produces triangles and then merges coplanar ones, and that
    // merge is not cosmetic: SAT clips an incident face against a reference
    // face, and a triangular reference face on the flat side of a crate gives a
    // contact patch a third of the size it should be. The crate then rocks on
    // it, which is exactly what the manifold's four points and the centroid
    // split exist to prevent.
    struct Face {
        glm::vec3 normal{0.0f, 1.0f, 0.0f};   // unit, outward
        float offset{0.0f};                   // dot(normal, p) == offset on the plane
        uint32_t first{0};                    // into the index list
        uint32_t count{0};
    };

    // Undirected, each pair once. SAT needs one direction per edge, not two.
    struct Edge {
        uint32_t a{0};
        uint32_t b{0};
    };

    // How many vertices a hull may end up with.
    //
    // Hull-against-hull SAT tests every face normal of both plus the cross
    // product of every EDGE PAIR, so the cost is quadratic in the edge count: a
    // pair of 300-edge hulls is ninety thousand axes. The build adds points in
    // farthest-first order and stops here, so a hull of a detailed mesh is the
    // shape of that mesh to within a stated tolerance rather than an exact hull
    // nobody can afford to collide - see `residual`.
    static constexpr uint32_t kMaxVertices = 64;

    // False for anything that is not a solid: fewer than four points, all of
    // them collinear, all of them coplanar, or a tetrahedron with no volume.
    // A collider built on one of those collides with nothing, which is the only
    // answer that is not a crash or a NaN.
    bool Build(const std::vector<glm::vec3>& points);

    bool valid() const { return m_faces.size() >= 4; }

    const std::vector<glm::vec3>& vertices() const { return m_vertices; }
    const std::vector<Face>& faces() const { return m_faces; }
    const std::vector<Edge>& edges() const { return m_edges; }
    const std::vector<uint32_t>& indices() const { return m_indices; }

    glm::vec3 boundsMin() const { return m_boundsMin; }
    glm::vec3 boundsMax() const { return m_boundsMax; }

    // How far outside the finished hull the worst input point ended up.
    //
    // Zero for anything under the vertex cap, which is every authored collision
    // shape. Non-zero says the cap bit, and by how much - so "the hull is
    // slightly smaller than the mesh" is a number somebody can look at rather
    // than a thing they have to discover.
    float residual() const { return m_residual; }

    // The farthest vertex along a direction, which is the whole of what SAT
    // needs to project a hull onto an axis.
    glm::vec3 Support(const glm::vec3& direction) const;

    // Every vertex is behind every face plane, to within `tolerance`. The
    // definition of convex, and the cheapest real check on a finished hull.
    bool IsConvex(float tolerance = 1e-3f) const;

    // V - E + F == 2 for any convex polyhedron. Catches a face that was
    // dropped, one that was added twice, and a merge that left a hole - none of
    // which show up in a picture.
    bool SatisfiesEulerFormula() const;

private:
    std::vector<glm::vec3> m_vertices;
    std::vector<Face> m_faces;
    std::vector<uint32_t> m_indices;
    std::vector<Edge> m_edges;

    glm::vec3 m_boundsMin{0.0f};
    glm::vec3 m_boundsMax{0.0f};
    float m_residual{0.0f};
};

} // namespace Supersonic
