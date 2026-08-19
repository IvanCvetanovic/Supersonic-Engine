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

Manifold CollideObbObb(const Obb& a, const Obb& b);

// Sphere against an oriented box. The AABB version of this was already exact
// for an axis-aligned box; this is the same idea with the query point taken
// into the box's own frame first.
bool CollideSphereObb(const glm::vec3& sphereCentre, float radius, const Obb& box,
                      glm::vec3& outNormal, float& outPenetration, glm::vec3& outPoint);

} // namespace CollisionSAT
} // namespace Supersonic
