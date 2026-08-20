#include "core/CollisionSAT.hpp"

#include <algorithm>
#include <cmath>

namespace Supersonic::CollisionSAT {

namespace {

// Guards the cross-product axes.
//
// When two boxes have a pair of parallel axes their cross product is the zero
// vector, and normalising it gives NaN - which then compares false against
// everything and silently reports "no collision" for the single most common
// arrangement there is, two axis-aligned boxes. The epsilon is added to the
// absolute rotation matrix so those axes fail the length test instead.
constexpr float kParallelEpsilon = 1.0e-5f;

// Face axes are preferred over edge axes by this much before an edge axis is
// allowed to win.
//
// Without it, two boxes resting face to face pick a cross-product axis whenever
// floating point makes it a hair smaller, and the contact normal flips between
// a face normal and an edge direction from frame to frame. The symptom is a
// stack that shivers.
constexpr float kFaceBias = 1.02f;

struct AxisResult {
    float overlap{0.0f};
    int index{-1};       // 0..2 A faces, 3..5 B faces, 6..14 edge pairs
    bool valid{false};
};

// Projection radius of an OBB onto a unit axis.
float projectedRadius(const Obb& box, const glm::vec3& axis) {
    return box.halfExtent.x * std::fabs(glm::dot(box.axes[0], axis))
         + box.halfExtent.y * std::fabs(glm::dot(box.axes[1], axis))
         + box.halfExtent.z * std::fabs(glm::dot(box.axes[2], axis));
}

// Tests one candidate axis and keeps it if it is the smallest overlap so far.
// Returns false the moment an axis separates the boxes, which is the whole
// point of SAT: one separating axis is proof, and the remaining tests are
// wasted work.
bool testAxis(const glm::vec3& axis, const Obb& a, const Obb& b, const glm::vec3& toB,
              int index, float bias, float margin, AxisResult& best) {
    const float lengthSquared = glm::dot(axis, axis);
    // A degenerate axis is not a separating axis, it is no axis at all.
    if (lengthSquared < kParallelEpsilon) return true;

    const glm::vec3 unitAxis = axis / std::sqrt(lengthSquared);
    const float overlap = projectedRadius(a, unitAxis) + projectedRadius(b, unitAxis)
                        - std::fabs(glm::dot(toB, unitAxis));

    // Separated by more than the margin: proven apart, and the remaining
    // axes are wasted work. With a zero margin this is the ordinary SAT
    // early-out; with a positive one it lets a near miss through so the solver
    // can stop a fast body before it crosses the gap.
    if (overlap < -margin) return false;

    if (!best.valid || overlap * bias < best.overlap) {
        best.overlap = overlap * bias;
        best.index = index;
        best.valid = true;
    }
    return true;
}

// The face of `box` most anti-parallel to `normal` - the one facing the other
// box. Returns the axis index and whether it points along +axis or -axis.
void mostAntiParallelFace(const Obb& box, const glm::vec3& normal, int& outAxis, float& outSign) {
    outAxis = 0;
    outSign = 1.0f;
    float smallest = 1.0e30f;
    for (int i = 0; i < 3; ++i) {
        const float d = glm::dot(box.axes[i], normal);
        if (d < smallest) { smallest = d; outAxis = i; outSign = 1.0f; }
        if (-d < smallest) { smallest = -d; outAxis = i; outSign = -1.0f; }
    }
}

// The four corners of one face of a box, wound consistently.
void faceCorners(const Obb& box, int axis, float sign, glm::vec3 out[4]) {
    const int u = (axis + 1) % 3;
    const int v = (axis + 2) % 3;

    const glm::vec3 faceCentre = box.centre + box.axes[axis] * (sign * box.halfExtent[axis]);
    const glm::vec3 du = box.axes[u] * box.halfExtent[u];
    const glm::vec3 dv = box.axes[v] * box.halfExtent[v];

    out[0] = faceCentre - du - dv;
    out[1] = faceCentre + du - dv;
    out[2] = faceCentre + du + dv;
    out[3] = faceCentre - du + dv;
}

// Sutherland-Hodgman: clip `polygon` against the half-space behind a plane.
// Keeps points on the negative side and inserts intersections at crossings.
int clipAgainstPlane(const glm::vec3* input, int count, const glm::vec3& planeNormal,
                     float planeOffset, glm::vec3* output) {
    int produced = 0;
    for (int i = 0; i < count; ++i) {
        const glm::vec3& current = input[i];
        const glm::vec3& next = input[(i + 1) % count];

        const float dCurrent = glm::dot(planeNormal, current) - planeOffset;
        const float dNext = glm::dot(planeNormal, next) - planeOffset;

        if (dCurrent <= 0.0f) {
            if (produced < 8) output[produced++] = current;
        }
        // Straddles the plane: insert the crossing point. The sign test rather
        // than a product, because two values that are both tiny multiply to
        // zero and lose the crossing.
        if ((dCurrent > 0.0f) != (dNext > 0.0f)) {
            const float t = dCurrent / (dCurrent - dNext);
            if (produced < 8) output[produced++] = current + (next - current) * t;
        }
    }
    return produced;
}

} // namespace

Manifold CollideObbObb(const Obb& a, const Obb& b, float speculativeMargin) {
    Manifold manifold;

    const glm::vec3 toB = b.centre - a.centre;

    AxisResult best;

    // 3 faces of A, 3 of B, then the 9 edge-edge cross products. Face axes
    // carry a bias so a near-tie goes to a face, which is the stable answer.
    for (int i = 0; i < 3; ++i) {
        if (!testAxis(a.axes[i], a, b, toB, i, 1.0f, speculativeMargin, best)) return manifold;
    }
    for (int i = 0; i < 3; ++i) {
        if (!testAxis(b.axes[i], a, b, toB, 3 + i, 1.0f, speculativeMargin, best)) return manifold;
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const glm::vec3 axis = glm::cross(a.axes[i], b.axes[j]);
            if (!testAxis(axis, a, b, toB, 6 + i * 3 + j, kFaceBias, speculativeMargin, best)) return manifold;
        }
    }

