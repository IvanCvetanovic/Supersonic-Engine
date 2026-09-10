// Tests for oriented box collision.
//
// Box against box was done as WORLD AXIS-ALIGNED boxes, with the normal snapped
// to whichever of six world axes had the least overlap. For an axis-aligned
// crate that is exact; for anything rotated it is not an approximation of the
// box, it is a different box.
//
// The case that motivated this is the last test here: a 30-degree ramp. Its
// AABB half-height is 2.933 against a true mid-surface half-height of 0.577, so
// a ball "rested" about 2.4 units above the visible surface, on a contact
// normal of exactly (0, 1, 0) - which is why nothing ever rolled down a slope.

#include "TestHarness.hpp"
#include "core/CollisionSAT.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using namespace Supersonic;
using namespace Supersonic::CollisionSAT;

namespace {

Obb makeBox(const glm::vec3& centre, const glm::vec3& halfExtent,
            float radiansAboutZ = 0.0f) {
    Obb box;
    box.centre = centre;
    box.halfExtent = halfExtent;
    if (radiansAboutZ != 0.0f) {
        box.axes = glm::mat3(glm::rotate(glm::mat4(1.0f), radiansAboutZ, glm::vec3(0, 0, 1)));
    }
    return box;
}

} // namespace

static void testSeparatedBoxesDoNotCollide() {
    const auto a = makeBox(glm::vec3(0.0f), glm::vec3(0.5f));
    const auto b = makeBox(glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.5f));

    const auto manifold = CollideObbObb(a, b);
    CHECK_MSG(!manifold.colliding, "boxes five units apart must not collide");
    CHECK_EQ(manifold.pointCount, 0);
}

static void testAxisAlignedOverlapMatchesTheOldAabbAnswer() {
    // A strict generalisation: with identity orientations this must agree with
    // the AABB test it replaces, or every existing scene changes behaviour.
    const auto a = makeBox(glm::vec3(0.0f), glm::vec3(0.5f));
    const auto b = makeBox(glm::vec3(0.0f, 0.8f, 0.0f), glm::vec3(0.5f));

    const auto manifold = CollideObbObb(a, b);
    CHECK(manifold.colliding);

    // Overlap on Y is 1.0 - 0.8 = 0.2, and Y is the least-overlap axis.
    CHECK_NEAR(manifold.MaxPenetration(), 0.2f);
    CHECK_MSG(::test::nearly(manifold.normal.y, 1.0f, 1e-3f),
              "b is above a, so the normal must point +Y");
    CHECK_NEAR(manifold.normal.x, 0.0f);
    CHECK_NEAR(manifold.normal.z, 0.0f);
}

static void testFaceContactProducesAManifoldNotAPoint() {
    // One point is enough to stop two boxes intersecting and not enough to stop
    // them rocking: a crate resting flat is held at a single point inside its
    // footprint, so any disturbance rotates it about that point.
    const auto ground = makeBox(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(4.0f, 0.5f, 4.0f));
    const auto crate = makeBox(glm::vec3(0.0f, 0.45f, 0.0f), glm::vec3(0.5f));

    const auto manifold = CollideObbObb(ground, crate);
    CHECK(manifold.colliding);
    CHECK_MSG(manifold.pointCount > 1,
              "a flat face-to-face rest must produce more than one contact point");
    CHECK_MSG(manifold.pointCount <= kMaxContactPoints, "and no more than four");

    // Every point should sit near the contact plane, y = 0.
    for (int i = 0; i < manifold.pointCount; ++i) {
        CHECK_MSG(std::fabs(manifold.points[i].position.y) < 0.15f,
                  "contact points belong on the touching face");
    }
}

static void testNormalAlwaysPointsFromAtowardB() {
    // The solver pushes b along +normal. Getting this backwards resolves a
    // collision by pulling the bodies together.
    const auto a = makeBox(glm::vec3(0.0f), glm::vec3(0.5f));

    const auto above = CollideObbObb(a, makeBox(glm::vec3(0.0f, 0.8f, 0.0f), glm::vec3(0.5f)));
    CHECK(above.normal.y > 0.9f);

    const auto below = CollideObbObb(a, makeBox(glm::vec3(0.0f, -0.8f, 0.0f), glm::vec3(0.5f)));
    CHECK_MSG(below.normal.y < -0.9f, "a box underneath must be pushed down, not up");

    const auto right = CollideObbObb(a, makeBox(glm::vec3(0.8f, 0.0f, 0.0f), glm::vec3(0.5f)));
    CHECK(right.normal.x > 0.9f);
}

