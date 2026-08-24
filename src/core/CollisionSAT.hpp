#pragma once

#include <glm/glm.hpp>

namespace Supersonic {

// Oriented box-box collision by the separating axis theorem.
//
// Box against box was done as WORLD AXIS-ALIGNED boxes: the overlap of two
// AABBs, with the normal snapped to whichever of the six world axes had the
// least overlap. For an axis-aligned crate that is exact. For anything rotated
// it is not an approximation of the box, it is a different box.
//
// The concrete cost: a 10 x 1 x 1 ramp rotated 30 degrees has an AABB
// half-height of 2.933 against a true mid-surface half-height of 0.577, so a
// ball "rests" about 2.4 units above the visible surface - and the contact
// normal it rests on is exactly (0, 1, 0), because that is the world axis that
// won. A ball on a slope therefore sits in the air and never rolls, which means
// slopes cannot be built at all.
//
// Kept out of PhysicsSystem.cpp on purpose. This is the one part of the solver
// that is pure geometry - no registry, no components, no frame - so it can be
// tested directly with hand-checkable numbers, which is the only practical way
// to have any confidence in fifteen axis tests and a polygon clip.
namespace CollisionSAT {

struct Obb {
    glm::vec3 centre{0.0f};

    // LOCAL half extents, before rotation. Not the world AABB.
    glm::vec3 halfExtent{0.5f};

    // Columns are the box's world-space axes, unit length and orthonormal.
    // Identity means axis-aligned, which is what makes this a strict
    // generalisation of the AABB test it replaces.
    glm::mat3 axes{1.0f};
};

struct ContactPoint {
    glm::vec3 position{0.0f};
    float penetration{0.0f};
};

// Up to four points, which is what a face-face overlap between two boxes can
// produce after clipping. One point is enough to stop two boxes intersecting
// and not enough to stop them rocking: a crate resting flat on the ground is
// held by a single point somewhere in its footprint, so any disturbance rotates
// it about that point. Four is what makes a stack settle.
inline constexpr int kMaxContactPoints = 4;

struct Manifold {
    bool colliding{false};

    // True when the boxes are APART but within the speculative margin.
    //
    // A separated pair is still worth reporting when something is moving fast
    // enough to cross the gap inside one step. The solver can then remove just
    // enough approach velocity for the body to land ON the surface instead of
    // passing through it - which is what stops a projectile tunnelling without
    // any swept test at all.
    //
    // When this is set, points[0].penetration is NEGATIVE and is the size of
    // the gap.
    bool speculative{false};

    // Points from a toward b, so b is pushed along +normal. Same convention as
    // the rest of the solver.
    glm::vec3 normal{0.0f, 1.0f, 0.0f};

    int pointCount{0};
    ContactPoint points[kMaxContactPoints];

    // The deepest point, for callers that still want exactly one contact.
    float MaxPenetration() const {
        float deepest = 0.0f;
        for (int i = 0; i < pointCount; ++i) {
            if (points[i].penetration > deepest) deepest = points[i].penetration;
        }
        return deepest;
    }
};

// `speculativeMargin` is how far apart the boxes may be and still produce a
// contact. Zero gives ordinary touching-only collision, which is what every
// caller that does not care about fast movement should pass.
Manifold CollideObbObb(const Obb& a, const Obb& b, float speculativeMargin = 0.0f);

// Sphere against an oriented box. The AABB version of this was already exact
// for an axis-aligned box; this is the same idea with the query point taken
// into the box's own frame first.
// `speculativeMargin` behaves as it does for CollideObbObb: a gap up to that
// size still reports a contact, with a NEGATIVE penetration. The sphere path
// needs it as much as the box path does - a projectile is usually a sphere,
// and giving only boxes speculative contacts fixes tunnelling for the shape
// least likely to be doing the tunnelling.
bool CollideSphereObb(const glm::vec3& sphereCentre, float radius, const Obb& box,
                      glm::vec3& outNormal, float& outPenetration, glm::vec3& outPoint,
                      float speculativeMargin = 0.0f);

// ---- Capsules ---------------------------------------------------------------
//
// A capsule is a segment with a radius: every point within `radius` of the line
// between two endpoints. That is one definition covering three shapes, because a
// segment of zero length is a sphere - which is why the sphere tests below are
// not written twice.
//
// It is the shape a character wants. A box catches on every seam it walks over
// and a sphere rolls off everything, and neither can be made to stop doing that
// by tuning. A capsule slides up small steps and stands where it is put.

// The point on segment [a, b] nearest to p. Clamped, so a point beyond either
// end returns that end rather than a point on the infinite line.
glm::vec3 ClosestPointOnSegment(const glm::vec3& a, const glm::vec3& b, const glm::vec3& p);

// The point on triangle (a, b, c) nearest to p.
//
// Here rather than in the heightfield that wants it, because it is the same
// kind of thing as the two segment queries above - pure geometry with no
// registry, no components and no frame - and because the next shape that needs
// a triangle should find one rather than write a second.
//
// A degenerate triangle - two coincident corners, or three collinear ones -
// collapses to the nearest point on its longest edge rather than dividing by a
// zero area. A heightfield with two neighbouring vertices at the same place is
// not an error; it is a flat spot.
glm::vec3 ClosestPointOnTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                 const glm::vec3& p);

// The nearest pair of points between two segments.
//
// Parallel segments have no unique answer - any point along the overlap is as
// near as any other - so this returns one of them and callers must not depend on
// which. That is what makes two parallel capsules lying against each other rest
// on a single contact rather than along their length.
void ClosestPointsBetweenSegments(const glm::vec3& a0, const glm::vec3& a1,
                                  const glm::vec3& b0, const glm::vec3& b1,
                                  glm::vec3& outA, glm::vec3& outB);

// Capsule against capsule, and therefore sphere against sphere and capsule
// against sphere: pass a zero-length segment for a sphere.
bool CollideCapsuleCapsule(const glm::vec3& a0, const glm::vec3& a1, float radiusA,
                           const glm::vec3& b0, const glm::vec3& b1, float radiusB,
                           glm::vec3& outNormal, float& outPenetration, glm::vec3& outPoint,
                           float speculativeMargin = 0.0f);

// Capsule against an oriented box. The normal points from the BOX toward the
// capsule, matching the a-to-b convention when the box is a.
//
// Returns a manifold rather than one point for the same reason box-against-box
// does: a capsule lying ALONG a surface touches it in a line, and holding it
// with a single contact leaves it free to rock end over end about that point
// forever, because there is nothing anywhere else to resist. One point when a
// cap is what touches - a capsule standing upright, or leaning - and two when
// the axis runs along the surface.
Manifold CollideCapsuleObb(const glm::vec3& a0, const glm::vec3& a1, float radius,
                           const Obb& box, float speculativeMargin = 0.0f);

} // namespace CollisionSAT
} // namespace Supersonic
