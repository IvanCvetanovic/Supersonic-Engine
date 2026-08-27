#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/ConvexHull.hpp"

namespace Supersonic {

// A shape that is not convex, as the convex pieces that make it up.
//
// A hull is convex by definition, so a doughnut collided as a disc and a chair
// as the block it sits in. The fix is not a better hull - it is several of
// them.
//
// Deliberately a BSP by axis-aligned splits rather than a voxel method like
// V-HACD. Not because the results are better; they are not. Because the results
// can be CHECKED. Splitting a closed mesh by a plane and clipping the triangles
// leaves both halves closed, so each piece has a volume, the pieces are
// interior-disjoint, and their volumes therefore ADD - which turns "is this
// decomposition right" from a matter of opinion into two inequalities:
//
//   sum of piece volumes >= the mesh's volume    (nothing was dropped)
//   sum of piece volumes <= the convex hull's    (nothing was invented that
//                                                 the single hull did not have)
//
// A voxel method satisfies neither exactly, and this project does not accept
// "it looks about right" as evidence.
class ConvexDecomposition {
public:
    struct Options {
        // Stop splitting a piece once its convex hull invents less than this
        // fraction of the hull's own volume. Zero would split forever; a shape
        // is never exactly its own hull once floating point is involved.
        float concavityFraction{0.03f};

        // A hard cap, because the recursion is driven by a measure that a
        // pathological mesh can keep failing. Eight is well past what a prop
        // needs and far below what the narrowphase would notice.
        uint32_t maxPieces{8};
    };

    bool Build(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
               const Options& options);

    // An OVERLOAD rather than a default argument of `Options{}`, and the reason
    // is a conformance rule rather than taste.
    //
    // A default argument is a complete-class context, but it is not a member
    // function BODY - and a default member initializer of a nested class may
    // only be used inside one. MSVC accepts `= Options{}` here; clang rejects
    // it outright with "default member initializer for 'concavityFraction'
    // needed within definition of enclosing class ... outside of member
    // functions", which is the standard's reading. An inline body is a
    // complete-class context where the initializers ARE available, so this
    // compiles everywhere and callers cannot tell the difference.
    //
    // Found by pointing the Android NDK's clang at this tree. It is not an
    // Android problem: any clang build of the engine hits it.
    bool Build(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices) {
        return Build(positions, indices, Options{});
    }

    const std::vector<ConvexHull>& pieces() const { return m_pieces; }
    bool valid() const { return !m_pieces.empty(); }

    // How much solid the pieces have between them that the mesh did not, in
    // world units cubed. Zero for a shape that was already convex, and it is
    // the number worth showing an author: it is exactly what a collider will
    // catch on that the mesh would not.
    float invented() const { return m_invented; }

    // What the input's own volume was, so `invented` can be read as a fraction.
    float meshVolume() const { return m_meshVolume; }

    glm::vec3 boundsMin() const { return m_boundsMin; }
    glm::vec3 boundsMax() const { return m_boundsMax; }

private:
    std::vector<ConvexHull> m_pieces;
    float m_invented{0.0f};
    float m_meshVolume{0.0f};
    glm::vec3 m_boundsMin{0.0f};
    glm::vec3 m_boundsMax{0.0f};
};

// The volume enclosed by a closed triangle mesh, by the divergence theorem.
//
// Signed: a mesh wound inside-out comes back negative, which is worth knowing
// rather than hiding behind an abs().
float SignedVolume(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices);

// The volume of a convex hull, by fanning each face into tetrahedra from the
// origin. Exact for any convex polyhedron, whatever its faces look like.
float HullVolume(const ConvexHull& hull);

// Whether every edge is shared by exactly two triangles with opposite winding.
//
// The precondition for every volume above. An open mesh has no volume, so a
// concavity measured on one is a number with no meaning - and it will still
// look like a number.
bool IsClosed(const std::vector<uint32_t>& indices);

} // namespace Supersonic