static void testAGapInsideTheMarginIsReportedAsItIs() {
    // A crate one centimetre above a slab, inside a margin of five. Apart, an
    // edge axis of two axis-aligned boxes points exactly where a face axis does
    // - x crossed with z is y - and the face bias used to MULTIPLY the overlap:
    // a negative one came out more negative and the edge took the tie. The gap
    // was reported two percent wide, from an axis that was never the face's.
    const auto ground = makeBox(glm::vec3(0.0f), glm::vec3(2.0f, 0.25f, 2.0f));
    const auto crate = makeBox(glm::vec3(0.0f, 0.66f, 0.0f), glm::vec3(0.4f));

    const auto manifold = CollideObbObb(ground, crate, 0.05f);
    CHECK(manifold.colliding);
    CHECK_MSG(manifold.speculative, "a centimetre apart is apart");
    // The point itself: MaxPenetration() floors at zero, so it never says "apart".
    CHECK_MSG(manifold.pointCount == 1 && ::test::nearly(manifold.points[0].penetration, -0.01f, 1e-5f),
              "the gap is a centimetre, not " + std::to_string(-manifold.points[0].penetration));
    CHECK_MSG(::test::nearly(manifold.normal.y, 1.0f, 1e-5f), "and it is straight up");
}

static void testRotationChangesTheAnswer() {
    // The whole point. A box rotated 45 degrees about Z reaches sqrt(2)/2 along
    // its diagonal - about 0.707 - where its unrotated extent is 0.5. Two boxes
    // 1.2 apart therefore touch when rotated and do not when they are not.
    const auto still = makeBox(glm::vec3(0.0f), glm::vec3(0.5f));
    const auto farBox = makeBox(glm::vec3(1.2f, 0.0f, 0.0f), glm::vec3(0.5f));

    CHECK_MSG(!CollideObbObb(still, farBox).colliding,
              "two unrotated unit boxes 1.2 apart do not touch");

    const auto turned = makeBox(glm::vec3(1.2f, 0.0f, 0.0f), glm::vec3(0.5f),
                                glm::radians(45.0f));
    CHECK_MSG(CollideObbObb(still, turned).colliding,
              "rotating one of them by 45 degrees makes them touch");
}

static void testAnAabbTestWouldHaveBeenWrong() {
    // The converse, and the one that matters for gameplay: two boxes whose
    // AABBs overlap but which do not actually touch. The old code reported a
    // collision here and pushed them apart.
    const auto a = makeBox(glm::vec3(0.0f), glm::vec3(0.5f, 0.5f, 0.5f), glm::radians(45.0f));
    const auto b = makeBox(glm::vec3(1.3f, 1.3f, 0.0f), glm::vec3(0.5f), glm::radians(45.0f));

    // Both AABBs span roughly +/-0.707 around their centres, so they overlap on
    // every world axis; the boxes themselves are corner to corner and apart.
    CHECK_MSG(!CollideObbObb(a, b).colliding,
              "two diagonal boxes whose AABBs overlap must not report a collision");
}

static void testSphereAgainstARotatedBox() {
    const auto box = makeBox(glm::vec3(0.0f), glm::vec3(2.0f, 0.25f, 2.0f),
                             glm::radians(45.0f));

    glm::vec3 normal(0.0f);
    float penetration = 0.0f;
    glm::vec3 point(0.0f);

    // Straight above the centre. The box is a thin slab turned 45 degrees, so
    // its surface directly above the origin is only 0.25 * cos(45) + 2 * sin(45)
    // away along the box's own axes - a sphere at y = 3 misses it entirely.
    CHECK_MSG(!CollideSphereObb(glm::vec3(0.0f, 3.0f, 0.0f), 0.4f, box,
                                normal, penetration, point),
              "a sphere well clear of a rotated slab must not collide");

    // Close enough to touch. Worked out rather than guessed: at world height h
    // the centre sits at local y = h * cos(45) = 0.7071h, the slab's own face is
    // at 0.25, so contact needs 0.7071h - 0.25 <= 0.4, i.e. h <= 0.919. The
    // first version of this test used 1.3 and failed, correctly - at that height
    // the gap is 0.669 against a radius of 0.4.
    CHECK(CollideSphereObb(glm::vec3(0.0f, 0.85f, 0.0f), 0.4f, box, normal, penetration, point));
    CHECK_MSG(penetration > 0.0f, "a reported collision must have positive penetration");
    CHECK_MSG(::test::nearly(glm::length(normal), 1.0f, 1e-3f),
              "the normal must be unit length");
}