    if (!best.valid) return manifold;

    manifold.colliding = true;

    // Recover the winning axis and orient it from a toward b.
    glm::vec3 normal;
    if (best.index < 3) {
        normal = a.axes[best.index];
    } else if (best.index < 6) {
        normal = b.axes[best.index - 3];
    } else {
        const int i = (best.index - 6) / 3;
        const int j = (best.index - 6) % 3;
        normal = glm::cross(a.axes[i], b.axes[j]);
        const float length = glm::length(normal);
        if (length < kParallelEpsilon) {
            // Degenerate after all - fall back to the direction between the
            // centres, which is always a usable push even if not minimal.
            normal = glm::length(toB) > kParallelEpsilon ? glm::normalize(toB)
                                                         : glm::vec3(0.0f, 1.0f, 0.0f);
        } else {
            normal /= length;
        }
    }
    if (glm::dot(normal, toB) < 0.0f) normal = -normal;
    manifold.normal = glm::normalize(normal);

    // Apart, but close enough that something moving fast could cross the gap
    // this step. There is no contact area to clip - the faces are not touching -
    // so this is one point carrying the size of the gap as a negative
    // penetration, which is exactly what the solver needs to know.
    if (best.overlap < 0.0f) {
        manifold.speculative = true;
        manifold.pointCount = 1;
        manifold.points[0].position = a.centre + toB * 0.5f;
        manifold.points[0].penetration = best.overlap;
        return manifold;
    }

    // An edge-edge contact is a single point, and clipping faces for it would
    // invent an area that is not touching. The midpoint of the deepest overlap
    // along the normal is close enough for a case that is inherently one point.
    if (best.index >= 6) {
        manifold.pointCount = 1;
        manifold.points[0].position = a.centre + toB * 0.5f;
        manifold.points[0].penetration = best.overlap / kFaceBias;
        return manifold;
    }

    // Face contact. The reference face belongs to whichever box owned the
    // winning axis; the incident face is the other box's face most anti-parallel
    // to the normal.
    const bool referenceIsA = best.index < 3;
    const Obb& reference = referenceIsA ? a : b;
    const Obb& incident = referenceIsA ? b : a;

    // The reference normal must point from the reference box toward the other.
    const glm::vec3 referenceNormal = referenceIsA ? manifold.normal : -manifold.normal;

    int referenceAxis = referenceIsA ? best.index : best.index - 3;
    float referenceSign = glm::dot(reference.axes[referenceAxis], referenceNormal) >= 0.0f ? 1.0f : -1.0f;

    int incidentAxis = 0;
    float incidentSign = 1.0f;
    mostAntiParallelFace(incident, referenceNormal, incidentAxis, incidentSign);

    glm::vec3 incidentFace[4];
    faceCorners(incident, incidentAxis, incidentSign, incidentFace);

    // Clip the incident face against the four side planes of the reference
    // face. Two buffers, ping-ponged, because each clip can add a point.
    glm::vec3 bufferA[8];
    glm::vec3 bufferB[8];
    std::copy(incidentFace, incidentFace + 4, bufferA);
    int count = 4;

    const int u = (referenceAxis + 1) % 3;
    const int v = (referenceAxis + 2) % 3;
    const glm::vec3 faceCentre =
        reference.centre + reference.axes[referenceAxis] * (referenceSign * reference.halfExtent[referenceAxis]);

