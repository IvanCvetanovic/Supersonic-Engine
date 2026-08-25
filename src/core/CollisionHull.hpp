#pragma once

#include <glm/glm.hpp>

#include "core/CollisionSAT.hpp"
#include "core/ConvexHull.hpp"

namespace Supersonic {

// Colliding a convex hull, which is the last shape the narrowphase was missing.
//
// Separate from CollisionSAT for the same reason CollisionSAT is separate from
// PhysicsSystem: the box path there is written around three axes and four
// corners per face - `best.index < 3`, `(referenceAxis + 1) % 3`, a fixed array
// of four - and none of that generalises. This is the same ALGORITHM with the
// faces and edges read from data instead of counted out.
namespace CollisionHull {

// A hull placed in the world.
//
// The basis carries rotation AND scale, and unlike a sphere that costs nothing:
// a linear transform of a convex set is still convex, so a hull squashed on one
// axis collides as exactly the shape it looks like. It is the only collider
// here for which that is true.
struct Instance {
    const ConvexHull* hull{nullptr};

    // Where the hull's own origin sits, in the world.
    glm::vec3 origin{0.0f};

    // Rotation and scale together, applied to a vertex.
    glm::mat3 basis{1.0f};

    // The inverse transpose, which is what a PLANE normal transforms by. Using
    // the basis for a normal is right only while the scale is uniform, and the
    // failure is a face whose normal no longer points out of it - so a squashed
    // hull develops a separating axis that is not perpendicular to anything.
    glm::mat3 normalBasis{1.0f};
};

// A hull placed by its transform. Returns false when the basis cannot be
// inverted - a collider scaled to nothing on some axis - because a hull with no
// thickness has no outside.
bool MakeInstance(const ConvexHull& hull, const glm::vec3& origin, const glm::mat3& basis,
                  Instance& out);

// The unit cube, built once.
//
// An oriented box IS this hull with its half extents on the basis, which is why
// box-against-hull is not a fifth pair test with its own bugs.
const ConvexHull& UnitCube();

// That cube, placed where an Obb is.
Instance InstanceFromObb(const CollisionSAT::Obb& box);

// Separating axis theorem over both hulls' face normals and every pair of their
// edge directions, then the incident face clipped against the reference face.
//
// The normal points from `a` toward `b`, matching every other pair test here.
//
// The cost is what the vertex cap in ConvexHull exists to bound: the edge pairs
// are quadratic, so two hulls with three hundred edges each would be ninety
// thousand axes.
CollisionSAT::Manifold CollideHullHull(const Instance& a, const Instance& b,
                                       float speculativeMargin = 0.0f);

// A capsule against a hull. The normal points from the HULL toward the capsule,
// which is the a-toward-b convention when the hull is a.
//
// A sphere is a capsule whose segment has no length, so this is both round
// shapes - the same choice the rest of the narrowphase makes, and for the same
// reason: two implementations of one test disagree about a case neither author
// thought of.
CollisionSAT::Manifold CollideCapsuleHull(const glm::vec3& a0, const glm::vec3& a1, float radius,
                                          const Instance& hull, float speculativeMargin = 0.0f);

// The nearest point on the hull's surface to `point`, in world space.
//
// `outInside` says whether the point was within the hull, in which case the
// answer is on the face it is nearest to leaving through - which is the
// direction a body buried in a rock has to be pushed.
glm::vec3 ClosestPointOnHull(const Instance& hull, const glm::vec3& point, bool& outInside);

} // namespace CollisionHull
} // namespace Supersonic