static void testSphereCentreInsideTheBoxStillPushesOut() {
    // Degenerate but reachable: a fast body can end a step inside another. With
    // no special case the normal is a zero vector normalised, which is NaN, and
    // the body is never pushed out again.
    const auto box = makeBox(glm::vec3(0.0f), glm::vec3(1.0f, 0.25f, 1.0f));

    glm::vec3 normal(0.0f);
    float penetration = 0.0f;
    glm::vec3 point(0.0f);

    CHECK(CollideSphereObb(glm::vec3(0.0f, 0.1f, 0.0f), 0.3f, box, normal, penetration, point));
    CHECK_MSG(::test::nearly(glm::length(normal), 1.0f, 1e-3f),
              "a centre inside the box must still give a unit normal, not NaN");
    CHECK_MSG(::test::nearly(normal.y, 1.0f, 1e-3f),
              "the nearest face is the top, so it must push up");
    CHECK_MSG(penetration > 0.0f, "and by a positive amount");
}

static void testTheRampThatCouldNotBeBuilt() {
    // A 10 x 1 x 1 plank rotated 30 degrees. Its world AABB half-height is
    //   5 * sin(30) + 0.5 * cos(30) = 2.5 + 0.433 = 2.933
    // against a true half-height along its own short axis of 0.5. The old
    // AABB test therefore had a ball resting about 2.4 units above the surface,
    // on a normal of exactly (0, 1, 0) - level ground, as far as the solver
    // was concerned, so nothing ever rolled.
    const float angle = glm::radians(30.0f);
    const auto ramp = makeBox(glm::vec3(0.0f), glm::vec3(5.0f, 0.5f, 1.0f), angle);

    glm::vec3 normal(0.0f);
    float penetration = 0.0f;
    glm::vec3 point(0.0f);

    // A ball where the old code would have parked it: well above the plank.
    CHECK_MSG(!CollideSphereObb(glm::vec3(0.0f, 2.9f, 0.0f), 0.4f, ramp,
                                normal, penetration, point),
              "the AABB corner is empty space - nothing should be resting there");

    // Resting on the actual surface, just above the centre.
    CHECK(CollideSphereObb(glm::vec3(0.0f, 0.8f, 0.0f), 0.4f, ramp, normal, penetration, point));

    // And this is the payoff: the surface normal is TILTED, so gravity has a
    // component along it and the ball accelerates downhill instead of sitting
    // on invisible level ground.
    CHECK_MSG(normal.y < 0.95f,
              "a 30-degree ramp must produce a tilted normal, not a flat one");
    CHECK_MSG(std::fabs(normal.x) > 0.1f,
              "and it must lean along the slope");
}


// --- capsules ---------------------------------------------------------------
//
// A capsule is a segment with a radius, so all of this rests on the segment
// arithmetic below it. Every one of these numbers is checkable by hand, which is
// the reason this geometry lives outside PhysicsSystem at all.

static void testClosestPointOnASegmentIsClamped() {
    const glm::vec3 a(0.0f, 0.0f, 0.0f);
    const glm::vec3 b(0.0f, 4.0f, 0.0f);

    // Beside the middle.
    const glm::vec3 middle = ClosestPointOnSegment(a, b, glm::vec3(3.0f, 2.0f, 0.0f));
    CHECK_NEAR(middle.y, 2.0f);

    // Past each end. Unclamped, this would run off along the infinite line, and
    // a capsule would collide with things nowhere near it.
    const glm::vec3 below = ClosestPointOnSegment(a, b, glm::vec3(0.0f, -10.0f, 0.0f));
    CHECK_NEAR(below.y, 0.0f);
    const glm::vec3 above = ClosestPointOnSegment(a, b, glm::vec3(0.0f, 99.0f, 0.0f));
    CHECK_NEAR(above.y, 4.0f);

    // A segment of no length is a point, which is what a sphere is.
    const glm::vec3 degenerate = ClosestPointOnSegment(a, a, glm::vec3(5.0f, 5.0f, 5.0f));
    CHECK_NEAR(degenerate.x, 0.0f);
}