    for (const int sideAxis : { u, v }) {
        for (const float sideSign : { 1.0f, -1.0f }) {
            const glm::vec3 planeNormal = reference.axes[sideAxis] * sideSign;
            const float planeOffset = glm::dot(planeNormal, faceCentre)
                                    + reference.halfExtent[sideAxis];
            count = clipAgainstPlane(bufferA, count, planeNormal, planeOffset, bufferB);
            std::copy(bufferB, bufferB + count, bufferA);
            if (count == 0) break;
        }
        if (count == 0) break;
    }

    // Keep only the points actually below the reference face. A clipped polygon
    // can include corners that are inside the side planes but not penetrating.
    const float faceOffset = glm::dot(referenceNormal, faceCentre);
    for (int i = 0; i < count && manifold.pointCount < kMaxContactPoints; ++i) {
        const float depth = faceOffset - glm::dot(referenceNormal, bufferA[i]);
        if (depth < 0.0f) continue;

        manifold.points[manifold.pointCount].position = bufferA[i];
        manifold.points[manifold.pointCount].penetration = depth;
        ++manifold.pointCount;
    }

    // Everything clipped away, which happens at glancing contact. Fall back to
    // one point so the caller still gets a usable push rather than a collision
    // with nowhere to apply it.
    if (manifold.pointCount == 0) {
        manifold.pointCount = 1;
        manifold.points[0].position = a.centre + toB * 0.5f;
        manifold.points[0].penetration = best.overlap;
    }

    return manifold;
}

bool CollideSphereObb(const glm::vec3& sphereCentre, float radius, const Obb& box,
                      glm::vec3& outNormal, float& outPenetration, glm::vec3& outPoint,
                      float speculativeMargin) {
    // Into the box's frame, where the problem is a sphere against an AABB.
    const glm::vec3 relative = sphereCentre - box.centre;
    glm::vec3 local(glm::dot(relative, box.axes[0]),
                    glm::dot(relative, box.axes[1]),
                    glm::dot(relative, box.axes[2]));

    const glm::vec3 clamped = glm::clamp(local, -box.halfExtent, box.halfExtent);
    const glm::vec3 toSurface = local - clamped;
    const float distanceSquared = glm::dot(toSurface, toSurface);

    const float reach = radius + speculativeMargin;
    if (distanceSquared > reach * reach) return false;

    if (distanceSquared > kParallelEpsilon) {
        // Outside: the normal is the direction from the closest surface point.
        const float distance = std::sqrt(distanceSquared);
        const glm::vec3 localNormal = toSurface / distance;
        outNormal = box.axes * localNormal;
        outPenetration = radius - distance;
    } else {
        // The centre is inside the box. Push out along whichever face is
        // nearest, which is the smallest remaining half-extent difference.
        const glm::vec3 toFace = box.halfExtent - glm::abs(local);
        int axis = 0;
        if (toFace.y < toFace[axis]) axis = 1;
        if (toFace.z < toFace[axis]) axis = 2;

        glm::vec3 localNormal(0.0f);
        localNormal[axis] = local[axis] < 0.0f ? -1.0f : 1.0f;
        outNormal = box.axes * localNormal;
        outPenetration = radius + toFace[axis];
    }

    // The normal points from the BOX toward the sphere, matching the a-to-b
    // convention when the box is a.
    outPoint = box.centre + box.axes * clamped;
    return true;
}

glm::vec3 ClosestPointOnSegment(const glm::vec3& a, const glm::vec3& b, const glm::vec3& p) {
    const glm::vec3 along = b - a;
    const float lengthSquared = glm::dot(along, along);
    // A degenerate segment is a point, which is what a sphere is.
    if (lengthSquared < kParallelEpsilon) return a;

    const float t = std::clamp(glm::dot(p - a, along) / lengthSquared, 0.0f, 1.0f);
    return a + along * t;
}

