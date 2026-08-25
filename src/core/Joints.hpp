#pragma once

#include <glm/glm.hpp>

namespace Supersonic {

// Constraints between two bodies: the arithmetic, and nothing else.
//
// The gap this closes was named in the README: nothing held one body to
// another. A door, a rope bridge, a ragdoll limb and a suspension arm all want
// the same machinery and none of it existed, so anything hinged had to be
// faked by a script writing transforms - which is not a physical object, it is
// a body that ignores everything it touches.
//
// Kept out of PhysicsSystem.cpp for the reason CollisionSAT is: this is the
// part where a sign error is a pendulum that gains energy until it flies apart,
// and the only practical way to have confidence in a three-by-three effective
// mass matrix is to call it with hand-checkable numbers. There is no registry
// here, no components, no frame - the caller does every lookup and hands over
// world-space vectors.
namespace Joints {

// What a constraint is allowed to see of a body.
//
// `position` is the centre of mass, which for this engine is the entity's
// world transform origin - the point everything else integrates about.
//
// A body that cannot move has an inverse mass of zero, and one that cannot turn
// has a zero inverse inertia. Both fall out of the arithmetic as "infinitely
// hard to shift" with no branch at any use, which is the same convention the
// contact solver uses for a static obstacle.
struct Body {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    float inverseMass{0.0f};
    glm::mat3 inverseInertia{0.0f};
};

enum class Type {
    // The two anchors must coincide. Three degrees of freedom removed, none of
    // them rotational: a ragdoll shoulder, a pendulum that may also spin.
    Point,

    // The two anchors must stay a given distance apart. ONE degree of freedom,
    // which is the whole difference from Point: everything perpendicular to the
    // line is left alone, and that is what lets a pendulum swing rather than
    // hang rigid.
    Distance,

    // Point, plus the two rotational degrees of freedom that are not the hinge
    // axis. A door, a wheel, a lid.
    Hinge,
};

// One joint, fully resolved into world space.
//
// Everything here is computed once per step by the caller. The arms are world
// offsets from each body's centre to its anchor, not local ones, because this
// file does arithmetic and the caller is the only thing that knows about
// transforms, parents and scale.
struct Constraint {
    Type type{Type::Point};

    glm::vec3 armA{0.0f};
    glm::vec3 armB{0.0f};

    // Distance only.
    float distance{0.0f};

    // A rope resists STRETCHING and nothing else, so two bodies may drift
    // together freely and are caught only when the line goes taut. Without it
    // every chain is a set of rigid rods and a hanging one cannot fold.
    bool rope{false};

    // Hinge only: the axis in world space, unit length, one per body. They are
    // the same axis when the joint is satisfied, and the constraint is the
    // difference.
    glm::vec3 axisA{0.0f, 1.0f, 0.0f};
    glm::vec3 axisB{0.0f, 1.0f, 0.0f};

    // Hinge only: how much of the CURRENT misalignment to ask the velocity
    // solver to remove, per second. Already divided by the step, because the
    // caller is the only thing that knows the step.
    //
    // The linear half of every joint is corrected by moving the bodies instead,
    // exactly as a contact is - see SolvePosition. The angular half is not,
    // because writing a rotation back means going through the transform's Euler
    // triple and there is nothing pulling a hinge out of alignment the way
    // gravity pulls a rope down every single step.
    float angularBias{0.0f};

    // Accumulated across the iterations of ONE step, and reset between steps by
    // the caller rebuilding the list.
    //
    // The rope is why these exist. An inequality constraint has to clamp its
    // TOTAL impulse rather than each pass's change, or a later pass cannot undo
    // an earlier over-correction and a slack rope shoves instead of hanging.
    glm::vec3 linearImpulse{0.0f};
    float scalarImpulse{0.0f};
    glm::vec2 angularImpulse{0.0f};
};

// The vector from A's anchor to B's, which is the error a Point joint drives to
// zero and the line a Distance joint measures along.
glm::vec3 Separation(const Constraint& joint, const Body& a, const Body& b);

// One pass of the velocity solver over one joint, mutating both bodies.
//
// Called once per solver iteration, interleaved with the contact constraints,
// because a body held by a joint AND resting on the ground has to satisfy both
// at once - solving them in separate loops lets each undo the other.
void SolveVelocity(Constraint& joint, Body& a, Body& b);

// The positional half: how far each body has to move to close the error.
//
// Split by inverse mass, so the heavier body moves less and an immovable one
// does not move at all - the same rule the contact correction uses, and for the
// same reason.
//
// `factor` is the fraction of the error to remove this step. Correcting to
// exactly zero every step makes a loaded joint vibrate, because floating-point
// error re-creates the error immediately; the residue at 0.8 is under a
// millimetre for a body hanging under gravity at 60Hz.
//
// Returns false when there is nothing to correct, including a slack rope.
bool SolvePosition(const Constraint& joint, const Body& a, const Body& b, float factor,
                   glm::vec3& outShiftA, glm::vec3& outShiftB);

} // namespace Joints
} // namespace Supersonic