static void testClosestPointsBetweenCrossedSegments() {
    // Along x at y = 0, and along z at y = 2. They cross when seen from above,
    // so the nearest points are directly above and below the origin.
    glm::vec3 onFirst(0.0f);
    glm::vec3 onSecond(0.0f);
    ClosestPointsBetweenSegments(glm::vec3(-3.0f, 0.0f, 0.0f), glm::vec3(3.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 2.0f, -3.0f), glm::vec3(0.0f, 2.0f, 3.0f),
                                 onFirst, onSecond);

    CHECK_NEAR(onFirst.x, 0.0f);
    CHECK_NEAR(onFirst.y, 0.0f);
    CHECK_NEAR(onSecond.y, 2.0f);
    CHECK_NEAR(onSecond.z, 0.0f);
}

static void testParallelSegmentsDoNotProduceANaN() {
    // Two parallel segments give a zero denominator in the closed-form solve,
    // and dividing by it is a NaN. Every comparison against a NaN is false, so
    // the pair reports no contact - two parallel capsules lying against each
    // other pass straight through, which is most of them.
    glm::vec3 onFirst(0.0f);
    glm::vec3 onSecond(0.0f);
    ClosestPointsBetweenSegments(glm::vec3(-2.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f),
                                 glm::vec3(-2.0f, 1.0f, 0.0f), glm::vec3(2.0f, 1.0f, 0.0f),
                                 onFirst, onSecond);

    CHECK_MSG(onFirst == onFirst && onSecond == onSecond, "the answer must not be NaN");
    // Any pair along the overlap is as near as any other; what must hold is the
    // distance, which is the only part a caller may rely on.
    CHECK_NEAR(glm::length(onSecond - onFirst), 1.0f);
}

static void testClampingOneParameterMovesTheOther() {
    // The case the second solve exists for, and the one that is easy to write
    // and never notice. Solve for the first parameter, use it to get the second,
    // clamp the second to its segment - and the first is now wrong, because it
    // was the answer to a question about a point that is no longer there. The
    // symptom is a capsule resting past the end of another sitting slightly
    // inside it.
    //
    // Skew and NOT perpendicular, which is what makes it discriminate: with
    // perpendicular segments the recomputed parameter comes out the same and a
    // test built on those passes either way. Here the diagonal segment runs from
    // the origin to (1, 1, 0) and the short one sits off at x = 3, so the
    // nearest point on the diagonal is its far END - and dropping the second
    // solve leaves it at the origin instead.
    glm::vec3 onFirst(0.0f);
    glm::vec3 onSecond(0.0f);

    ClosestPointsBetweenSegments(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f),
                                 glm::vec3(3.0f, 0.0f, 1.0f), glm::vec3(4.0f, 0.0f, 1.0f),
                                 onFirst, onSecond);
    CHECK_NEAR(onFirst.x, 1.0f);
    CHECK_NEAR(onFirst.y, 1.0f);
    CHECK_NEAR(onSecond.x, 3.0f);

    // And with the second segment pointing the other way, so the clamp lands on
    // its far end rather than its near one. Both directions, because the two are
    // separate branches and one of them is always the one nobody tried.
    ClosestPointsBetweenSegments(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f),
                                 glm::vec3(4.0f, 0.0f, 1.0f), glm::vec3(3.0f, 0.0f, 1.0f),
                                 onFirst, onSecond);
    CHECK_NEAR(onFirst.x, 1.0f);
    CHECK_NEAR(onFirst.y, 1.0f);
    CHECK_NEAR(onSecond.x, 3.0f);
}

