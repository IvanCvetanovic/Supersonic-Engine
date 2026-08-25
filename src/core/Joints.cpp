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

void applyImpulse(Body& a, Body& b, const glm::vec3& armA, const glm::vec3& armB,
                  const glm::vec3& impulse) {
    // Equal and opposite, always. This is the one place linear momentum can be
    // created out of nothing, and a test asserts it is not.
    a.velocity -= impulse * a.inverseMass;
    a.angularVelocity -= a.inverseInertia * glm::cross(armA, impulse);
    b.velocity += impulse * b.inverseMass;
    b.angularVelocity += b.inverseInertia * glm::cross(armB, impulse);
}

void applyAngularImpulse(Body& a, Body& b, const glm::vec3& impulse) {
    a.angularVelocity -= a.inverseInertia * impulse;
    b.angularVelocity += b.inverseInertia * impulse;
}

// Whether a 3x3 effective mass can be inverted at all.
//
// It cannot when neither body can move - two static bodies bolted together,
// which is a joint an author is entitled to build and which must cost nothing
// rather than produce a matrix of infinities that then flows into a velocity.
bool invertible(const glm::mat3& matrix) {
    return std::fabs(glm::determinant(matrix)) > kEpsilon;
}

// Two unit vectors spanning the plane perpendicular to `axis`.
//
// The reference vector is chosen by the axis's SMALLEST component, which is
// what stops the cross product collapsing: picking a fixed reference gives a
// zero-length result whenever the axis happens to be parallel to it, and a
// hinge about world Y is not an unusual thing to build.
void perpendicularBasis(const glm::vec3& axis, glm::vec3& outFirst, glm::vec3& outSecond) {
    const glm::vec3 magnitude = glm::abs(axis);
    glm::vec3 reference(0.0f, 0.0f, 1.0f);
    if (magnitude.x <= magnitude.y && magnitude.x <= magnitude.z) {
        reference = glm::vec3(1.0f, 0.0f, 0.0f);
    } else if (magnitude.y <= magnitude.z) {
        reference = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    outFirst = glm::cross(axis, reference);
    const float length = glm::length(outFirst);
    outFirst = length > kEpsilon ? outFirst / length : glm::vec3(1.0f, 0.0f, 0.0f);
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
    applyImpulse(a, b, joint.armA, joint.armB, impulse);
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

    applyImpulse(a, b, joint.armA, joint.armB, direction * lambda);
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
    applyAngularImpulse(a, b, rowFirst * lambda.x + rowSecond * lambda.y);
}

} // namespace

glm::vec3 Separation(const Constraint& joint, const Body& a, const Body& b) {
    return (b.position + joint.armB) - (a.position + joint.armA);
}

void SolveVelocity(Constraint& joint, Body& a, Body& b) {
    switch (joint.type) {
    case Type::Distance:
        solveDistanceVelocity(joint, a, b);
        break;
    case Type::Hinge:
        // The point constraint first, because the axis constraint is a
        // correction to a joint that is already holding position: solving the
        // rotation of a hinge whose pin has drifted apart is answering the
        // wrong question.
        solvePointVelocity(joint, a, b);
        solveAxisVelocity(joint, a, b);
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
        // Point and Hinge share their linear half exactly.
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
