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

static void runTests() {
    testSeparatedBoxesDoNotCollide();
    testAxisAlignedOverlapMatchesTheOldAabbAnswer();
    testFaceContactProducesAManifoldNotAPoint();
    testNormalAlwaysPointsFromAtowardB();
    testRotationChangesTheAnswer();
    testAnAabbTestWouldHaveBeenWrong();
    testSphereAgainstARotatedBox();
    testSphereCentreInsideTheBoxStillPushesOut();
    testTheRampThatCouldNotBeBuilt();
}

TEST_MAIN("test_sat", 25)