static void testASphereIsACapsuleWithNoLength() {
    // Not a tidiness argument. Sphere-against-sphere used to be its own routine,
    // and a second implementation of the same test is a second set of edge cases
    // to get wrong - and the sphere one had no speculative margin, so two fast
    // spheres passed through each other while a sphere and a box did not.
    glm::vec3 normal(0.0f);
    float penetration = 0.0f;
    glm::vec3 point(0.0f);

    const glm::vec3 left(0.0f, 0.0f, 0.0f);
    const glm::vec3 right(1.5f, 0.0f, 0.0f);

    CHECK(CollideCapsuleCapsule(left, left, 1.0f, right, right, 1.0f,
                                normal, penetration, point));
    CHECK_NEAR(penetration, 0.5f);
    CHECK_NEAR(normal.x, 1.0f);
    // Half way into the overlap, measured from the first sphere's surface.
    CHECK_NEAR(point.x, 0.75f);

    // Apart, and beyond any margin.
    const glm::vec3 far(5.0f, 0.0f, 0.0f);
    CHECK_MSG(!CollideCapsuleCapsule(left, left, 1.0f, far, far, 1.0f,
                                     normal, penetration, point),
              "spheres five units apart must not collide");

    // Apart, but closing fast enough to meet inside the step.
    CHECK_MSG(CollideCapsuleCapsule(left, left, 1.0f, far, far, 1.0f,
                                    normal, penetration, point, 4.0f),
              "a speculative margin must reach it");
    CHECK_MSG(penetration < 0.0f, "and report the gap as a negative penetration");
    CHECK_NEAR(penetration, -3.0f);
}

static void testTwoCapsulesTouchAtTheirNearestApproach() {
    // Upright, side by side, radius 0.5 each with their axes 0.8 apart: the
    // radii sum to 1.0, so they overlap by 0.2 wherever their straight sections
    // face each other.
    glm::vec3 normal(0.0f);
    float penetration = 0.0f;
    glm::vec3 point(0.0f);

    const glm::vec3 a0(0.0f, -1.0f, 0.0f);
    const glm::vec3 a1(0.0f, 1.0f, 0.0f);
    const glm::vec3 b0(0.8f, -1.0f, 0.0f);
    const glm::vec3 b1(0.8f, 1.0f, 0.0f);

    CHECK(CollideCapsuleCapsule(a0, a1, 0.5f, b0, b1, 0.5f, normal, penetration, point));
    CHECK_NEAR(penetration, 0.2f);
    CHECK_NEAR(normal.x, 1.0f);
    CHECK_MSG(std::fabs(normal.y) < 1e-4f,
              "two upright capsules must push each other sideways, not up");

    // Half way into the overlap, measured from the first capsule's surface.
    CHECK_NEAR(point.x, 0.4f);

    // Moved apart, they must stop being a contact.
    CHECK_MSG(!CollideCapsuleCapsule(a0, a1, 0.5f, glm::vec3(3.0f, -1.0f, 0.0f),
                                     glm::vec3(3.0f, 1.0f, 0.0f), 0.5f,
                                     normal, penetration, point),
              "three units apart is not a contact");
}

static void testACapsuleStandsOnTheTopOfABox() {
    // The whole reason the shape exists. Radius 0.4, straight section from
    // y = 1.2 to y = 2.0, so its lowest point is at y = 0.8. The box's top face
    // is at y = 1.0, so it is 0.2 deep.
    const auto floor = makeBox(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(5.0f, 1.0f, 5.0f));

    const auto manifold = CollideCapsuleObb(glm::vec3(0.0f, 1.2f, 0.0f),
                                            glm::vec3(0.0f, 2.0f, 0.0f), 0.4f, floor);
    CHECK(manifold.colliding);
    CHECK_NEAR(manifold.MaxPenetration(), 0.2f);
    CHECK_MSG(manifold.normal.y > 0.99f,
              "standing on a floor must push the capsule straight up, not sideways");
    CHECK_NEAR(manifold.points[0].position.y, 1.0f);

    // ONE point, because only the bottom cap is touching. A second contact under
    // the head would resist a lean the capsule is entitled to have.
    CHECK_EQ(manifold.pointCount, 1);
}

