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
              int index, float bias, AxisResult& best) {
    const float lengthSquared = glm::dot(axis, axis);
    // A degenerate axis is not a separating axis, it is no axis at all.
    if (lengthSquared < kParallelEpsilon) return true;

    const glm::vec3 unitAxis = axis / std::sqrt(lengthSquared);
    const float overlap = projectedRadius(a, unitAxis) + projectedRadius(b, unitAxis)
                        - std::fabs(glm::dot(toB, unitAxis));

    if (overlap <= 0.0f) return false;   // separated: done

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

Manifold CollideObbObb(const Obb& a, const Obb& b) {
    Manifold manifold;

    const glm::vec3 toB = b.centre - a.centre;

    AxisResult best;

    // 3 faces of A, 3 of B, then the 9 edge-edge cross products. Face axes
    // carry a bias so a near-tie goes to a face, which is the stable answer.
    for (int i = 0; i < 3; ++i) {
        if (!testAxis(a.axes[i], a, b, toB, i, 1.0f, best)) return manifold;
    }
    for (int i = 0; i < 3; ++i) {
        if (!testAxis(b.axes[i], a, b, toB, 3 + i, 1.0f, best)) return manifold;
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const glm::vec3 axis = glm::cross(a.axes[i], b.axes[j]);
            if (!testAxis(axis, a, b, toB, 6 + i * 3 + j, kFaceBias, best)) return manifold;
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
                      glm::vec3& outNormal, float& outPenetration, glm::vec3& outPoint) {
    // Into the box's frame, where the problem is a sphere against an AABB.
    const glm::vec3 relative = sphereCentre - box.centre;
    glm::vec3 local(glm::dot(relative, box.axes[0]),
                    glm::dot(relative, box.axes[1]),
                    glm::dot(relative, box.axes[2]));

    const glm::vec3 clamped = glm::clamp(local, -box.halfExtent, box.halfExtent);
    const glm::vec3 toSurface = local - clamped;
    const float distanceSquared = glm::dot(toSurface, toSurface);

    if (distanceSquared > radius * radius) return false;

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

} // namespace Supersonic::CollisionSAT
