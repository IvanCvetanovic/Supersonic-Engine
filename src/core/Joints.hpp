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

    // The rigid body's per-axis locks (RigidBodyComponent::lockPosition and
    // lockRotation). The position lock arrives as a factor, 1 on a free axis
    // and 0 on a locked one, and a joint never moves a body along a locked axis
    // either. The rotation lock is already in inverseInertia, which the caller
    // builds locked; the flag only says the tensor may have rows of zeros.
    glm::vec3 linearFactor{1.0f};
    bool hasLinearLock{false};
    bool hasAngularLock{false};
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

    // Point, plus ALL THREE rotational degrees of freedom: two bodies rigidly
    // fixed to each other.
    //
    // Not the same as parenting, which is the thing people reach for instead. A
    // parented child integrates in its parent's space and inherits that motion
    // on top of its own, so it is carried rather than held - it does not push
    // back, and the pair has no shared response to being hit.
    Weld,
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

    // A reference direction perpendicular to each axis, world space and unit
    // length, each rotating with its own body.
    //
    // The angle between them about the axis IS the hinge angle, and it is the
    // only thing a limit or a motor can be stated against. Derived from the
    // axis by PerpendicularTo, which is deterministic, so a body's reference
    // is the same direction every step rather than one that wanders as the
    // solver nudges the axis.
    glm::vec3 referenceA{1.0f, 0.0f, 0.0f};
    glm::vec3 referenceB{1.0f, 0.0f, 0.0f};

    // Hinge only: how far the joint may turn, in radians, measured as A
    // relative to B - which for a door hinged to the world is the door's own
    // angle, because the world is B and does not move.
    //
    // Zero is where the two reference directions coincide. That is an arbitrary
    // configuration rather than a meaningful one, which is why the inspector
    // shows the live angle: limits are authored by looking at the number, not
    // by predicting it.
    bool useLimit{false};
    float minAngle{0.0f};
    float maxAngle{0.0f};

    // Hinge only: drive the joint at a speed rather than let it swing.
    //
    // maxMotorImpulse is a torque already multiplied by the step, because the
    // caller is the only thing that knows the step. Without a cap a motor is
    // infinitely strong and shoves whatever is in the way through a wall.
    // A hinge that pulls toward an angle instead of holding one.
    //
    // Authored as a FREQUENCY in hertz and a damping RATIO, not as a stiffness
    // and a damping coefficient, because the pair below is mass-independent:
    // "3 Hz, critically damped" behaves the same on a garden gate and on a bank
    // vault door. A stiffness that felt right on one would throw the other.
    //
    // Zero frequency is off. The scales derived from it in PhysicsSystem then
    // make solveSpring's expression collapse to exactly the rigid one, which is
    // deliberate - see the note there.
    bool useSpring{false};
    float springFrequency{0.0f};
    float springDamping{1.0f};
    float springRestAngle{0.0f};

    bool useMotor{false};
    float motorSpeed{0.0f};
    float maxMotorImpulse{0.0f};

    // Hinge only: how much of the CURRENT misalignment to ask the velocity
    // solver to remove, per second. Already divided by the step.
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
    // The rope and the limit are why these exist. An inequality constraint has
    // to clamp its TOTAL impulse rather than each pass's change, or a later
    // pass cannot undo an earlier over-correction and a slack rope shoves
    // instead of hanging.
    glm::vec3 linearImpulse{0.0f};
    float scalarImpulse{0.0f};
    glm::vec2 angularImpulse{0.0f};
    glm::vec3 lockImpulse{0.0f};
    float motorImpulse{0.0f};
    float springImpulse{0.0f};

    // Derived from frequency, damping and the STEP, so they belong to whoever
    // owns the step. PhysicsSystem fills them; nothing else should.
    //
    // This is constraint-force mixing, and the form matters. The softness
    // divides the impulse rather than scaling the bias up, which is the whole
    // difference between a spring and the accidental one this engine already
    // had: a hinge limit with a velocity bias hands out energy and the door
    // bounces off its own frame. Here springImpulseScale bleeds the accumulated
    // impulse away instead, so the constraint can only ever remove energy.
    float springBiasRate{0.0f};
    float springMassScale{1.0f};
    float springImpulseScale{0.0f};
    float limitImpulse{0.0f};

    // Everything the joint actually applied this step, kept apart because a
    // force and a torque are not the same quantity and a joint that breaks
    // under load has to be told which one broke it.
    glm::vec3 appliedLinear{0.0f};
    glm::vec3 appliedAngular{0.0f};
};

// A unit vector perpendicular to `axis`.
//
// Deterministic: the reference is chosen by the axis's SMALLEST component, so
// the same axis always gives the same perpendicular. A hinge angle measured
// against a perpendicular that wandered would drift with it, and the limit
// would move.
glm::vec3 PerpendicularTo(const glm::vec3& axis);

// The hinge angle in radians: A relative to B, about axisA, zero where the two
// reference directions coincide.
float HingeAngle(const Constraint& joint);

// How far A has to turn about the axis, in radians, to bring the hinge back
// inside its limits. Zero when it is already inside, or when there is no limit.
//
// The POSITION half of a stop. The velocity half removes only the speed going
// into it, because a velocity bias asks for a return SPEED and the body keeps
// that speed once it is back in range - which is a door that bounces off its
// own frame. This is the same split the linear half of every joint uses, and it
// is what makes a stop absorb rather than rebound.
float LimitOvershoot(const Constraint& joint);

// The vector from A's anchor to B's, which is the error a Point joint drives to
// zero and the line a Distance joint measures along.
glm::vec3 Separation(const Constraint& joint, const Body& a, const Body& b);

// One pass of the velocity solver over one joint, mutating both bodies.
//
// A `type` outside the enum does nothing at all rather than falling into one of
// the four: the switch is exhaustive over the enumerators, so a value that is
// none of them matches no case. That is the safe direction - a joint nobody can
// name holds nothing - and it is written down here because it is otherwise
// invisible.
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