static void testACapsuleLyingOnASurfaceIsHeldAtBothEnds() {
    // The failure the box manifold exists to prevent, arriving again with the
    // new shape. A capsule lying along a floor touches it in a line; held by one
    // contact under its middle it is free to rock end over end about that point,
    // with nothing anywhere else to resist. Measured before this: still swinging
    // at a radian per second ten seconds after a nudge.
    const auto floor = makeBox(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(5.0f, 1.0f, 5.0f));

    const auto manifold = CollideCapsuleObb(glm::vec3(-0.6f, 1.3f, 0.0f),
                                            glm::vec3(0.6f, 1.3f, 0.0f), 0.4f, floor);
    CHECK(manifold.colliding);
    CHECK_EQ(manifold.pointCount, 2);
    CHECK_MSG(manifold.normal.y > 0.99f, "it must still be pushed straight up");

    // One under each end, not two in the same place - which would resist nothing.
    const float spread = std::fabs(manifold.points[0].position.x -
                                   manifold.points[1].position.x);
    CHECK_MSG(spread > 1.0f,
              "the two contacts must be at the ends: spread = " + std::to_string(spread));

    // Half off the edge, and the overhanging end must not be reported as
    // touching something that is not there.
    const auto overhang = CollideCapsuleObb(glm::vec3(4.6f, 1.3f, 0.0f),
                                            glm::vec3(6.4f, 1.3f, 0.0f), 0.4f, floor);
    CHECK(overhang.colliding);
    CHECK_MSG(overhang.pointCount == 1,
              "an end hanging over the edge is not a contact");
}

static void testACapsuleOnASlopeGetsTheSlopesNormal() {
    // The same failure the ramp test at the bottom of this file describes, for
    // the shape a character actually uses. Against the box's bounding box the
    // normal would be a world axis and a character would stand level on a hill.
    const float slope = glm::radians(30.0f);
    const auto ramp = makeBox(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(5.0f, 0.5f, 5.0f), slope);

    const auto manifold = CollideCapsuleObb(glm::vec3(0.0f, 0.6f, 0.0f),
                                           glm::vec3(0.0f, 2.0f, 0.0f), 0.3f, ramp);
    CHECK(manifold.colliding);
    CHECK_MSG(manifold.normal.y < 0.95f, "a 30-degree ramp must produce a tilted normal");
    CHECK_MSG(std::fabs(manifold.normal.x) > 0.1f, "and it must lean along the slope");
}

static void testACapsuleLyingAcrossABoxIsCaughtByItsMiddle() {
    // Horizontal, spanning a narrow pillar it does not touch at either end. The
    // nearest point is somewhere along the straight section, which is the case
    // that would be missed by testing only the two end spheres - the tempting
    // shortcut, and wrong for exactly the arrangement a bridge is.
    const auto pillar = makeBox(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.2f, 1.0f, 0.2f));

    const auto manifold = CollideCapsuleObb(glm::vec3(-3.0f, 1.2f, 0.0f),
                                            glm::vec3(3.0f, 1.2f, 0.0f), 0.4f, pillar);
    CHECK(manifold.colliding);
    CHECK_MSG(manifold.normal.y > 0.99f, "it must be pushed up off the top of the pillar");
    CHECK_NEAR(manifold.MaxPenetration(), 0.2f);

    // One point, even though the axis lies along the surface: both ends are far
    // out over nothing, so neither is a contact and only the middle is left.
    CHECK_EQ(manifold.pointCount, 1);

    // Raised clear, it must not be reported at all.
    CHECK_MSG(!CollideCapsuleObb(glm::vec3(-3.0f, 2.0f, 0.0f), glm::vec3(3.0f, 2.0f, 0.0f),
                                 0.4f, pillar).colliding,
              "clear of the pillar is not a contact");
}

static void runTests() {
    testClosestPointOnASegmentIsClamped();
    testClosestPointsBetweenCrossedSegments();
    testParallelSegmentsDoNotProduceANaN();
    testClampingOneParameterMovesTheOther();
    testASphereIsACapsuleWithNoLength();
    testTwoCapsulesTouchAtTheirNearestApproach();
    testACapsuleStandsOnTheTopOfABox();
    testACapsuleLyingOnASurfaceIsHeldAtBothEnds();
    testACapsuleOnASlopeGetsTheSlopesNormal();
    testACapsuleLyingAcrossABoxIsCaughtByItsMiddle();
    testSeparatedBoxesDoNotCollide();
    testAxisAlignedOverlapMatchesTheOldAabbAnswer();
    testFaceContactProducesAManifoldNotAPoint();
    testNormalAlwaysPointsFromAtowardB();
    testAGapInsideTheMarginIsReportedAsItIs();
    testRotationChangesTheAnswer();
    testAnAabbTestWouldHaveBeenWrong();
    testSphereAgainstARotatedBox();
    testSphereCentreInsideTheBoxStillPushesOut();
    testTheRampThatCouldNotBeBuilt();
}

TEST_MAIN("test_sat", 78)
