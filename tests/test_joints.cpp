// Joint constraints.
//
// Every way of getting these wrong looks like a physics bug rather than an
// arithmetic one: a pendulum that gains energy until it flies apart, a door
// that swings by sliding, a rope that shoves. So the checks below are on the
// arithmetic directly, with numbers that can be worked out by hand, and the
// integration cases live next to the solver in test_physics.
//
// The single most valuable one is testMomentumIsConserved: a joint impulse is
// the one place in this file that can create linear momentum out of nothing,
// and a sign error, an asymmetric effective mass and an unstable solve all show
// up there and nowhere else.

#include "core/Joints.hpp"
#include "TestHarness.hpp"

#include <cmath>

using namespace Supersonic;

namespace {

bool nearlyVec(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    return test::nearly(a.x, b.x, eps) && test::nearly(a.y, b.y, eps) &&
           test::nearly(a.z, b.z, eps);
}

bool finiteVec(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// A body that can be moved and turned. The inertia is isotropic, which is what
// a solid sphere has and what keeps the expected values arithmetic rather than
// a tensor rotation.
Joints::Body freeBody(const glm::vec3& position, float mass, float inertia = 1.0f) {
    Joints::Body body;
    body.position = position;
    body.inverseMass = 1.0f / mass;
    body.inverseInertia = glm::mat3(1.0f / inertia);
    return body;
}

// No mass and no inertia: the anchor a pendulum hangs from, and the convention
// the contact solver already uses for level geometry.
Joints::Body immovable(const glm::vec3& position) {
    Joints::Body body;
    body.position = position;
    body.inverseMass = 0.0f;
    body.inverseInertia = glm::mat3(0.0f);
    return body;
}

glm::vec3 anchorVelocityOf(const Joints::Body& body, const glm::vec3& arm) {
    return body.velocity + glm::cross(body.angularVelocity, arm);
}

// Momentum, from the inverse mass the solver actually uses. An immovable body
// carries none, which is the point: it is a wall, and a wall is entitled to
// absorb as much as it likes.
glm::vec3 momentumOf(const Joints::Body& body) {
    if (body.inverseMass <= 0.0f) return glm::vec3(0.0f);
    return body.velocity / body.inverseMass;
}

Joints::Constraint pointJoint(const glm::vec3& armA, const glm::vec3& armB) {
    Joints::Constraint joint;
    joint.type = Joints::Type::Point;
    joint.armA = armA;
    joint.armB = armB;
    return joint;
}

Joints::Constraint distanceJoint(float distance, bool rope = false) {
    Joints::Constraint joint;
    joint.type = Joints::Type::Distance;
    joint.distance = distance;
    joint.rope = rope;
    return joint;
}

// --- the conservation law -------------------------------------------------

void testMomentumIsConserved() {
    // Two bodies floating in nothing, one of them moving, joined at their
    // centres. A joint impulse is equal and opposite by construction, so
    // whatever it does to them it cannot change the total.
    //
    // This is the check that earns the most: a flipped sign in applyImpulse, an
    // effective mass that is not symmetric, and a solve that diverges all show
    // here, and it needs no tolerance argument because the answer is exact
    // arithmetic on the same two numbers.
    Joints::Body a = freeBody(glm::vec3(-1.0f, 0.0f, 0.0f), 2.0f);
    Joints::Body b = freeBody(glm::vec3(1.0f, 0.0f, 0.0f), 5.0f);
    a.velocity = glm::vec3(3.0f, -1.0f, 0.5f);
    b.velocity = glm::vec3(0.0f, 2.0f, 0.0f);

    const glm::vec3 before = momentumOf(a) + momentumOf(b);

    Joints::Constraint joint = pointJoint(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
    for (int pass = 0; pass < 8; ++pass) Joints::SolveVelocity(joint, a, b);

    const glm::vec3 after = momentumOf(a) + momentumOf(b);
    CHECK_MSG(nearlyVec(before, after, 1e-4f),
              "a joint may move momentum between two bodies and may not create any");
    CHECK(finiteVec(a.velocity) && finiteVec(b.velocity));
}

void testTheAnchorsStopMovingRelativeToEachOther() {
    // A point joint removes three degrees of freedom, and the effective mass is
    // inverted exactly rather than iterated - so ONE pass has to leave the
    // anchors with no relative motion at all, not merely less of it.
    Joints::Body a = freeBody(glm::vec3(0.0f), 1.0f);
    Joints::Body b = freeBody(glm::vec3(2.0f, 0.0f, 0.0f), 3.0f);
    a.velocity = glm::vec3(1.0f, 2.0f, -1.0f);
    b.velocity = glm::vec3(-2.0f, 0.5f, 0.0f);
    a.angularVelocity = glm::vec3(0.0f, 0.0f, 1.5f);
    b.angularVelocity = glm::vec3(0.3f, 0.0f, 0.0f);

    Joints::Constraint joint = pointJoint(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
    Joints::SolveVelocity(joint, a, b);

    const glm::vec3 relative =
        anchorVelocityOf(b, joint.armB) - anchorVelocityOf(a, joint.armA);
    CHECK_MSG(nearlyVec(relative, glm::vec3(0.0f), 1e-4f),
              "one pass of an exactly inverted effective mass leaves nothing to do");
}

void testAnOffCentreAnchorTurnsTheBody() {
    // The two skew terms in the effective mass are what make an anchor on the
    // EDGE of a body different from one at its centre. Drop them and the joint
    // drags the body bodily without ever turning it - so a door swings by
    // sliding, which is the failure this checks for.
    Joints::Body wall = immovable(glm::vec3(0.0f));
    Joints::Body door = freeBody(glm::vec3(1.0f, 0.0f, 0.0f), 1.0f, 0.5f);
    door.velocity = glm::vec3(0.0f, 0.0f, 4.0f);

    Joints::Constraint joint = pointJoint(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
    Joints::SolveVelocity(joint, wall, door);

    CHECK_MSG(glm::length(door.angularVelocity) > 0.1f,
              "a body pulled up at its edge has to turn about that edge");
    CHECK_MSG(nearlyVec(wall.velocity, glm::vec3(0.0f)) &&
              nearlyVec(wall.angularVelocity, glm::vec3(0.0f)),
              "and the thing it is hinged to must not move at all");

    // The hinge point itself is what is held still; the far side of the door is
    // free to keep going.
    const glm::vec3 atHinge = anchorVelocityOf(door, joint.armB);
    CHECK(nearlyVec(atHinge, glm::vec3(0.0f), 1e-4f));
}

// --- distance, and the difference between a rod and a rope ----------------

void testADistanceJointLeavesTheSwingAlone() {
    // ONE degree of freedom, and that is the whole difference from a point
    // joint: everything perpendicular to the line is untouched. Constrain all
    // three and a pendulum cannot swing, it hangs rigid.
    Joints::Body anchor = immovable(glm::vec3(0.0f, 5.0f, 0.0f));
    Joints::Body bob = freeBody(glm::vec3(0.0f, 3.0f, 0.0f), 1.0f);

    // Straight down the line, and straight across it.
    bob.velocity = glm::vec3(2.0f, -1.0f, 0.0f);

    Joints::Constraint joint = distanceJoint(2.0f);
    Joints::SolveVelocity(joint, anchor, bob);

    CHECK_MSG(test::nearly(bob.velocity.y, 0.0f, 1e-4f),
              "the component along the rod is removed");
    CHECK_MSG(test::nearly(bob.velocity.x, 2.0f, 1e-6f),
              "and the component across it is not touched at all, or nothing can swing");
    CHECK_NEAR(bob.velocity.z, 0.0f);
}

void testARopeOnlyEverPulls() {
    Joints::Body anchor = immovable(glm::vec3(0.0f, 5.0f, 0.0f));

    // Slack: the bob is nearer than the rope is long, so there is nothing
    // holding it and it may keep falling.
    {
        Joints::Body bob = freeBody(glm::vec3(0.0f, 4.0f, 0.0f), 1.0f);
        bob.velocity = glm::vec3(0.0f, -3.0f, 0.0f);
        Joints::Constraint joint = distanceJoint(2.0f, /*rope=*/true);
        Joints::SolveVelocity(joint, anchor, bob);
        CHECK_MSG(test::nearly(bob.velocity.y, -3.0f, 1e-6f),
                  "a slack rope does nothing whatever");
    }

    // Taut and still stretching: caught.
    {
        Joints::Body bob = freeBody(glm::vec3(0.0f, 3.0f, 0.0f), 1.0f);
        bob.velocity = glm::vec3(0.0f, -3.0f, 0.0f);
        Joints::Constraint joint = distanceJoint(2.0f, /*rope=*/true);
        Joints::SolveVelocity(joint, anchor, bob);
        CHECK_MSG(test::nearly(bob.velocity.y, 0.0f, 1e-4f),
                  "a taut rope stops it going any further");
    }

    // Taut but CLOSING. A rod would push it back out; a rope must let it come.
    {
        Joints::Body bob = freeBody(glm::vec3(0.0f, 3.0f, 0.0f), 1.0f);
        bob.velocity = glm::vec3(0.0f, 3.0f, 0.0f);
        Joints::Constraint joint = distanceJoint(2.0f, /*rope=*/true);
        Joints::SolveVelocity(joint, anchor, bob);
        CHECK_MSG(test::nearly(bob.velocity.y, 3.0f, 1e-6f),
                  "a rope may never push, and this is what stops a chain going rigid");
    }

    // The rod, for contrast, does exactly what the rope refused to.
    {
        Joints::Body bob = freeBody(glm::vec3(0.0f, 3.0f, 0.0f), 1.0f);
        bob.velocity = glm::vec3(0.0f, 3.0f, 0.0f);
        Joints::Constraint joint = distanceJoint(2.0f, /*rope=*/false);
        Joints::SolveVelocity(joint, anchor, bob);
        CHECK_MSG(test::nearly(bob.velocity.y, 0.0f, 1e-4f), "a rod holds in both directions");
    }
}

void testARopeCanUndoItsOwnOverCorrection() {
    // The accumulator is why the impulse is clamped rather than each pass's
    // change: a later pass has to be allowed to push, as long as the TOTAL is
    // still a pull. Without that a multi-pass solve on a slack rope shoves.
    Joints::Body a = freeBody(glm::vec3(0.0f), 1.0f);
    Joints::Body b = freeBody(glm::vec3(0.0f, -2.0f, 0.0f), 1.0f);
    b.velocity = glm::vec3(0.0f, -1.0f, 0.0f);

    Joints::Constraint joint = distanceJoint(2.0f, /*rope=*/true);
    for (int pass = 0; pass < 8; ++pass) Joints::SolveVelocity(joint, a, b);

    CHECK_MSG(joint.scalarImpulse <= 0.0f,
              "the total impulse a rope has applied can never be a push");
    CHECK(finiteVec(a.velocity) && finiteVec(b.velocity));
}

// --- the hinge ------------------------------------------------------------

void testTheAxisConstraintPullsAHingeBackIntoTrue() {
    Joints::Body frame = immovable(glm::vec3(0.0f));
    Joints::Body door = freeBody(glm::vec3(1.0f, 0.0f, 0.0f), 1.0f, 0.5f);

    Joints::Constraint joint;
    joint.type = Joints::Type::Hinge;
    joint.armA = glm::vec3(0.0f);
    joint.armB = glm::vec3(-1.0f, 0.0f, 0.0f);
    joint.axisA = glm::vec3(0.0f, 1.0f, 0.0f);
    joint.axisB = glm::normalize(glm::vec3(0.15f, 1.0f, 0.0f));   // knocked out of true
    joint.angularBias = 0.2f * 60.0f;                             // a fifth of it, per step

    const float before = glm::dot(joint.axisB, joint.axisA);
    Joints::SolveVelocity(joint, frame, door);

    CHECK_MSG(glm::length(door.angularVelocity) > 1e-4f,
              "a hinge out of true has to be given a spin that fixes it");

    // Where that spin puts the axis after one step, to first order.
    const glm::vec3 turned = glm::normalize(
        joint.axisB + glm::cross(door.angularVelocity, joint.axisB) * (1.0f / 60.0f));
    CHECK_MSG(glm::dot(turned, joint.axisA) > before,
              "and the spin has to move it TOWARDS the hinge axis, not away");

    CHECK_MSG(nearlyVec(frame.angularVelocity, glm::vec3(0.0f)),
              "the door frame does not turn");
}

void testTheAxisConstraintWorksWhicheverEndIsFree() {
    // The mirror of the case above, and the one a door hinged to the WORLD
    // actually uses: the free body is A and the immovable one is B. The
    // arithmetic is symmetric in the two bodies and this is the check that says
    // so, because the Jacobian is written in terms of B's axis and A's
    // perpendicular basis - which reads as though it favours one of them.
    Joints::Body door = freeBody(glm::vec3(1.0f, 0.0f, 0.0f), 1.0f, 0.5f);
    Joints::Body world = immovable(glm::vec3(0.0f));

    Joints::Constraint joint;
    joint.type = Joints::Type::Hinge;
    joint.armA = glm::vec3(-1.0f, 0.0f, 0.0f);
    joint.armB = glm::vec3(0.0f);
    joint.axisA = glm::normalize(glm::vec3(0.15f, 1.0f, 0.0f));   // the door, out of true
    joint.axisB = glm::vec3(0.0f, 1.0f, 0.0f);                    // the frame, fixed
    joint.angularBias = 0.2f * 60.0f;

    const float before = glm::dot(joint.axisA, joint.axisB);
    Joints::SolveVelocity(joint, door, world);

    CHECK_MSG(glm::length(door.angularVelocity) > 1e-4f,
              "the free end has to be the one that gets the spin");
    CHECK(nearlyVec(world.angularVelocity, glm::vec3(0.0f)));

    const glm::vec3 turned = glm::normalize(
        joint.axisA + glm::cross(door.angularVelocity, joint.axisA) * (1.0f / 60.0f));
    CHECK_MSG(glm::dot(turned, joint.axisB) > before,
              "and it has to turn the door towards the frame, not away from it");
}

void testAHingeBetweenTwoThingsThatCannotTurnDoesNothing() {
    // Both sides have zero inverse inertia, so the two-by-two is singular.
    // Inverting it anyway is a matrix of infinities that flows straight into a
    // velocity, and every later comparison against the NaN is false - which
    // reads as physics that has simply stopped working.
    Joints::Body a = immovable(glm::vec3(0.0f));
    Joints::Body b = immovable(glm::vec3(1.0f, 0.0f, 0.0f));

    Joints::Constraint joint;
    joint.type = Joints::Type::Hinge;
    joint.axisA = glm::vec3(0.0f, 1.0f, 0.0f);
    joint.axisB = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
    joint.angularBias = 12.0f;

    Joints::SolveVelocity(joint, a, b);

    CHECK(finiteVec(a.angularVelocity) && finiteVec(b.angularVelocity));
    CHECK(nearlyVec(a.angularVelocity, glm::vec3(0.0f)));
    CHECK(nearlyVec(b.angularVelocity, glm::vec3(0.0f)));
}

void testAHingeAboutEveryWorldAxisFindsAPerpendicularBasis() {
    // The perpendicular basis picks its reference by the axis's smallest
    // component. A fixed reference collapses whenever the axis is parallel to
    // it, and a hinge about world Y - a door - is the most ordinary thing an
    // author will build.
    const glm::vec3 axes[3] = {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
                               glm::vec3(0.0f, 0.0f, 1.0f)};
    int spun = 0;
    for (const glm::vec3& axis : axes) {
        Joints::Body a = immovable(glm::vec3(0.0f));
        Joints::Body b = freeBody(glm::vec3(1.0f, 0.0f, 0.0f), 1.0f, 0.5f);

        Joints::Constraint joint;
        joint.type = Joints::Type::Hinge;
        joint.armB = glm::vec3(-1.0f, 0.0f, 0.0f);
        joint.axisA = axis;
        // Tipped away from the axis by mixing in one of the other two.
        joint.axisB = glm::normalize(axis + glm::vec3(axis.z, axis.x, axis.y) * 0.2f);
        joint.angularBias = 12.0f;

        Joints::SolveVelocity(joint, a, b);
        CHECK(finiteVec(b.angularVelocity));
        if (glm::length(b.angularVelocity) > 1e-4f) ++spun;
    }
    CHECK_MSG(spun == 3, "a hinge about any world axis has to be correctable");
}

// --- the positional half --------------------------------------------------

void testPositionCorrectionIsSharedByInverseMass() {
    // The same rule the contact correction uses: the heavier body moves less,
    // and an immovable one does not move at all.
    Joints::Body light = freeBody(glm::vec3(0.0f), 1.0f);
    Joints::Body heavy = freeBody(glm::vec3(3.0f, 0.0f, 0.0f), 3.0f);

    const Joints::Constraint joint = pointJoint(glm::vec3(0.0f), glm::vec3(0.0f));
    CHECK(nearlyVec(Joints::Separation(joint, light, heavy), glm::vec3(3.0f, 0.0f, 0.0f)));

    glm::vec3 shiftLight(0.0f);
    glm::vec3 shiftHeavy(0.0f);
    CHECK(Joints::SolvePosition(joint, light, heavy, 1.0f, shiftLight, shiftHeavy));

    // Inverse masses 1 and 1/3, so the light one takes three quarters of it.
    CHECK_NEAR(shiftLight.x, 2.25f);
    CHECK_NEAR(shiftHeavy.x, -0.75f);
    CHECK_MSG(test::nearly(shiftLight.x - shiftHeavy.x, 3.0f, 1e-4f),
              "at a factor of one the two shifts must close the whole gap");
}

void testPositionCorrectionNeverMovesSomethingImmovable() {
    Joints::Body wall = immovable(glm::vec3(0.0f));
    Joints::Body body = freeBody(glm::vec3(0.0f, -3.0f, 0.0f), 2.0f);

    const Joints::Constraint joint = distanceJoint(2.0f);
    glm::vec3 shiftWall(0.0f);
    glm::vec3 shiftBody(0.0f);
    CHECK(Joints::SolvePosition(joint, wall, body, 1.0f, shiftWall, shiftBody));

    CHECK(nearlyVec(shiftWall, glm::vec3(0.0f)));
    // Three apart, two of rope: the whole overstretch is taken out of the body.
    CHECK_NEAR(shiftBody.y, 1.0f);
}

void testPositionCorrectionLeavesASlackRopeAlone() {
    Joints::Body anchor = immovable(glm::vec3(0.0f, 5.0f, 0.0f));
    Joints::Body bob = freeBody(glm::vec3(0.0f, 4.0f, 0.0f), 1.0f);

    glm::vec3 shiftA(0.0f);
    glm::vec3 shiftB(0.0f);

    const Joints::Constraint rope = distanceJoint(2.0f, /*rope=*/true);
    CHECK_MSG(!Joints::SolvePosition(rope, anchor, bob, 1.0f, shiftA, shiftB),
              "a rope shorter than its rest length is doing nothing");
    CHECK(nearlyVec(shiftB, glm::vec3(0.0f)));

    // A rod pushes the bob back OUT to its rest length, which is exactly the
    // difference between the two.
    const Joints::Constraint rod = distanceJoint(2.0f, /*rope=*/false);
    CHECK(Joints::SolvePosition(rod, anchor, bob, 1.0f, shiftA, shiftB));
    CHECK_NEAR(shiftB.y, -1.0f);
}

void testTwoImmovableBodiesCostNothing() {
    // A joint an author is entitled to build, and it must not divide by zero.
    Joints::Body a = immovable(glm::vec3(0.0f));
    Joints::Body b = immovable(glm::vec3(1.0f, 2.0f, 3.0f));

    Joints::Constraint joint = pointJoint(glm::vec3(0.0f), glm::vec3(0.0f));
    Joints::SolveVelocity(joint, a, b);
    CHECK(nearlyVec(a.velocity, glm::vec3(0.0f)) && nearlyVec(b.velocity, glm::vec3(0.0f)));

    glm::vec3 shiftA(1.0f);
    glm::vec3 shiftB(1.0f);
    CHECK(!Joints::SolvePosition(joint, a, b, 1.0f, shiftA, shiftB));
    CHECK(nearlyVec(shiftA, glm::vec3(0.0f)) && nearlyVec(shiftB, glm::vec3(0.0f)));
}

void testCoincidentAnchorsAreNotANaN() {
    // A distance joint whose two anchors are in the same place has no direction
    // to constrain along. Normalising it is a NaN that spreads into the
    // velocity and never comes back out.
    Joints::Body a = freeBody(glm::vec3(0.0f), 1.0f);
    Joints::Body b = freeBody(glm::vec3(0.0f), 1.0f);
    b.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    Joints::Constraint joint = distanceJoint(2.0f);
    Joints::SolveVelocity(joint, a, b);

    CHECK(finiteVec(a.velocity) && finiteVec(b.velocity));
    CHECK_MSG(nearlyVec(b.velocity, glm::vec3(1.0f, 0.0f, 0.0f)),
              "with no line to pull along there is nothing the joint can do");

    glm::vec3 shiftA(0.0f);
    glm::vec3 shiftB(0.0f);
    CHECK(!Joints::SolvePosition(joint, a, b, 1.0f, shiftA, shiftB));
}

void runTests() {
    testMomentumIsConserved();
    testTheAnchorsStopMovingRelativeToEachOther();
    testAnOffCentreAnchorTurnsTheBody();

    testADistanceJointLeavesTheSwingAlone();
    testARopeOnlyEverPulls();
    testARopeCanUndoItsOwnOverCorrection();

    testTheAxisConstraintPullsAHingeBackIntoTrue();
    testTheAxisConstraintWorksWhicheverEndIsFree();
    testAHingeBetweenTwoThingsThatCannotTurnDoesNothing();
    testAHingeAboutEveryWorldAxisFindsAPerpendicularBasis();

    testPositionCorrectionIsSharedByInverseMass();
    testPositionCorrectionNeverMovesSomethingImmovable();
    testPositionCorrectionLeavesASlackRopeAlone();
    testTwoImmovableBodiesCostNothing();
    testCoincidentAnchorsAreNotANaN();
}

} // namespace

TEST_MAIN("test_joints", 40)
