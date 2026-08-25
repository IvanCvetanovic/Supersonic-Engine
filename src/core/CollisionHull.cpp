#include "core/CollisionHull.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Supersonic {
namespace CollisionHull {

namespace {

constexpr float kEpsilon = 1.0e-6f;

// An edge axis has to beat the best face axis by this much before it is taken.
//
// The same reason and the same number as the box path: two axes within
// floating-point noise of each other flip between steps, the contact normal
// flips with them, and a stack shivers.
constexpr float kFaceBias = 1.02f;

// Enough for a clipped face. A hull face can have many vertices and each clip
// plane can add one; sixty-four is far past anything a capped hull produces.
constexpr int kClipCapacity = 64;

glm::vec3 worldVertex(const Instance& instance, uint32_t index) {
    return instance.origin + instance.basis * instance.hull->vertices()[index];
}

glm::vec3 worldFaceNormal(const Instance& instance, const ConvexHull::Face& face) {
    const glm::vec3 normal = instance.normalBasis * face.normal;
    const float length = glm::length(normal);
    return length > kEpsilon ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
}

// The farthest world point of the hull along a world direction.
//
// The direction is taken into the hull's own space by the TRANSPOSE of the
// basis, because maximising dot(d, B*v) over v is maximising dot(B^T*d, v) -
// which is the support of the untransformed hull along a different direction.
glm::vec3 worldSupport(const Instance& instance, const glm::vec3& direction) {
    const glm::vec3 local = glm::transpose(instance.basis) * direction;
    return instance.origin + instance.basis * instance.hull->Support(local);
}

struct AxisResult {
    float overlap{std::numeric_limits<float>::max()};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    int owner{-1};          // 0 = a's face, 1 = b's face, 2 = an edge pair
    uint32_t face{0};
    uint32_t edgeA{0};
    uint32_t edgeB{0};
};

// How far the two hulls overlap along `axis`, or a negative number when they
// are apart. False when the axis separates them by more than the margin, which
// ends the whole test.
bool measureAxis(const glm::vec3& axis, const Instance& a, const Instance& b,
                 const glm::vec3& toB, float bias, float margin, AxisResult& best,
                 int owner, uint32_t face, uint32_t edgeA, uint32_t edgeB) {
    const float lengthSquared = glm::dot(axis, axis);
    // Two parallel edges cross to nothing. Normalising that is a NaN, and every
    // comparison against a NaN is false - so the axis reports no separation and
    // two shapes sitting inside one another are declared apart.
    if (lengthSquared < kEpsilon) return true;

    glm::vec3 normal = axis / std::sqrt(lengthSquared);

    // Pointed from a toward b, so every candidate is comparable and the winner
    // needs no fixing up afterwards.
    if (glm::dot(normal, toB) < 0.0f) normal = -normal;

    const float maxA = glm::dot(normal, worldSupport(a, normal));
    const float minB = glm::dot(normal, worldSupport(b, -normal));

    // Only the a-then-b side matters once the normal is oriented: the other
    // overlap is the same shapes measured backwards.
    const float overlap = maxA - minB;
    if (overlap < -margin) return false;

    if (overlap * bias < best.overlap) {
        best.overlap = overlap * bias;
        best.normal = normal;
        best.owner = owner;
        best.face = face;
        best.edgeA = edgeA;
        best.edgeB = edgeB;
    }
    return true;
}

int clipAgainstPlane(const glm::vec3* input, int count, const glm::vec3& planeNormal,
                     float planeOffset, glm::vec3* output) {
    int produced = 0;
    for (int i = 0; i < count; ++i) {
        const glm::vec3& current = input[i];
        const glm::vec3& next = input[(i + 1) % count];

        const float dCurrent = glm::dot(planeNormal, current) - planeOffset;
        const float dNext = glm::dot(planeNormal, next) - planeOffset;

        if (dCurrent <= 0.0f && produced < kClipCapacity) output[produced++] = current;

        // The SIGNS, not the product: two distances small enough that their
        // product underflows to zero lose the crossing, and the clipped face
        // comes out missing a corner.
        if ((dCurrent > 0.0f) != (dNext > 0.0f) && produced < kClipCapacity) {
            const float t = dCurrent / (dCurrent - dNext);
            output[produced++] = current + (next - current) * t;
        }
    }
    return produced;
}

// The face most anti-parallel to a direction: what the other hull presents to
// the reference face.
uint32_t mostAntiParallelFace(const Instance& instance, const glm::vec3& direction) {
    uint32_t best = 0;
    float worst = std::numeric_limits<float>::max();
    for (uint32_t i = 0; i < instance.hull->faces().size(); ++i) {
        const float along = glm::dot(worldFaceNormal(instance, instance.hull->faces()[i]), direction);
        if (along < worst) {
            worst = along;
            best = i;
        }
    }
    return best;
}

void gatherFace(const Instance& instance, uint32_t faceIndex, glm::vec3* out, int& count) {
    const ConvexHull::Face& face = instance.hull->faces()[faceIndex];
    count = 0;
    for (uint32_t i = 0; i < face.count && count < kClipCapacity; ++i) {
        out[count++] = worldVertex(instance, instance.hull->indices()[face.first + i]);
    }
}

// The closest point on one convex polygon to `point`.
glm::vec3 closestPointOnFace(const Instance& instance, uint32_t faceIndex,
                             const glm::vec3& point) {
    glm::vec3 corners[kClipCapacity];
    int count = 0;
    gatherFace(instance, faceIndex, corners, count);
    if (count == 0) return point;

    const glm::vec3 normal = worldFaceNormal(instance, instance.hull->faces()[faceIndex]);
    const glm::vec3 onPlane = point - normal * (glm::dot(normal, point - corners[0]));

    // Inside the polygon when it is on the inner side of every edge, which for
    // a convex polygon is the whole test.
    bool inside = true;
    for (int i = 0; i < count; ++i) {
        const glm::vec3& current = corners[i];
        const glm::vec3& next = corners[(i + 1) % count];
        const glm::vec3 outward = glm::cross(next - current, normal);
        if (glm::dot(outward, onPlane - current) > 0.0f) {
            inside = false;
            break;
        }
    }
    if (inside) return onPlane;

    glm::vec3 best = corners[0];
    float nearest = std::numeric_limits<float>::max();
    for (int i = 0; i < count; ++i) {
        const glm::vec3 candidate =
            CollisionSAT::ClosestPointOnSegment(corners[i], corners[(i + 1) % count], point);
        const float distance = glm::dot(candidate - point, candidate - point);
        if (distance < nearest) {
            nearest = distance;
            best = candidate;
        }
    }
    return best;
}

} // namespace

bool MakeInstance(const ConvexHull& hull, const glm::vec3& origin, const glm::mat3& basis,
                  Instance& out) {
    if (!hull.valid()) return false;
    // A basis that cannot be inverted is a collider flattened to nothing on some
    // axis, and a hull with no thickness has no outside to be on.
    if (std::fabs(glm::determinant(basis)) < 1.0e-12f) return false;

    out.hull = &hull;
    out.origin = origin;
    out.basis = basis;
    out.normalBasis = glm::transpose(glm::inverse(basis));
    return true;
}

const ConvexHull& UnitCube() {
    static const ConvexHull cube = [] {
        std::vector<glm::vec3> corners;
        for (int i = 0; i < 8; ++i) {
            corners.push_back(glm::vec3((i & 1) ? 0.5f : -0.5f,
                                        (i & 2) ? 0.5f : -0.5f,
                                        (i & 4) ? 0.5f : -0.5f));
        }
        ConvexHull built;
        built.Build(corners);
        return built;
    }();
    return cube;
}

Instance InstanceFromObb(const CollisionSAT::Obb& box) {
    // The unit cube's corners are at plus or minus a half, so the basis carries
    // the FULL extent on each axis.
    glm::mat3 basis;
    for (int axis = 0; axis < 3; ++axis) {
        basis[axis] = box.axes[axis] * (box.halfExtent[axis] * 2.0f);
    }

    Instance instance;
    if (!MakeInstance(UnitCube(), box.centre, basis, instance)) {
        instance.hull = nullptr;
    }
    return instance;
}

CollisionSAT::Manifold CollideHullHull(const Instance& a, const Instance& b,
                                       float speculativeMargin) {
    CollisionSAT::Manifold manifold;
    if (!a.hull || !b.hull || !a.hull->valid() || !b.hull->valid()) return manifold;

    const float margin = std::max(speculativeMargin, 0.0f);

    // From a's middle to b's, which is what orients every candidate axis. The
    // hull's own bounds centre rather than the origin, because a hull's origin
    // is wherever the mesh had it and may be nowhere near its middle.
    const glm::vec3 centreA =
        a.origin + a.basis * ((a.hull->boundsMin() + a.hull->boundsMax()) * 0.5f);
    const glm::vec3 centreB =
        b.origin + b.basis * ((b.hull->boundsMin() + b.hull->boundsMax()) * 0.5f);
    const glm::vec3 toB = centreB - centreA;

    AxisResult best;

    for (uint32_t i = 0; i < a.hull->faces().size(); ++i) {
        if (!measureAxis(worldFaceNormal(a, a.hull->faces()[i]), a, b, toB, 1.0f, margin, best,
                         0, i, 0, 0)) {
            return manifold;
        }
    }
    for (uint32_t i = 0; i < b.hull->faces().size(); ++i) {
        if (!measureAxis(worldFaceNormal(b, b.hull->faces()[i]), a, b, toB, 1.0f, margin, best,
                         1, i, 0, 0)) {
            return manifold;
        }
    }

    // Edge against edge, which the face axes cannot find: a plank resting on the
    // corner of another plank passes through it if this loop is skipped.
    for (uint32_t i = 0; i < a.hull->edges().size(); ++i) {
        const ConvexHull::Edge& edgeA = a.hull->edges()[i];
        const glm::vec3 directionA = worldVertex(a, edgeA.b) - worldVertex(a, edgeA.a);

        for (uint32_t j = 0; j < b.hull->edges().size(); ++j) {
            const ConvexHull::Edge& edgeB = b.hull->edges()[j];
            const glm::vec3 directionB = worldVertex(b, edgeB.b) - worldVertex(b, edgeB.a);

            if (!measureAxis(glm::cross(directionA, directionB), a, b, toB, kFaceBias, margin,
                             best, 2, 0, i, j)) {
                return manifold;
            }
        }
    }

    if (best.owner < 0) return manifold;

    manifold.colliding = true;
    manifold.normal = best.normal;
    manifold.speculative = best.overlap < 0.0f;

    // ---- Edge against edge: one point, where the two lines pass ------------
    if (best.owner == 2) {
        const ConvexHull::Edge& edgeA = a.hull->edges()[best.edgeA];
        const ConvexHull::Edge& edgeB = b.hull->edges()[best.edgeB];

        glm::vec3 onA(0.0f);
        glm::vec3 onB(0.0f);
        CollisionSAT::ClosestPointsBetweenSegments(
            worldVertex(a, edgeA.a), worldVertex(a, edgeA.b),
            worldVertex(b, edgeB.a), worldVertex(b, edgeB.b), onA, onB);

        manifold.pointCount = 1;
        manifold.points[0].position = (onA + onB) * 0.5f;
        manifold.points[0].penetration = best.overlap / kFaceBias;
        return manifold;
    }

    // ---- Face against face: clip, and keep what is behind ------------------
    const bool referenceIsA = best.owner == 0;
    const Instance& reference = referenceIsA ? a : b;
    const Instance& incident = referenceIsA ? b : a;

    // The reference normal points from the reference hull toward the other one.
    const glm::vec3 referenceNormal = referenceIsA ? manifold.normal : -manifold.normal;

    // The reference face is chosen HERE, from the normal, rather than taken
    // from whichever face won the axis search.
    //
    // A hull has two faces for every axis - the top and the bottom of a slab
    // both answer to Y - and measureAxis flips a normal to point from a toward
    // b, so the face that won may be the one facing the other way. Trusting its
    // index put the reference plane at the BOTTOM of the floor: every clipped
    // point measured a metre behind it, all of them were dropped, and a crate
    // fell through a slab the SAT had correctly reported it was standing on.
    //
    // Most-aligned is the mirror of how the incident face is picked, and cannot
    // pick the wrong side.
    glm::vec3 referenceFace[kClipCapacity];
    int referenceCount = 0;
    gatherFace(reference, mostAntiParallelFace(reference, -referenceNormal), referenceFace,
               referenceCount);
    if (referenceCount < 3) return manifold;

    const uint32_t incidentIndex = mostAntiParallelFace(incident, referenceNormal);

    glm::vec3 bufferA[kClipCapacity];
    glm::vec3 bufferB[kClipCapacity];
    int count = 0;
    gatherFace(incident, incidentIndex, bufferA, count);
    if (count < 3) return manifold;

    // Against each SIDE of the reference face. For a polygon wound so its normal
    // faces out, the outward side normal of an edge is the edge crossed with the
    // face normal.
    glm::vec3* input = bufferA;
    glm::vec3* output = bufferB;
    for (int i = 0; i < referenceCount && count > 0; ++i) {
        const glm::vec3& current = referenceFace[i];
        const glm::vec3& next = referenceFace[(i + 1) % referenceCount];
        const glm::vec3 outward = glm::cross(next - current, referenceNormal);

        const float lengthSquared = glm::dot(outward, outward);
        if (lengthSquared < kEpsilon) continue;

        const glm::vec3 planeNormal = outward / std::sqrt(lengthSquared);
        count = clipAgainstPlane(input, count, planeNormal, glm::dot(planeNormal, current), output);
        std::swap(input, output);
    }
    if (count == 0) return manifold;

    // Only what is actually behind the reference plane, or within the margin of
    // it. The rest of the clipped polygon is off the side of the contact.
    const float referenceOffset = glm::dot(referenceNormal, referenceFace[0]);

    struct Candidate {
        glm::vec3 position;
        float penetration;
    };
    Candidate candidates[kClipCapacity];
    int candidateCount = 0;

    for (int i = 0; i < count; ++i) {
        const float depth = referenceOffset - glm::dot(referenceNormal, input[i]);
        if (depth < -margin) continue;
        candidates[candidateCount].position = input[i];
        candidates[candidateCount].penetration = depth;
        ++candidateCount;
    }
    if (candidateCount == 0) return manifold;

    // The deepest four. A clipped face can produce more, and the solver takes
    // four - so which four it gets should be the ones holding the shapes apart
    // rather than the ones the loop reached first.
    std::sort(candidates, candidates + candidateCount,
              [](const Candidate& left, const Candidate& right) {
                  return left.penetration > right.penetration;
              });

    manifold.pointCount = std::min(candidateCount, CollisionSAT::kMaxContactPoints);
    for (int i = 0; i < manifold.pointCount; ++i) {
        manifold.points[i].position = candidates[i].position;
        manifold.points[i].penetration = candidates[i].penetration;
    }
    manifold.speculative = manifold.points[0].penetration < 0.0f;
    return manifold;
}

glm::vec3 ClosestPointOnHull(const Instance& hull, const glm::vec3& point, bool& outInside) {
    outInside = false;
    if (!hull.hull || !hull.hull->valid()) return point;

    // Inside when it is behind every face. The face it is LEAST behind is the
    // one it is nearest to leaving through, which is the way out.
    float leastDepth = std::numeric_limits<float>::max();
    uint32_t exitFace = 0;
    bool inside = true;

    for (uint32_t i = 0; i < hull.hull->faces().size(); ++i) {
        const glm::vec3 normal = worldFaceNormal(hull, hull.hull->faces()[i]);
        glm::vec3 corners[kClipCapacity];
        int count = 0;
        gatherFace(hull, i, corners, count);
        if (count == 0) continue;

        const float signedDistance = glm::dot(normal, point - corners[0]);
        if (signedDistance > 0.0f) {
            inside = false;
            break;
        }
        if (-signedDistance < leastDepth) {
            leastDepth = -signedDistance;
            exitFace = i;
        }
    }

    if (inside) {
        outInside = true;
        return closestPointOnFace(hull, exitFace, point);
    }

    glm::vec3 best = point;
    float nearest = std::numeric_limits<float>::max();
    for (uint32_t i = 0; i < hull.hull->faces().size(); ++i) {
        const glm::vec3 candidate = closestPointOnFace(hull, i, point);
        const float distance = glm::dot(candidate - point, candidate - point);
        if (distance < nearest) {
            nearest = distance;
            best = candidate;
        }
    }
    return best;
}

CollisionSAT::Manifold CollideCapsuleHull(const glm::vec3& a0, const glm::vec3& a1, float radius,
                                          const Instance& hull, float speculativeMargin) {
    CollisionSAT::Manifold manifold;
    if (!hull.hull || !hull.hull->valid()) return manifold;

    const float reach = radius + std::max(speculativeMargin, 0.0f);

    // The two sets projected onto each other in turn: clamp a point onto the
    // hull, find the nearest point on the segment to that, clamp again. Each
    // step can only reduce the distance between two convex sets, so it
    // converges - the same argument, and the same eight passes, as the capsule
    // against a box.
    glm::vec3 onSegment = (a0 + a1) * 0.5f;
    glm::vec3 onHull = onSegment;
    bool inside = false;
    for (int pass = 0; pass < 8; ++pass) {
        onHull = ClosestPointOnHull(hull, onSegment, inside);
        onSegment = CollisionSAT::ClosestPointOnSegment(a0, a1, onHull);
    }

    glm::vec3 offset = onSegment - onHull;
    float distance = glm::length(offset);

    glm::vec3 normal(0.0f, 1.0f, 0.0f);
    float penetration = 0.0f;

    if (inside) {
        // The segment's nearest point is within the hull, so the way out is the
        // face it is nearest to leaving through - the offset itself is either
        // tiny or points the wrong way.
        bool ignored = false;
        const glm::vec3 exit = ClosestPointOnHull(hull, onSegment, ignored);
        const glm::vec3 outward = exit - onSegment;
        const float outwardLength = glm::length(outward);
        normal = outwardLength > kEpsilon ? outward / outwardLength : glm::vec3(0.0f, 1.0f, 0.0f);
        penetration = radius + outwardLength;
        onHull = exit;
    } else {
        if (distance > reach) return manifold;
        normal = distance > kEpsilon ? offset / distance : glm::vec3(0.0f, 1.0f, 0.0f);
        penetration = radius - distance;
    }

    manifold.colliding = true;
    manifold.normal = normal;
    manifold.speculative = penetration < 0.0f;
    manifold.pointCount = 1;
    manifold.points[0].position = onHull;
    manifold.points[0].penetration = penetration;

    // A capsule lying ALONG a face touches it in a line, and one contact under
    // its middle leaves it free to rock end over end about that point. So when
    // the axis is across the normal rather than into it, both ends are tested
    // as well - the same rule, and the same sixty degrees, as the box path.
    const glm::vec3 axis = a1 - a0;
    const float axisLength = glm::length(axis);
    if (axisLength < kEpsilon) return manifold;

    if (std::fabs(glm::dot(axis / axisLength, normal)) > 0.5f) return manifold;

    manifold.pointCount = 0;
    for (const glm::vec3& end : {a0, a1}) {
        bool endInside = false;
        const glm::vec3 surface = ClosestPointOnHull(hull, end, endInside);
        const glm::vec3 toEnd = end - surface;
        const float endDistance = glm::length(toEnd);

        const float endPenetration = endInside ? radius + endDistance : radius - endDistance;
        if (!endInside && endDistance > reach) continue;

        // Both ends have to agree with the normal the middle found, or a
        // capsule wedged into a corner is pushed along the average of two faces
        // - into neither of them, and out of the corner sideways.
        if (!endInside && endDistance > kEpsilon &&
            glm::dot(toEnd / endDistance, normal) < 0.5f) {
            continue;
        }

        if (manifold.pointCount < CollisionSAT::kMaxContactPoints) {
            manifold.points[manifold.pointCount].position = surface;
            manifold.points[manifold.pointCount].penetration = endPenetration;
            ++manifold.pointCount;
        }
    }

    // Neither end held up, so the middle is all there is.
    if (manifold.pointCount == 0) {
        manifold.pointCount = 1;
        manifold.points[0].position = onHull;
        manifold.points[0].penetration = penetration;
    }

    manifold.speculative = manifold.MaxPenetration() <= 0.0f && penetration < 0.0f;
    return manifold;
}

} // namespace CollisionHull
} // namespace Supersonic
