#include "core/Joints.hpp"

#include <algorithm>
#include <cmath>

namespace Supersonic {
namespace Joints {

namespace {

constexpr float kEpsilon = 1.0e-8f;

// The matrix M for which M * v is cross(r, v).
//
// glm is column-major, so each triple below is a COLUMN. Written out rather
// than assembled from rows, because the transposed version of this is also a
// valid skew matrix - of -r - and the mistake produces a joint that pulls
// almost right and drifts.
glm::mat3 skew(const glm::vec3& r) {
    return glm::mat3( 0.0f,  r.z, -r.y,
                     -r.z,  0.0f,  r.x,
                      r.y, -r.x,  0.0f);
}

// The effective mass of the two anchors moving relative to each other:
// how much relative velocity one unit of impulse buys.
//
// (imA + imB) I - skew(rA) IinvA skew(rA) - skew(rB) IinvB skew(rB).
//
// The two skew terms are what make an off-centre anchor different from a
// central one. Drop them and a joint on the edge of a crate pulls the crate
// bodily without ever turning it, so a door swings by sliding.
glm::mat3 effectiveMass(const Body& a, const Body& b,
                        const glm::vec3& armA, const glm::vec3& armB) {
    const glm::mat3 crossA = skew(armA);
    const glm::mat3 crossB = skew(armB);
    return glm::mat3(a.inverseMass + b.inverseMass) -
           crossA * a.inverseInertia * crossA -
           crossB * b.inverseInertia * crossB;
}

// Velocity of the anchor, which is the body's own velocity plus whatever the
// spin contributes at that distance from the centre.
glm::vec3 anchorVelocity(const Body& body, const glm::vec3& arm) {
    return body.velocity + glm::cross(body.angularVelocity, arm);
}

void applyImpulse(Constraint& joint, Body& a, Body& b,
                  const glm::vec3& armA, const glm::vec3& armB, const glm::vec3& impulse) {
    // Equal and opposite, always. This is the one place linear momentum can be
    // created out of nothing, and a test asserts it is not.
    a.velocity -= impulse * a.inverseMass;
    a.angularVelocity -= a.inverseInertia * glm::cross(armA, impulse);
    b.velocity += impulse * b.inverseMass;
    b.angularVelocity += b.inverseInertia * glm::cross(armB, impulse);

    joint.appliedLinear += impulse;
}

void applyAngularImpulse(Constraint& joint, Body& a, Body& b, const glm::vec3& impulse) {
    a.angularVelocity -= a.inverseInertia * impulse;
    b.angularVelocity += b.inverseInertia * impulse;
    joint.appliedAngular += impulse;
}

// The hinge-axis constraints - the motor and the stop - both speak in terms of
// "how fast is A turning about the axis RELATIVE to B", so they share one apply
// and one effective mass.
//
// Positive lambda INCREASES that relative speed, which is the opposite sign
// from applyAngularImpulse's convention, and that is exactly why this exists:
// writing it inline twice is two chances to get the sign backwards, and a
// backwards motor drives the door shut.
void applyAxisImpulse(Constraint& joint, Body& a, Body& b,
                      const glm::vec3& axis, float lambda) {
    const glm::vec3 impulse = axis * lambda;
    a.angularVelocity += a.inverseInertia * impulse;
    b.angularVelocity -= b.inverseInertia * impulse;
    joint.appliedAngular += impulse;
}

float axisMass(const Body& a, const Body& b, const glm::vec3& axis) {
    const glm::mat3 inertia = a.inverseInertia + b.inverseInertia;
    return glm::dot(axis, inertia * axis);
}

float axisSpeed(const Body& a, const Body& b, const glm::vec3& axis) {
    return glm::dot(a.angularVelocity - b.angularVelocity, axis);
}

// Whether a 3x3 effective mass can be inverted at all.
//
// It cannot when neither body can move - two static bodies bolted together,
// which is a joint an author is entitled to build and which must cost nothing
// rather than produce a matrix of infinities that then flows into a velocity.
bool invertible(const glm::mat3& matrix) {
    return std::fabs(glm::determinant(matrix)) > kEpsilon;
}

void perpendicularBasis(const glm::vec3& axis, glm::vec3& outFirst, glm::vec3& outSecond) {
    outFirst = PerpendicularTo(axis);
    outSecond = glm::cross(axis, outFirst);
}

void solvePointVelocity(Constraint& joint, Body& a, Body& b) {
    const glm::mat3 mass = effectiveMass(a, b, joint.armA, joint.armB);
    if (!invertible(mass)) return;

    const glm::vec3 relative = anchorVelocity(b, joint.armB) - anchorVelocity(a, joint.armA);

    // No bias term. The position error is taken out by moving the bodies, the
    // way a contact's is - see SolvePosition - so all this has to do is stop
    // the anchors moving relative to each other.
    const glm::vec3 impulse = glm::inverse(mass) * -relative;

    joint.linearImpulse += impulse;
    applyImpulse(joint, a, b, joint.armA, joint.armB, impulse);
}

void solveDistanceVelocity(Constraint& joint, Body& a, Body& b) {
    const glm::vec3 separation = Separation(joint, a, b);
    const float length = glm::length(separation);
    // Coincident anchors have no direction to constrain along, and normalising
    // them is a NaN that spreads into the velocity and never comes out.
    if (length < kEpsilon) return;

    const glm::vec3 direction = separation / length;

    // A slack rope constrains nothing. Checked on the CURRENT separation rather
    // than once per step, because an earlier iteration may have pulled the
    // bodies together far enough that there is no longer anything to hold.
    if (joint.rope && length < joint.distance) return;

    const glm::vec3 angularA = glm::cross(joint.armA, direction);
    const glm::vec3 angularB = glm::cross(joint.armB, direction);
    const float mass = a.inverseMass + b.inverseMass +
                       glm::dot(angularA, a.inverseInertia * angularA) +
                       glm::dot(angularB, b.inverseInertia * angularB);
    if (mass < kEpsilon) return;

    const glm::vec3 relative = anchorVelocity(b, joint.armB) - anchorVelocity(a, joint.armA);
    float lambda = -glm::dot(relative, direction) / mass;

    if (joint.rope) {
        // Clamp the ACCUMULATED impulse, not this pass's change: a rope may
        // only pull, so the total has to stay non-positive, and a pass is
        // allowed to push as long as it is undoing an earlier over-pull.
        const float previous = joint.scalarImpulse;
        joint.scalarImpulse = std::min(previous + lambda, 0.0f);
        lambda = joint.scalarImpulse - previous;
    } else {
        joint.scalarImpulse += lambda;
    }

    applyImpulse(joint, a, b, joint.armA, joint.armB, direction * lambda);
}

void solveAxisVelocity(Constraint& joint, Body& a, Body& b) {
    glm::vec3 first(0.0f);
    glm::vec3 second(0.0f);
    perpendicularBasis(joint.axisA, first, second);

    // The rate of change of axisB . first is (wB - wA) . (axisB x first), so
    // those cross products ARE the Jacobian rows. Using `first` and `second`
    // directly is the usual shortcut and is only correct while the two axes
    // already agree, which is exactly when the constraint has nothing to do.
    const glm::vec3 rowFirst = glm::cross(joint.axisB, first);
    const glm::vec3 rowSecond = glm::cross(joint.axisB, second);

    const glm::mat3 inertia = a.inverseInertia + b.inverseInertia;
    const glm::vec3 inertiaFirst = inertia * rowFirst;
    const glm::vec3 inertiaSecond = inertia * rowSecond;

    const glm::mat2 mass(glm::dot(rowFirst, inertiaFirst), glm::dot(rowSecond, inertiaFirst),
                         glm::dot(rowFirst, inertiaSecond), glm::dot(rowSecond, inertiaSecond));
    const float determinant = mass[0][0] * mass[1][1] - mass[0][1] * mass[1][0];
    // Neither body can turn, so there is no misalignment anything could fix.
    if (std::fabs(determinant) < kEpsilon) return;

    const glm::vec3 relative = b.angularVelocity - a.angularVelocity;

    // The misalignment itself, fed in as a velocity the solver should reach.
    // Without it the constraint only stops the axes drifting FURTHER apart and
    // never brings them back, so a hinge knocked out of true stays that way.
    const glm::vec2 error(glm::dot(joint.axisB, first) * joint.angularBias,
                          glm::dot(joint.axisB, second) * joint.angularBias);

    const glm::vec2 rate(glm::dot(relative, rowFirst), glm::dot(relative, rowSecond));
    const glm::vec2 target = -(rate + error);

    const glm::vec2 lambda((target.x * mass[1][1] - target.y * mass[1][0]) / determinant,
                           (target.y * mass[0][0] - target.x * mass[0][1]) / determinant);

    joint.angularImpulse += lambda;
    applyAngularImpulse(joint, a, b, rowFirst * lambda.x + rowSecond * lambda.y);
}

// All three rotational degrees of freedom, for a weld.
//
// Velocity only: it holds the relative orientation the two bodies had when the
// joint started acting, rather than driving them to one it was told about.
// There is nowhere to store a rest orientation and no way to derive one - and
// holding what they have is what a weld MEANS, since welding two things is not
// supposed to move either of them.
//
// The price, stated: with the relative angular velocity driven to zero every
// step there is no systematic drift, only the float error of the integration,
// which accumulates far too slowly to see. A weld that had to survive being
// left alone for an hour would need its rest orientation stored.
void solveAngularLock(Constraint& joint, Body& a, Body& b) {
    const glm::mat3 inertia = a.inverseInertia + b.inverseInertia;
    if (!invertible(inertia)) return;

    const glm::vec3 relative = b.angularVelocity - a.angularVelocity;
    const glm::vec3 impulse = glm::inverse(inertia) * -relative;

    joint.lockImpulse += impulse;
    applyAngularImpulse(joint, a, b, impulse);
}

void solveMotor(Constraint& joint, Body& a, Body& b) {
    if (!joint.useMotor) return;

    const float mass = axisMass(a, b, joint.axisA);
    if (mass < kEpsilon) return;

    float lambda = (joint.motorSpeed - axisSpeed(a, b, joint.axisA)) / mass;

    // Clamped against the TOTAL, not this pass's change, and clamped at all
    // because a motor with no cap is infinitely strong: it drives whatever is
    // in the way straight through a wall rather than stalling against it.
    const float previous = joint.motorImpulse;
    joint.motorImpulse =
        std::clamp(previous + lambda, -joint.maxMotorImpulse, joint.maxMotorImpulse);
    lambda = joint.motorImpulse - previous;

    applyAxisImpulse(joint, a, b, joint.axisA, lambda);
}

void solveLimit(Constraint& joint, Body& a, Body& b) {
    if (!joint.useLimit) return;

    const float angle = HingeAngle(joint);

    // Which stop it is pressing against, if either. Away from both, a limit is
    // not a constraint at all - that is the difference between a hinge that
    // STOPS at ninety degrees and one welded at ninety degrees.
    float overshoot = 0.0f;
    float sign = 0.0f;
    if (angle < joint.minAngle) {
        overshoot = joint.minAngle - angle;
        sign = 1.0f;            // has to turn positively to get back into range
    } else if (angle > joint.maxAngle) {
        overshoot = angle - joint.maxAngle;
        sign = -1.0f;
    } else {
        // Off the stop entirely, so the accumulator has to go too: leave it and
        // the next time the door touches, the clamp below starts from an old
        // total and the door bounces off nothing.
        joint.limitImpulse = 0.0f;
        return;
    }

    const float mass = axisMass(a, b, joint.axisA);
    if (mass < kEpsilon) return;

    // Measured in the direction that RETURNS to range, so both stops are one
    // piece of arithmetic rather than two that can disagree about a sign.
    // NO bias, and that is the whole design of this stop.
    //
    // A bias asks the solver to reach a RETURN SPEED, and the body keeps that
    // speed once it is back in range because nothing takes it away again - so
    // the stop hands out energy and the door bounces off its own frame. At the
    // full rate it crossed its entire range and slammed into the opposite stop;
    // softened to half a radian a second it still drifted forty-five degrees
    // back off a ninety degree stop.
    //
    // So this removes only the speed going INTO the stop. The overshoot -
    // bounded by one step's travel - is walked out by ROTATING the body in the
    // position pass, exactly as the linear half of every joint is walked out by
    // moving it. See LimitOvershoot.
    (void)overshoot;
    const float returning = axisSpeed(a, b, joint.axisA) * sign;
    float lambda = -returning / mass;

    // An inequality: a stop may push back into range and may never pull the
    // door away from itself.
    const float previous = joint.limitImpulse;
    joint.limitImpulse = std::max(previous + lambda, 0.0f);
    lambda = joint.limitImpulse - previous;

    applyAxisImpulse(joint, a, b, joint.axisA, lambda * sign);
}

} // namespace

glm::vec3 PerpendicularTo(const glm::vec3& axis) {
    // The reference is chosen by the axis's SMALLEST component, which is what
    // stops the cross product collapsing: picking a fixed reference gives a
    // zero-length result whenever the axis happens to be parallel to it, and a
    // hinge about world Y is not an unusual thing to build.
    const glm::vec3 magnitude = glm::abs(axis);
    glm::vec3 reference(0.0f, 0.0f, 1.0f);
    if (magnitude.x <= magnitude.y && magnitude.x <= magnitude.z) {
        reference = glm::vec3(1.0f, 0.0f, 0.0f);
    } else if (magnitude.y <= magnitude.z) {
        reference = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    const glm::vec3 perpendicular = glm::cross(axis, reference);
    const float length = glm::length(perpendicular);
    return length > kEpsilon ? perpendicular / length : glm::vec3(1.0f, 0.0f, 0.0f);
}

float HingeAngle(const Constraint& joint) {
    const glm::vec3 axis = joint.axisA;

    // Both references are perpendicular to their OWN axis, and the two axes
    // drift a little apart between solves - so both are projected into the
    // plane of this one before the angle is taken. Without that the angle
    // wobbles by whatever the misalignment happens to be, and a limit set near
    // a stop chatters on and off.
    glm::vec3 fromA = joint.referenceA - axis * glm::dot(joint.referenceA, axis);
    glm::vec3 fromB = joint.referenceB - axis * glm::dot(joint.referenceB, axis);

    const float lengthA = glm::length(fromA);
    const float lengthB = glm::length(fromB);
    // A reference parallel to the axis has no direction in the plane. Zero is
    // the only answer that is not a NaN, and a NaN here would leave the limit
    // permanently on.
    if (lengthA < kEpsilon || lengthB < kEpsilon) return 0.0f;

    fromA /= lengthA;
    fromB /= lengthB;

    // A relative to B, so a door hinged to the world reads as the DOOR's angle:
    // the world is B and never moves. The other convention is just as correct
    // and reads backwards to everyone who has to author one.
    return std::atan2(glm::dot(glm::cross(fromB, fromA), axis), glm::dot(fromB, fromA));
}

float LimitOvershoot(const Constraint& joint) {
    if (!joint.useLimit) return 0.0f;

    const float angle = HingeAngle(joint);
    if (angle < joint.minAngle) return joint.minAngle - angle;   // turn positively to return
    if (angle > joint.maxAngle) return joint.maxAngle - angle;   // negatively
    return 0.0f;
}

glm::vec3 Separation(const Constraint& joint, const Body& a, const Body& b) {
    return (b.position + joint.armB) - (a.position + joint.armA);
}

void SolveVelocity(Constraint& joint, Body& a, Body& b) {
    switch (joint.type) {
    case Type::Distance:
        solveDistanceVelocity(joint, a, b);
        break;
    case Type::Hinge:
        // The point constraint first, because everything after it is a
        // correction to a joint that is already holding position: solving the
        // rotation of a hinge whose pin has drifted apart is answering the
        // wrong question.
        solvePointVelocity(joint, a, b);
        solveAxisVelocity(joint, a, b);
        // Then the motor, then the stop - in that order, so a motor driving
        // into a limit is overruled by the limit rather than fighting it to a
        // draw. A door held shut by a stop is not a door being driven open.
        solveMotor(joint, a, b);
        solveLimit(joint, a, b);
        break;
    case Type::Weld:
        solvePointVelocity(joint, a, b);
        solveAngularLock(joint, a, b);
        break;
    case Type::Point:
        solvePointVelocity(joint, a, b);
        break;
    }
}

bool SolvePosition(const Constraint& joint, const Body& a, const Body& b, float factor,
                   glm::vec3& outShiftA, glm::vec3& outShiftB) {
    outShiftA = glm::vec3(0.0f);
    outShiftB = glm::vec3(0.0f);

    const float total = a.inverseMass + b.inverseMass;
    // Neither body can be moved, so there is nothing to share out. Two static
    // bodies bolted together is a joint an author may build, and it has to cost
    // nothing rather than divide by zero.
    if (total < kEpsilon) return false;

    const glm::vec3 separation = Separation(joint, a, b);

    glm::vec3 correction(0.0f);
    if (joint.type == Type::Distance) {
        const float length = glm::length(separation);
        if (length < kEpsilon) return false;

        const float error = length - joint.distance;
        // Slack. A rope shorter than its rest length is doing nothing, and
        // pushing the bodies apart to reach it would make it a rod.
        if (joint.rope && error <= 0.0f) return false;
        if (std::fabs(error) < kEpsilon) return false;

        correction = (separation / length) * error;
    } else {
        // Point, Hinge and Weld share their linear half exactly.
        correction = separation;
        if (glm::dot(correction, correction) < kEpsilon * kEpsilon) return false;
    }

    const glm::vec3 scaled = correction * factor;
    outShiftA = scaled * (a.inverseMass / total);
    outShiftB = -scaled * (b.inverseMass / total);
    return true;
}

} // namespace Joints
} // namespace Supersonic