void ClosestPointsBetweenSegments(const glm::vec3& a0, const glm::vec3& a1,
                                  const glm::vec3& b0, const glm::vec3& b1,
                                  glm::vec3& outA, glm::vec3& outB) {
    const glm::vec3 dirA = a1 - a0;
    const glm::vec3 dirB = b1 - b0;
    const glm::vec3 between = a0 - b0;

    const float lengthA = glm::dot(dirA, dirA);
    const float lengthB = glm::dot(dirB, dirB);
    const float projectB = glm::dot(dirB, between);

    // Both degenerate: two spheres.
    if (lengthA < kParallelEpsilon && lengthB < kParallelEpsilon) {
        outA = a0;
        outB = b0;
        return;
    }

    float s = 0.0f;
    float t = 0.0f;

    if (lengthA < kParallelEpsilon) {
        // A is a point; only B has a parameter to solve for.
        t = std::clamp(projectB / lengthB, 0.0f, 1.0f);
    } else {
        const float projectA = glm::dot(dirA, between);
        if (lengthB < kParallelEpsilon) {
            s = std::clamp(-projectA / lengthA, 0.0f, 1.0f);
        } else {
            const float dot = glm::dot(dirA, dirB);
            const float denominator = lengthA * lengthB - dot * dot;

            // Zero when the segments are parallel, and dividing by it is the
            // NaN that makes two parallel capsules report no contact at all.
            // Any point is as near as any other then, so one end will do.
            s = (denominator > kParallelEpsilon)
                    ? std::clamp((dot * projectB - projectA * lengthB) / denominator, 0.0f, 1.0f)
                    : 0.0f;

            t = (dot * s + projectB) / lengthB;

            // Clamping t can move the nearest point off A's own segment, so s
            // is recomputed against the clamped t and clamped again. Without
            // this second pass a capsule resting past the end of another sits
            // slightly inside it.
            if (t < 0.0f) {
                t = 0.0f;
                s = std::clamp(-projectA / lengthA, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = std::clamp((dot - projectA) / lengthA, 0.0f, 1.0f);
            }
        }
    }

    outA = a0 + dirA * s;
    outB = b0 + dirB * t;
}

bool CollideCapsuleCapsule(const glm::vec3& a0, const glm::vec3& a1, float radiusA,
                           const glm::vec3& b0, const glm::vec3& b1, float radiusB,
                           glm::vec3& outNormal, float& outPenetration, glm::vec3& outPoint,
                           float speculativeMargin) {
    glm::vec3 nearestA(0.0f);
    glm::vec3 nearestB(0.0f);
    ClosestPointsBetweenSegments(a0, a1, b0, b1, nearestA, nearestB);

    const glm::vec3 delta = nearestB - nearestA;
    const float distanceSquared = glm::dot(delta, delta);
    const float sum = radiusA + radiusB;
    const float reach = sum + speculativeMargin;
    if (distanceSquared > reach * reach) return false;

    if (distanceSquared < kParallelEpsilon) {
        // The two axes intersect. Any direction is as good as another; up keeps
        // them from being launched sideways at enormous speed.
        outNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        outPenetration = sum;
        outPoint = nearestA;
        return true;
    }

    const float distance = std::sqrt(distanceSquared);
    outNormal = delta / distance;
    // Negative when the pair is merely within the speculative margin, which is
    // the same convention every other test here uses.
    outPenetration = sum - distance;
    // On the line between the two axes, between the two surfaces.
    outPoint = nearestA + outNormal * (radiusA - outPenetration * 0.5f);
    return true;
}

bool CollideCapsuleObb(const glm::vec3& a0, const glm::vec3& a1, float radius, const Obb& box,
                       glm::vec3& outNormal, float& outPenetration, glm::vec3& outPoint,
                       float speculativeMargin) {
    // Into the box's frame, where the problem is a segment against an AABB.
    const auto toLocal = [&](const glm::vec3& world) {
        const glm::vec3 relative = world - box.centre;
        return glm::vec3(glm::dot(relative, box.axes[0]),
                         glm::dot(relative, box.axes[1]),
                         glm::dot(relative, box.axes[2]));
    };

    const glm::vec3 localA = toLocal(a0);
    const glm::vec3 localB = toLocal(a1);

    // Alternating projection: clamp a point onto the box, find the nearest
    // point on the segment to that, clamp again. Each step can only reduce the
    // distance between the two convex sets, so it converges, and for a box
    // against a segment it converges in a handful of steps. Eight is well past
    // where the answer stops changing.
    //
    // The alternative - solving for the segment parameter directly - is a case
    // analysis over the box's six faces, twelve edges and eight corners, which
    // is where a shape test of this kind usually goes wrong.
    glm::vec3 onBox = glm::clamp((localA + localB) * 0.5f, -box.halfExtent, box.halfExtent);
    glm::vec3 onSegment = localA;
    for (int i = 0; i < 8; ++i) {
        onSegment = ClosestPointOnSegment(localA, localB, onBox);
        const glm::vec3 next = glm::clamp(onSegment, -box.halfExtent, box.halfExtent);
        if (next == onBox) break;
        onBox = next;
    }

    // Back to world, and then it is a sphere against the box - one
    // implementation rather than two that can disagree about which face a
    // corner belongs to.
    const glm::vec3 worldSegment = box.centre + box.axes * onSegment;
    return CollideSphereObb(worldSegment, radius, box, outNormal, outPenetration, outPoint,
                            speculativeMargin);
}

} // namespace Supersonic::CollisionSAT
