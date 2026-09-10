#include "core/PhysicsSystem.hpp"
#include "core/CollisionSAT.hpp"
#include "core/Heightfield.hpp"
#include "core/CollisionHull.hpp"
#include "core/ConvexHullCache.hpp"
#include "core/HeightfieldCache.hpp"
#include "core/Joints.hpp"
#include "core/Log.hpp"
#include "core/PhysicsSettings.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include "core/Components.hpp"
#include "core/DetMath.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace Supersonic {

namespace {

// Defaults for a body that has no RigidBodyComponent at all - a static
// collider. A body that has one carries its own.
constexpr float kRestitution = 0.3f;
constexpr float kRestVelocity = 0.1f;

// Positional correction. Bodies are allowed to interpenetrate by kSlop before
// anything is pushed apart, and only kCorrection of the remaining overlap is
// removed per step: correcting to exactly zero makes resting stacks vibrate,
// because floating-point error re-creates the overlap every step.
// How many times the velocity solver revisits every contact.
//
// One pass resolves each contact as though it were the only one in the world,
// so a box in a stack is pushed out of the box below it and straight into the
// box above, and the stack sinks and shivers. Revisiting lets the contacts
// reach an arrangement that satisfies all of them at once - which is the whole
// reason a stack settles rather than merely stops.
//
// Eight is the usual default and is well past the point of visible improvement
// here; the cost is eight cheap passes over a list that is already built.
constexpr int kSolverIterations = 8;

// How many times the joint position pass sweeps the list.
//
// One is enough for a single joint and nowhere near enough for a CHAIN. Each
// link carries the weight of everything below it, so the errors are coupled:
// correcting every link once, from the positions they all had at the start,
// is a Jacobi sweep and it converges about as slowly as one. Measured on a
// five-link rope at 60Hz, one pass left every link stretched by three to five
// per cent - a rope visibly longer than it was built - and four passes,
// re-reading where each body has just been PUT, brings it under half a per
// cent.
//
// Four rather than eight because the returns fall off a cliff after three and
// the pass is not free: it re-derives a world matrix per body per sweep.
constexpr int kJointPositionIterations = 4;

constexpr float kSlop = 0.005f;
constexpr float kCorrection = 0.8f;

// Coulomb friction. Without it a box landing on a slope slides forever and a
// stack of boxes never settles laterally.
constexpr float kFriction = 0.4f;

// ---- Sleeping ---------------------------------------------------------------
//
// Below both thresholds for kSleepTime and a body stops being simulated.
//
// The thresholds are well under kRestVelocity on purpose: a body that is merely
// at the point where its bounce is being killed has not settled, and putting it
// to sleep there would freeze it one step into whatever it was still doing. By
// the time it is this slow it has already stopped.
constexpr float kSleepLinearVelocity = 0.05f;
constexpr float kSleepAngularVelocity = 0.05f;

// Half a second. Long enough that nothing sleeps at the apex of a bounce - a
// body thrown up is below the linear threshold for about ten milliseconds -
// and short enough that a settled scene goes quiet while you are still looking
// at it.
constexpr float kSleepTime = 0.5f;

// How far a sleeping body may be moved before it counts as having been moved by
// something else. Nothing in the solver touches a sleeping body's transform, so
// this only has to be above the noise of reading the value back.
constexpr float kSleepWakeDistance = 1e-4f;

// How two surfaces combine.
//
// Restitution takes the larger of the two: a superball dropped on concrete
// bounces, and taking the smaller or the average would mean any dead surface
// killed every ball that touched it.
float combineRestitution(float a, float b) {
    return std::clamp(std::max(a, b), 0.0f, 0.99f);
}

// Friction is the geometric mean, which is the usual choice and has the
// property that matters: ice against anything is still slippery, because a
// zero on either side takes the result to zero. An average would let a rough
// floor grip a puck.
float combineFriction(float a, float b) {
    const float clampedA = std::clamp(a, 0.0f, 4.0f);
    const float clampedB = std::clamp(b, 0.0f, 4.0f);
    return std::sqrt(clampedA * clampedB);
}

// A sphere is a capsule whose segment has no length, which is why there is no
// separate sphere test below: keeping them apart means two implementations that
// can disagree about a case neither author thought of.
//
// Heightfield is the odd one out and stays that way on purpose: it is always
// immovable, it is the only shape whose collision lives outside CollisionSAT,
// and it is a SURFACE rather than a volume. Folding it in with the others would
// mean every one of those three facts becoming a branch somewhere.
enum class Shape { Box, Sphere, Capsule, Heightfield, Hull };

struct Body {
    entt::entity entity{entt::null};
    Shape shape{Shape::Box};

    // World space.
    glm::vec3 centre{0.0f};
    glm::vec3 halfExtent{0.5f}; // boxes: WORLD-AXIS-ALIGNED, for the broadphase
    float radius{0.5f};         // spheres and capsules

    // Half the length of a capsule's straight section, along axes[1]. Zero for
    // a sphere, which is the whole of the difference between the two.
    float halfSegment{0.0f};

    // ---- Hulls only ----
    //
    // The hull itself, owned by the registry's ConvexHullCache, and the basis
    // that places it. The basis carries SCALE as well as rotation, which for a
    // hull is exact rather than an approximation - see the component.
    const ConvexDecomposition* pieces{nullptr};
    glm::mat3 hullBasis{1.0f};

    // ---- Heightfields only ----
    //
    // The grid itself, owned by the registry's HeightfieldCache and valid for
    // as long as the step is.
    const Heightfield* field{nullptr};

    // The entity's world ORIGIN, which for a heightfield is not `centre`: the
    // grid is not centred on it, so `centre` carries the offset to the middle
    // of the bounds and this carries the point the local space is measured
    // from.
    glm::vec3 fieldOrigin{0.0f};

    // One number, because a heightfield's local space has to stay a heightfield
    // - a non-uniform scale would turn a sphere queried against it into an
    // ellipsoid. The largest axis wins, which is the same choice a sphere
    // collider already makes and errs by over-reporting.
    float fieldScale{1.0f};

    // The box in its own frame, which is what the narrowphase needs.
    //
    // halfExtent above is the world AABB - the enclosing box, not the box - and
    // using it for collision is what made a rotated crate collide as the volume
    // that contains it. It is still what the sweep-and-prune sorts on, because a
    // broadphase wants the conservative bound.
    glm::vec3 localHalfExtent{0.5f};
    glm::mat3 axes{1.0f};

    // Per-axis distance travelled this step, used to widen the broadphase bound
    // and to decide how large a gap is worth reporting.
    glm::vec3 sweep{0.0f};

    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    float inverseMass{0.0f};
    bool isTrigger{false};

    // World-space inverse inertia. Zero for anything that cannot turn, which
    // makes the impulse arithmetic treat it as infinitely hard to turn without
    // a branch at every use.
    glm::mat3 inverseInertia{0.0f};

    // The rigid body's per-axis locks as factors - 1 on a free axis, 0 on a
    // locked one - and whether any is set. See linearFactorOf.
    glm::vec3 linearFactor{1.0f};
    glm::vec3 angularFactor{1.0f};
    bool hasLinearLock{false};
    bool hasAngularLock{false};

    // Converts a world-space displacement into the entity's local space. A
    // parented body stores its position relative to its parent, so applying a
    // world-space push to it directly moves it by the wrong amount and in the
    // wrong direction the moment the parent is rotated or scaled.
    glm::mat3 worldToLocal{1.0f};
};

// Parent's world matrix, or identity. Read from the cache TransformSystem fills
// once per frame; within a frame's several fixed steps a moving parent's matrix
// is therefore one step stale, which is far cheaper than re-resolving the whole
// hierarchy per step and is invisible at 60Hz.
glm::mat4 parentWorldMatrix(entt::registry& registry, entt::entity entity) {
    const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
    if (!hierarchy || hierarchy->parent == entt::null) return glm::mat4(1.0f);
    if (!registry.valid(hierarchy->parent)) return glm::mat4(1.0f);

    if (const auto* world = registry.try_get<WorldTransformComponent>(hierarchy->parent)) {
        return world->matrix;
    }
    return glm::mat4(1.0f);
}

// Axis-aligned world bounds of a local box under a transform: centre through
// the matrix, extent through its absolute basis. Exact for translation and
// scale, and conservative for rotation - a rotated box collides as the box that
// encloses it, which over-reports rather than letting things pass through.
void worldBounds(const glm::mat4& transform, const glm::vec3& localHalfExtent,
                 glm::vec3& outCentre, glm::vec3& outHalfExtent) {
    outCentre = glm::vec3(transform[3]);

    const glm::mat3 basis(transform);
    outHalfExtent = glm::vec3(
        std::abs(basis[0].x) * localHalfExtent.x + std::abs(basis[1].x) * localHalfExtent.y + std::abs(basis[2].x) * localHalfExtent.z,
        std::abs(basis[0].y) * localHalfExtent.x + std::abs(basis[1].y) * localHalfExtent.y + std::abs(basis[2].y) * localHalfExtent.z,
        std::abs(basis[0].z) * localHalfExtent.x + std::abs(basis[1].z) * localHalfExtent.y + std::abs(basis[2].z) * localHalfExtent.z);
}

float inverseMassOf(const RigidBodyComponent* rigidBody) {
    // No rigid body at all means a static obstacle, which is how the ground and
    // level geometry participate without needing to be simulated.
    if (!rigidBody) return 0.0f;
    if (rigidBody->isKinematic) return 0.0f;
    if (rigidBody->mass <= 0.0f) return 0.0f;
    return 1.0f / rigidBody->mass;
}

// ---- Per-axis locks ----
//
// RigidBodyComponent::lockPosition and lockRotation as the solver uses them: 1
// on an axis the body may move along or turn about, 0 on one it may not.
//
// Multiplying an impulse by one of these is exact on a free axis - x * 1 is x -
// so the apply sites multiply unconditionally. The two places a lock would
// otherwise change the arithmetic of a body that has none, an effective mass and
// the inverse inertia, branch on the Body's flags instead, so a scene with no
// locks steps bit for bit as it did before they existed (test_determinism).
glm::vec3 linearFactorOf(const RigidBodyComponent* rigidBody) {
    if (!rigidBody) return glm::vec3(1.0f);
    return glm::vec3(rigidBody->lockPosition.x ? 0.0f : 1.0f, rigidBody->lockPosition.y ? 0.0f : 1.0f,
                     rigidBody->lockPosition.z ? 0.0f : 1.0f);
}

glm::vec3 angularFactorOf(const RigidBodyComponent* rigidBody) {
    if (!rigidBody) return glm::vec3(1.0f);
    return glm::vec3(rigidBody->lockRotation.x ? 0.0f : 1.0f, rigidBody->lockRotation.y ? 0.0f : 1.0f,
                     rigidBody->lockRotation.z ? 0.0f : 1.0f);
}

void setLocks(Body& body, const RigidBodyComponent* rigidBody) {
    body.linearFactor = linearFactorOf(rigidBody);
    body.angularFactor = angularFactorOf(rigidBody);
    body.hasLinearLock = rigidBody && glm::any(rigidBody->lockPosition);
    body.hasAngularLock = rigidBody && glm::any(rigidBody->lockRotation);
}

// The world inverse inertia with the locked world axes taken out - P I^-1 P,
// for P the diagonal of the angular factor. No torque turns the body about a
// locked axis, and no effective mass counts a turn it cannot make. Applied once,
// where the tensor is built, so everything downstream - contacts and joints -
// sees the locked tensor without being told.
glm::mat3 withLocks(const Body& body, const glm::mat3& inverseInertia) {
    if (!body.hasAngularLock) return inverseInertia;
    glm::mat3 locked = inverseInertia;
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) locked[column][row] *= body.angularFactor[column] * body.angularFactor[row];
    }
    return locked;
}

// The inverse mass a body presents along a unit direction: the scalar, unless an
// axis is locked, and then only the part of the direction it can move along.
// Branched rather than multiplied through, because the dot product rounds and an
// unlocked body must see exactly the number it always did.
float linearInverseMass(const Body& body, const glm::vec3& direction) {
    if (!body.hasLinearLock) return body.inverseMass;
    return body.inverseMass * glm::dot(direction * direction, body.linearFactor);
}

// Whether a contact reads this body's velocity and spin: yes for what the solver
// moves, and yes for a kinematic body, whose owner moves it and writes the
// velocity that says so. Read, never written - the impulses stay gated on
// inverse mass, so nothing here pushes a kinematic body.
//
// Left out, a platform looks still to the solve. A crate on one sliding sideways
// feels no friction and is left behind; on one going down it is caught, stopped
// dead and dropped again, a step at a time.
bool presentsVelocity(const RigidBodyComponent* rigidBody, const Body& body) {
    return rigidBody && (body.inverseMass > 0.0f || rigidBody->isKinematic);
}


// Inverse inertia, in the body's own axes, as a diagonal.
//
// Both shapes are symmetric enough that the tensor is diagonal in local axes,
// which is what keeps this three floats instead of a matrix: a box about its
// centre and a solid sphere both have no products of inertia.
//
// A body that cannot rotate - static, kinematic, or explicitly frozen - gets
// zero, which falls out of the impulse arithmetic as "infinitely hard to
// turn" without needing a branch at every use.
glm::vec3 inverseInertiaLocal(const RigidBodyComponent* rigidBody, Shape shape,
                              const glm::vec3& halfExtent, float radius) {
    if (!rigidBody || rigidBody->isKinematic || rigidBody->freezeRotation) return glm::vec3(0.0f);
    if (rigidBody->isSleeping) return glm::vec3(0.0f);
    if (rigidBody->mass <= 0.0f) return glm::vec3(0.0f);

    const float mass = rigidBody->mass;

    if (shape == Shape::Sphere) {
        // Solid sphere: 2/5 m r^2 about every axis.
        const float inertia = 0.4f * mass * radius * radius;
        return inertia > 1e-9f ? glm::vec3(1.0f / inertia) : glm::vec3(0.0f);
    }

    if (shape == Shape::Capsule) {
        // A solid cylinder of the same radius and total height, which is an
        // approximation: it puts the mass of the hemispherical caps slightly
        // further from the axis than it really is, so a capsule is a few
        // percent harder to tip end over end than it should be. Wrong in the
        // stable direction, and invisible next to the fact that the shape
        // exists mostly for characters, which usually freeze rotation anyway.
        //
        // halfExtent.y is half the TOTAL height here, caps included.
        const float halfHeight = std::max(halfExtent.y, radius);
        const float full = halfHeight * 2.0f;
        const float aboutAxis = 0.5f * mass * radius * radius;
        const float acrossAxis = mass * (3.0f * radius * radius + full * full) / 12.0f;
        return glm::vec3(acrossAxis > 1e-9f ? 1.0f / acrossAxis : 0.0f,
                         aboutAxis > 1e-9f ? 1.0f / aboutAxis : 0.0f,
                         acrossAxis > 1e-9f ? 1.0f / acrossAxis : 0.0f);
    }

    // Solid box, from FULL extents: m/12 * (y^2 + z^2) about x, and so on.
    const glm::vec3 full = halfExtent * 2.0f;
    const glm::vec3 inertia(
        mass * (full.y * full.y + full.z * full.z) / 12.0f,
        mass * (full.x * full.x + full.z * full.z) / 12.0f,
        mass * (full.x * full.x + full.y * full.y) / 12.0f);

    return glm::vec3(inertia.x > 1e-9f ? 1.0f / inertia.x : 0.0f,
                     inertia.y > 1e-9f ? 1.0f / inertia.y : 0.0f,
                     inertia.z > 1e-9f ? 1.0f / inertia.z : 0.0f);
}

// The same tensor in world axes: R * I * R^T, which for a diagonal I is the
// sum of each axis scaled by its column.
glm::mat3 worldInverseInertia(const glm::vec3& inverseLocal, const glm::mat3& orientation) {
    glm::mat3 result(0.0f);
    for (int axis = 0; axis < 3; ++axis) {
        const glm::vec3 column = orientation[axis];
        result += inverseLocal[axis] * glm::outerProduct(column, column);
    }
    return result;
}

// A body's box in its own frame, for the narrowphase.
CollisionSAT::Obb obbOf(const Body& body) {
    CollisionSAT::Obb obb;
    obb.centre = body.centre;
    obb.halfExtent = body.localHalfExtent;
    obb.axes = body.axes;
    return obb;
}

// The two endpoints of a body's capsule axis. A sphere returns the same point
// twice, which is exactly what makes it a capsule with no length.
void capsuleEnds(const Body& body, glm::vec3& outA, glm::vec3& outB) {
    const glm::vec3 half = body.axes[1] * body.halfSegment;
    outA = body.centre - half;
    outB = body.centre + half;
}

} // namespace

void PhysicsSystem::SweepAndPrune(std::vector<Proxy>& proxies,
                                  std::vector<std::pair<size_t, size_t>>& outPairs) {
    outPairs.clear();
    if (proxies.size() < 2) return;

    // Sorted on X, then a forward scan that stops as soon as the next proxy
    // starts past the current one's end. This is what turns the O(n^2) all-pairs
    // test into something that costs a sort plus the actual overlaps.
    //
    // A TOTAL ORDER, and the index is what makes it one. Comparing on min.x
    // alone leaves every tie to std::sort, which is introsort and is free to
    // order equal elements however its implementation feels - so a row of
    // crates all starting at the same x, which is what a stacked or grid-placed
    // scene is made of, came out in one order under MSVC and potentially
    // another under libstdc++.
    //
    // That reaches the simulation rather than stopping at the broadphase: the
    // pair list is emitted in this order, the solver applies impulses in the
    // order it receives pairs, and floating-point addition is not associative.
    // Two machines would resolve the same pile-up to slightly different
    // positions and drift apart from there - which is a determinism hole that
    // no amount of fixing the clock or the input would close, and which only
    // ever shows up as "the replay from my machine does not reproduce on
    // yours".
    std::sort(proxies.begin(), proxies.end(), [](const Proxy& lhs, const Proxy& rhs) {
        if (lhs.min.x != rhs.min.x) return lhs.min.x < rhs.min.x;
        return lhs.index < rhs.index;
    });

    for (size_t i = 0; i < proxies.size(); ++i) {
        const Proxy& a = proxies[i];
        for (size_t j = i + 1; j < proxies.size(); ++j) {
            const Proxy& b = proxies[j];
            if (b.min.x > a.max.x) break;

            // Two immovable things can overlap all they like.
            if (a.inverseMass == 0.0f && b.inverseMass == 0.0f) continue;

            // BOTH sides must agree. One-way filtering would let A push B while
            // B ignored A, which the solver resolves as a one-sided impulse -
            // an object that shoves things it is not touching.
            if ((a.collidesWith & b.layer) == 0 || (b.collidesWith & a.layer) == 0) continue;

            if (a.max.y < b.min.y || b.max.y < a.min.y) continue;
            if (a.max.z < b.min.z || b.max.z < a.min.z) continue;

            outPairs.emplace_back(i, j);
        }
    }
}

float PhysicsSystem::CombineRestitution(float a, float b) { return combineRestitution(a, b); }
float PhysicsSystem::CombineFriction(float a, float b) { return combineFriction(a, b); }

void PhysicsSystem::Update(entt::registry& registry, float deltaTime,
                           std::vector<Contact>* outContacts) {
    if (outContacts) outContacts->clear();
    if (deltaTime <= 0.0f) return;

    // Gravity and the ground plane come from the scene, not from a constant in
    // this file. A scene that has never heard of them gets the defaults, which
    // is why nothing had to be changed to install them.
    static const PhysicsSettings kDefaults;
    const PhysicsSettings* stored = registry.ctx().find<PhysicsSettings>();
    const PhysicsSettings& settings = stored ? *stored : kDefaults;

    // ---- Joints reach across sleep ----
    //
    // Before anything else, so a body woken here is integrated AND gathered
    // this step rather than one behind. A chain whose top link is knocked has
    // to wake all the way down, or half of it hangs frozen in mid-air while the
    // rest swings.
    //
    // The same accepted limitation the contact solver documents: waking travels
    // one link per step, because a body woken here is still motionless while
    // the rest of this loop runs and so does not itself wake its neighbour
    // until the next one. At 60Hz nobody sees a chain wake from the top down.
    {
        // An awake but equally STILL neighbour must not count, exactly as it
        // must not for a contact: two settled links either side of a joint
        // would otherwise hold each other awake forever, which is the usual way
        // a sleep implementation ends up never sleeping at all.
        const auto stirring = [](const RigidBodyComponent* rigid) {
            if (!rigid) return false;
            if (rigid->isKinematic) return true;   // moved by code the solver cannot see
            if (rigid->isSleeping) return false;
            return glm::dot(rigid->velocity, rigid->velocity) >=
                       kSleepLinearVelocity * kSleepLinearVelocity ||
                   glm::dot(rigid->angularVelocity, rigid->angularVelocity) >=
                       kSleepAngularVelocity * kSleepAngularVelocity;
        };

        for (auto entity : registry.view<JointComponent>()) {
            const auto& joint = registry.get<JointComponent>(entity);
            if (!joint.enabled) continue;
            if (joint.connectedBody == entt::null) continue;   // the world never stirs
            if (!registry.valid(joint.connectedBody)) continue;

            auto* here = registry.try_get<RigidBodyComponent>(entity);
            auto* there = registry.try_get<RigidBodyComponent>(joint.connectedBody);

            if (here && here->isSleeping && stirring(there)) {
                here->isSleeping = false;
                here->sleepTimer = 0.0f;
            }
            if (there && there->isSleeping && stirring(here)) {
                there->isSleeping = false;
                there->sleepTimer = 0.0f;
            }
        }
    }

    // ---- Integrate, and resolve against the world ground plane ----
    auto dynamics = registry.view<TransformComponent, RigidBodyComponent>();
    for (auto entity : dynamics) {
        auto& transform = dynamics.get<TransformComponent>(entity);
        auto& rigidBody = dynamics.get<RigidBodyComponent>(entity);

        // Kinematic bodies are moved by whatever owns them and never sleep.
        if (rigidBody.isKinematic) continue;

        if (rigidBody.isSleeping) {
            // A sleeping body has EXACTLY zero velocity, because that is what
            // was written when it went to sleep and nothing here has touched it
            // since. Anything non-zero was therefore written from outside - a
            // script, the inspector, the time-travel debugger - and is a request
            // to start moving again.
            const bool pushed = glm::dot(rigidBody.velocity, rigidBody.velocity) > 0.0f ||
                                glm::dot(rigidBody.angularVelocity, rigidBody.angularVelocity) > 0.0f;

            // Same argument for the transform: a sleeping body is not integrated,
            // so if it is somewhere else it was put there. Without this an editor
            // gizmo drags a settled crate into the air and it hangs.
            const glm::vec3 drift = glm::abs(transform.position - rigidBody.sleepPosition);
            const bool moved = drift.x > kSleepWakeDistance || drift.y > kSleepWakeDistance ||
                               drift.z > kSleepWakeDistance;

            if (!pushed && !moved) continue;

            rigidBody.isSleeping = false;
            rigidBody.sleepTimer = 0.0f;
        }

        // Whether it has been still long enough to stop simulating.
        //
        // Read BEFORE this step integrates, so the velocity being judged is the
        // one the previous step's solve settled on - which is the whole question
        // being asked. Done here rather than after the solve because Update
        // returns early when there are fewer than two colliders, and a lone body
        // resting on the floor is exactly the case that must still be able to
        // sleep.
        if (rigidBody.allowSleep) {
            const bool still =
                glm::dot(rigidBody.velocity, rigidBody.velocity) <
                    kSleepLinearVelocity * kSleepLinearVelocity &&
                glm::dot(rigidBody.angularVelocity, rigidBody.angularVelocity) <
                    kSleepAngularVelocity * kSleepAngularVelocity;

            if (!still) {
                rigidBody.sleepTimer = 0.0f;
            } else {
                rigidBody.sleepTimer += deltaTime;
                if (rigidBody.sleepTimer >= kSleepTime) {
                    rigidBody.isSleeping = true;
                    rigidBody.velocity = glm::vec3(0.0f);
                    rigidBody.angularVelocity = glm::vec3(0.0f);
                    rigidBody.sleepPosition = transform.position;
                    continue;
                }
            }
        } else {
            rigidBody.sleepTimer = 0.0f;
        }

        if (rigidBody.useGravity) {
            rigidBody.velocity += settings.gravity * deltaTime;
        }

        // Velocity is world space; position is relative to the parent. Without
        // the conversion a parented body drifts along its parent's axes instead
        // of falling straight down.
        const glm::mat4 parentWorld = parentWorldMatrix(registry, entity);
        const glm::mat3 worldToLocal = glm::inverse(glm::mat3(parentWorld));
        // Air resistance. Framerate-independent: a fixed multiplier per step
        // would damp twice as hard at 120Hz as at 60Hz, so the same scene
        // would behave differently on a faster machine.
        if (rigidBody.linearDamping > 0.0f) {
            rigidBody.velocity *= DetMath::pow(std::max(0.0f, 1.0f - rigidBody.linearDamping),
                                           deltaTime);
        }

        // Per-axis locks. The solver never pushes along a locked axis; this takes
        // out what gravity, damping and anything written from outside put there.
        // Skipped outright for a body with no lock, which is every body in every
        // scene written before they existed.
        if (glm::any(rigidBody.lockPosition)) rigidBody.velocity *= linearFactorOf(&rigidBody);

        if (rigidBody.freezeRotation) {
            rigidBody.angularVelocity = glm::vec3(0.0f);
        } else {
            if (glm::any(rigidBody.lockRotation)) rigidBody.angularVelocity *= angularFactorOf(&rigidBody);
            if (rigidBody.angularDamping > 0.0f) {
                rigidBody.angularVelocity *=
                    DetMath::pow(std::max(0.0f, 1.0f - rigidBody.angularDamping), deltaTime);
            }

            // Integrated as a rotation MATRIX and written back as Euler
            // angles.
            //
            // Adding the angular velocity to the Euler triple directly is only
            // correct for spin about one axis at a time: Euler rates are not
            // the angular velocity, and a body tumbling about two axes at once
            // would wander off in a way that looks like the physics is broken.
            // The transform stores Euler because that is what an inspector can
            // sensibly edit, so the conversion happens here, once per step.
            //
            // Through the transform's OWN matrix, not through glm::quat(vec3).
            // That constructor composes the three angles in the opposite order
            // from getModelMatrix, so for any orientation with more than one
            // non-zero angle it is a different rotation - 0.33 out on a matrix
            // entry for (0.5, 0.7, 0.3). Both halves of the old round trip used
            // it, so it was self-consistent and every test passed, while the
            // spin was applied about the wrong axes relative to the matrix that
            // renders and collides the body. A body turning about ONE axis has
            // one non-zero angle and the two conventions agree exactly there,
            // which is why nothing caught it until a hinged door swung past
            // ninety degrees and picked up a second.
            const float speed = glm::length(rigidBody.angularVelocity);
            if (speed > 1e-6f) {
                // The spin is in WORLD space, so it multiplies on the left.
                const glm::mat3 spin = glm::mat3_cast(
                    DetMath::angleAxis(speed * deltaTime, rigidBody.angularVelocity / speed));
                transform.rotation =
                    TransformComponent::EulerFromRotation(spin * transform.getRotationMatrix());
            }
        }

        transform.position += worldToLocal * (rigidBody.velocity * deltaTime);

        // The optional world ground plane. Off unless the scene asks for it,
        // which is the whole of the change: it used to be unconditional, so
        // nothing could fall below y = 0 whether or not the scene had a floor,
        // and there was no switch.
        if (!settings.hasGroundPlane) continue;

        // Resolved against the bottom of the collider, not the transform
        // origin. Clamping the origin buried every body by half its height and
        // made it impossible to rest anything below the plane.
        //
        // Measured in WORLD space, through the collider's world bounds. Testing
        // the local position puts the floor wherever the parent happens to be,
        // so a body parented ten units up rested in mid-air and never fell at
        // all.
        glm::vec3 localHalfExtent(0.5f);
        bool isSphere = false;
        if (const auto* box = registry.try_get<BoxColliderComponent>(entity)) {
            localHalfExtent = box->size * 0.5f;
        } else if (const auto* sphere = registry.try_get<SphereColliderComponent>(entity)) {
            localHalfExtent = glm::vec3(sphere->radius);
            isSphere = true;
        }

        const glm::mat4 world = parentWorld * transform.getModelMatrix();

        glm::vec3 centre(0.0f);
        glm::vec3 halfExtent(0.5f);
        worldBounds(world, localHalfExtent, centre, halfExtent);

        // A sphere's size comes from the SCALE, not from the bounding box of a
        // rotated transform - the same argument as the collector below, and the
        // same bug: a ball that had been turned at all rested above the plane by
        // the size of the box that contains it rather than by its own radius,
        // which for a general orientation is up to 1.73 times too high.
        if (isSphere) {
            const glm::mat3 basis(world);
            const float scale = std::max({glm::length(basis[0]), glm::length(basis[1]),
                                          glm::length(basis[2])});
            halfExtent = glm::vec3(localHalfExtent.x * scale);
        }

        const float bottom = centre.y - halfExtent.y;
        if (bottom < settings.groundPlaneY) {
            transform.position +=
                worldToLocal * glm::vec3(0.0f, settings.groundPlaneY - bottom, 0.0f);

            // Only reflect when actually moving into the plane. Inverting
            // unconditionally re-launched bodies that were already rising.
            if (rigidBody.velocity.y < 0.0f) {
                rigidBody.velocity.y = -rigidBody.velocity.y *
                    std::clamp(rigidBody.restitution, 0.0f, 0.99f);
                if (std::fabs(rigidBody.velocity.y) < kRestVelocity) {
                    rigidBody.velocity.y = 0.0f;
                }
            }
        }
    }

    // ---- Gather every collider in the scene ----
    //
    // Colliders without a rigid body are included as immovable obstacles, which
    // is how level geometry participates without being simulated. Before this,
    // BoxColliderComponent and SphereColliderComponent were stored, shown in the
    // inspector, serialised - and never tested against anything.
    std::vector<Body> bodies;
    std::vector<Proxy> proxies;

    const auto collect = [&](entt::entity entity, Shape shape,
                             const glm::vec3& localHalfExtent, bool isTrigger,
                             const glm::vec3& localCenter, uint32_t layer,
                             uint32_t collidesWith) {
        const auto* transform = registry.try_get<TransformComponent>(entity);
        if (!transform) return;

        const auto* rigid = registry.try_get<RigidBodyComponent>(entity);

        Body body;
        body.entity = entity;
        body.shape = shape;
        body.isTrigger = isTrigger;

        // A sleeping body is immovable until something wakes it. Said once, here,
        // rather than branched on at each use, so every part of the response -
        // positional correction, the constraint list, the impulses - treats it
        // as the static obstacle it is pretending to be with no further changes.
        body.inverseMass = (rigid && rigid->isSleeping)
                               ? 0.0f
                               : inverseMassOf(rigid);
        setLocks(body, rigid);

        const glm::mat4 parentWorld = parentWorldMatrix(registry, entity);
        const glm::mat4 world = parentWorld * transform->getModelMatrix();
        body.worldToLocal = glm::inverse(glm::mat3(parentWorld));

        worldBounds(world, localHalfExtent, body.centre, body.halfExtent);

        // The box's own frame. The columns of the world matrix carry rotation
        // AND scale, so the length of each is that axis's scale - which has to
        // come out into the half extent, or a scaled crate collides at its
        // unscaled size.
        for (int axis = 0; axis < 3; ++axis) {
            const glm::vec3 column = glm::vec3(world[axis]);
            const float length = glm::length(column);
            body.axes[axis] = length > 1e-6f ? column / length : glm::vec3(axis == 0, axis == 1, axis == 2);
            body.localHalfExtent[axis] = localHalfExtent[axis] * length;
        }

        // The offset is in the collider's LOCAL space, so it goes through the
        // same world matrix the extents did - rotating the entity has to swing
        // the offset with it, or a door's collider stays on the wrong side.
        // Direction only, hence the w = 0.
        if (localCenter != glm::vec3(0.0f)) {
            body.centre += glm::vec3(world * glm::vec4(localCenter, 0.0f));
        }

        if (shape == Shape::Sphere) {
            // From the SCALE, not from the world AABB.
            //
            // A sphere is the same shape whichever way it is turned. The
            // bounding box of a rotated transform is not: worldBounds sums the
            // three scaled axes' contributions to each world axis, which for a
            // rotation of 45 degrees about one axis is 1.41 times the radius and
            // for a general orientation up to 1.73.
            //
            // Read from there, a ROLLING ball grew as it rolled. Slowly,
            // invisibly, and without limit: every step the orientation changed,
            // the "radius" went up by a fraction of a millimetre, the contact
            // pushed the ball that much higher, and the ball ended up hovering
            // further and further above whatever it was rolling on. It never
            // showed because until terrain arrived nothing in the suite rolled -
            // a ball dropped straight onto a box does not turn, and its
            // orientation stays exactly identity.
            //
            // localHalfExtent already carries the per-axis scale and nothing
            // else, which is where a radius has to come from. A non-uniform
            // scale still has to collapse to one number; the largest keeps it
            // conservative, as before.
            body.radius = std::max({body.localHalfExtent.x, body.localHalfExtent.y,
                                    body.localHalfExtent.z});
            body.halfExtent = glm::vec3(body.radius);
        } else if (shape == Shape::Capsule) {
            // localHalfExtent arrives as (radius, height/2, radius), already
            // through the per-axis scale by the loop above. A capsule has one
            // radius, so the two lateral scales collapse to the larger.
            body.radius = std::max(body.localHalfExtent.x, body.localHalfExtent.z);
            const float halfHeight = std::max(body.localHalfExtent.y, body.radius);

            // The straight section. A capsule shorter than twice its radius is a
            // sphere, which falls out as a segment of zero length rather than
            // needing to be rejected.
            body.halfSegment = std::max(halfHeight - body.radius, 0.0f);

            // The exact bound of a capsule, not the bound of the box that holds
            // it: the two end spheres, swept along the axis. worldBounds gave
            // the enclosing box's AABB, which for a capsule lying diagonally is
            // noticeably larger than it needs to be.
            body.halfExtent = glm::abs(body.axes[1]) * body.halfSegment + glm::vec3(body.radius);

            // What the inertia is built from, in the capsule's own frame.
            body.localHalfExtent = glm::vec3(body.radius, halfHeight, body.radius);
        }

        body.min = body.centre - body.halfExtent;
        body.max = body.centre + body.halfExtent;

        // From the same extents the collision uses, and in the same frame.
        //
        // The tensor is built in the box's OWN axes and then rotated into the
        // world by them, which is the pair of things that has to agree with the
        // narrowphase. It did not used to: the tensor came from the world
        // AABB with no rotation applied, which was right while box-box collided
        // as its bounding box and became wrong the moment SAT started
        // colliding the box itself. A crate rotated 45 degrees about Y has an
        // AABB 1.41 times wider than it is, so it was roughly twice as hard to
        // turn as the identical crate sitting square - the same object, two
        // different masses, decided by which way it happened to be facing.
        //
        // A sphere is isotropic, so this costs it nothing either way.
        body.inverseInertia = withLocks(body, worldInverseInertia(
            inverseInertiaLocal(rigid, shape, body.localHalfExtent, body.radius),
            body.axes));

        // How far this body travels in one step. The broadphase bound is
        // expanded by it so a pair that will meet during the step is found
        // BEFORE they touch - a projectile crossing a wall between two frames is
        // never a candidate pair otherwise, and nothing downstream gets a chance
        // to stop it.
        glm::vec3 sweep(0.0f);
        if (presentsVelocity(rigid, body)) {
            sweep = glm::abs(rigid->velocity) * deltaTime;
        }
        body.sweep = sweep;

        Proxy proxy;
        proxy.entity = entity;
        proxy.index = bodies.size();
        proxy.min = body.min - sweep;
        proxy.max = body.max + sweep;
        // The TRUE mass, not the zeroed one above. The broadphase drops a pair
        // where both sides are immovable, so handing it the pretence would stop
        // a sleeping body's contacts being reported at all - every settled body
        // would fire a spurious exit at whatever is watching the contact list,
        // and a trigger volume would forget about anything that fell asleep
        // inside it. Sleeping removes the response, not the report.
        proxy.inverseMass = inverseMassOf(rigid);
        proxy.isTrigger = isTrigger;
        proxy.layer = layer;
        proxy.collidesWith = collidesWith;

        bodies.push_back(body);
        proxies.push_back(proxy);
    };

    for (auto entity : registry.view<BoxColliderComponent>()) {
        const auto& box = registry.get<BoxColliderComponent>(entity);
        collect(entity, Shape::Box, box.size * 0.5f, box.isTrigger,
                box.center, box.layer, box.collidesWith);
    }
    for (auto entity : registry.view<CapsuleColliderComponent>()) {
        if (registry.all_of<BoxColliderComponent>(entity)) continue;
        const auto& capsule = registry.get<CapsuleColliderComponent>(entity);
        collect(entity, Shape::Capsule,
                glm::vec3(capsule.radius, capsule.height * 0.5f, capsule.radius),
                capsule.isTrigger, capsule.center, capsule.layer, capsule.collidesWith);
    }
    for (auto entity : registry.view<SphereColliderComponent>()) {
        // An entity carrying more than one collider would otherwise be added
        // twice and then collide with itself. Box wins, then capsule, then
        // sphere, then heightfield - one order, stated once, and the queries
        // below repeat it.
        if (registry.any_of<BoxColliderComponent, CapsuleColliderComponent>(entity)) continue;
        const auto& sphere = registry.get<SphereColliderComponent>(entity);
        collect(entity, Shape::Sphere, glm::vec3(sphere.radius), sphere.isTrigger,
                sphere.center, sphere.layer, sphere.collidesWith);
    }

    // Hulls. Gathered on their own because the collider's SIZE is the asset's,
    // not a number on the component: everything else here is described by an
    // extent somebody typed, and a hull is described by the mesh.
    ConvexHullCache::For(registry).Trim();
    for (auto entity : registry.view<ConvexHullColliderComponent>()) {
        if (registry.any_of<BoxColliderComponent, CapsuleColliderComponent,
                            SphereColliderComponent, HeightfieldColliderComponent>(entity)) {
            continue;
        }

        const auto* transform = registry.try_get<TransformComponent>(entity);
        if (!transform) continue;

        const auto& authored = registry.get<ConvexHullColliderComponent>(entity);
        const ConvexDecomposition* hull =
            ConvexHullCache::For(registry).Get(registry, entity, authored);
        if (!hull) continue;

        const auto* rigid = registry.try_get<RigidBodyComponent>(entity);

        const glm::mat4 parentWorld = parentWorldMatrix(registry, entity);
        const glm::mat4 world = parentWorld * transform->getModelMatrix();

        Body body;
        body.entity = entity;
        body.shape = Shape::Hull;
        body.isTrigger = authored.isTrigger;
        body.pieces = hull;
        body.hullBasis = glm::mat3(world);
        body.worldToLocal = glm::inverse(glm::mat3(parentWorld));
        body.inverseMass = (rigid && rigid->isSleeping) ? 0.0f : inverseMassOf(rigid);
        setLocks(body, rigid);

        // The hull's own origin is wherever the asset had it, which is not
        // necessarily its middle - so the bounds go through the world matrix
        // the way a box collider's `center` offset does, or the broadphase bound
        // sits off to one side of the shape it is meant to hold.
        const glm::vec3 localMin = hull->boundsMin();
        const glm::vec3 localMax = hull->boundsMax();
        worldBounds(world, (localMax - localMin) * 0.5f, body.centre, body.halfExtent);
        body.centre += glm::vec3(world * glm::vec4((localMin + localMax) * 0.5f, 0.0f));

        // The inertia of the box that contains it, which over-estimates how
        // hard a hull is to turn by however much the hull is smaller than its
        // bounds. Wrong in the stable direction, and the alternative - the real
        // tensor of a polyhedron - is an integral over its tetrahedra that
        // nothing else here would use.
        for (int axis = 0; axis < 3; ++axis) {
            const glm::vec3 column = glm::vec3(world[axis]);
            const float length = glm::length(column);
            body.axes[axis] = length > 1e-6f ? column / length
                                             : glm::vec3(axis == 0, axis == 1, axis == 2);
            body.localHalfExtent[axis] = (localMax[axis] - localMin[axis]) * 0.5f * length;
        }
        body.inverseInertia = withLocks(body, worldInverseInertia(
            inverseInertiaLocal(rigid, Shape::Box, body.localHalfExtent, body.radius), body.axes));

        glm::vec3 sweep(0.0f);
        if (presentsVelocity(rigid, body)) sweep = glm::abs(rigid->velocity) * deltaTime;
        body.sweep = sweep;

        body.min = body.centre - body.halfExtent;
        body.max = body.centre + body.halfExtent;

        Proxy proxy;
        proxy.entity = entity;
        proxy.index = bodies.size();
        proxy.min = body.min - sweep;
        proxy.max = body.max + sweep;
        proxy.inverseMass = inverseMassOf(rigid);
        proxy.isTrigger = authored.isTrigger;
        proxy.layer = authored.layer;
        proxy.collidesWith = authored.collidesWith;

        bodies.push_back(body);
        proxies.push_back(proxy);
    }

    // Terrain. Gathered on its own rather than through `collect`, because
    // almost nothing it needs is what `collect` computes: the grid's bounds are
    // not centred on the entity, its mass is zero whatever the scene says, and
    // the narrowphase wants the grid's frame rather than an oriented box.
    // Before the loop, never inside it: Trim frees every grid the cache holds,
    // and the bodies below keep pointers into them for the rest of the step.
    HeightfieldCache::For(registry).Trim();

    for (auto entity : registry.view<HeightfieldColliderComponent>()) {
        if (registry.any_of<BoxColliderComponent, CapsuleColliderComponent,
                            SphereColliderComponent>(entity)) continue;

        const auto* transform = registry.try_get<TransformComponent>(entity);
        if (!transform) continue;

        const auto& terrain = registry.get<HeightfieldColliderComponent>(entity);
        const Heightfield* field = HeightfieldCache::For(registry).Get(terrain);
        if (!field) continue;

        const glm::mat4 parentWorld = parentWorldMatrix(registry, entity);
        const glm::mat4 world = parentWorld * transform->getModelMatrix();

        Body body;
        body.entity = entity;
        body.shape = Shape::Heightfield;
        body.isTrigger = terrain.isTrigger;
        body.field = field;
        body.fieldOrigin = glm::vec3(world[3]);
        body.worldToLocal = glm::inverse(glm::mat3(parentWorld));

        // Immovable whatever else is on the entity. A RigidBodyComponent
        // attached to terrain would otherwise make the ground fall, and nothing
        // about that reads as a mistake in an inspector.
        body.inverseMass = 0.0f;
        body.inverseInertia = glm::mat3(0.0f);

        float largestScale = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const glm::vec3 column = glm::vec3(world[axis]);
            const float length = glm::length(column);
            body.axes[axis] = length > 1e-6f ? column / length
                                             : glm::vec3(axis == 0, axis == 1, axis == 2);
            largestScale = std::max(largestScale, length);
        }
        body.fieldScale = largestScale > 1e-6f ? largestScale : 1.0f;

        // The grid's local bounds are NOT centred on the origin - a 64-wide
        // field spans -32 to +31 - so the offset to the middle of them goes
        // through the world matrix exactly the way a box collider's `center`
        // does. Skip it and the broadphase bound sits half a cell off, which
        // loses the pair at one edge of the terrain and invents one at the other.
        const glm::vec3 localMin = field->LocalMin();
        const glm::vec3 localMax = field->LocalMax();
        worldBounds(world, (localMax - localMin) * 0.5f, body.centre, body.halfExtent);
        body.centre += glm::vec3(world * glm::vec4((localMin + localMax) * 0.5f, 0.0f));

        body.min = body.centre - body.halfExtent;
        body.max = body.centre + body.halfExtent;

        Proxy proxy;
        proxy.entity = entity;
        proxy.index = bodies.size();
        proxy.min = body.min;
        proxy.max = body.max;
        proxy.inverseMass = 0.0f;
        proxy.isTrigger = terrain.isTrigger;
        proxy.layer = terrain.layer;
        proxy.collidesWith = terrain.collidesWith;

        bodies.push_back(body);
        proxies.push_back(proxy);
    }

    // NOT an early return any more, and this is the change joints needed.
    //
    // A pendulum is one collider on a static anchor, and a bob on a rope may
    // have no collider at all - so both "fewer than two bodies" and "no pairs
    // touching" are ordinary states for a scene that still has constraints to
    // solve. The loops below do nothing when the lists are empty, and there is
    // one combined return further down once both are built.
    std::vector<std::pair<size_t, size_t>> pairs;
    if (bodies.size() >= 2) SweepAndPrune(proxies, pairs);

    // ---- Narrowphase and response ----
    // One entry per contact POINT, not per pair.
    //
    // A face-to-face rest between two boxes is up to four points, and resolving
    // it as one is what leaves a crate balanced on a single spot in its own
    // footprint, free to rotate about it. The SAT narrowphase has been
    // producing these points since it landed; this is the first thing to
    // consume them.
    struct Constraint {
        size_t bodyA{0};
        size_t bodyB{0};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec3 armA{0.0f};
        glm::vec3 armB{0.0f};

        // Computed once, from the velocities as they were BEFORE any impulse.
        // Recomputing restitution per iteration feeds the solver its own output
        // and a resting stack slowly climbs.
        float targetVelocity{0.0f};
        float normalMass{0.0f};

        // Accumulated across iterations and clamped to stay non-negative, so
        // the contact can only ever push. Without the accumulator, N iterations
        // apply N full impulses and everything launches.
        float normalImpulse{0.0f};
        float tangentImpulse{0.0f};

        float friction{0.0f};
    };
    std::vector<Constraint> constraints;
    constraints.reserve(pairs.size() * 2);

    // Wakes `sleeper` if `other` is something that can disturb it, and does it
    // IN PLACE, restoring the mass properties that were zeroed while it slept.
    //
    // In place because one step of latency is not a small difference here: the
    // positional correction separates the pair on the step of the impact, so by
    // the next step there is no contact left and the body that was hit never
    // receives the impulse at all. A ball would bounce off a sleeping crate and
    // leave it sitting exactly where it was.
    const auto wake = [&](Body& sleeper, const Body& other) {
        auto* rigid = registry.try_get<RigidBodyComponent>(sleeper.entity);
        if (!rigid) return;

        // Static level geometry wakes nothing. Resting on the floor is the
        // reason to sleep, not a reason to stay awake.
        const auto* otherRigid = registry.try_get<RigidBodyComponent>(other.entity);
        if (!otherRigid) return;

        if (otherRigid->isKinematic) {
            // A kinematic body is moved by code the solver cannot see. The
            // contact reads the velocity its owner writes, but nothing here can
            // know that velocity is about to change - a lift stops, a platform
            // turns back - so nothing resting on one is allowed to sleep at all:
            // the timer below is reset every step the contact lasts.
        } else {
            if (otherRigid->isSleeping) return;

            // An awake but equally still neighbour must NOT count, or two crates
            // settling side by side hold each other awake forever, which is the
            // usual way a sleep implementation ends up never sleeping.
            const bool moving =
                glm::dot(otherRigid->velocity, otherRigid->velocity) >=
                    kSleepLinearVelocity * kSleepLinearVelocity ||
                glm::dot(otherRigid->angularVelocity, otherRigid->angularVelocity) >=
                    kSleepAngularVelocity * kSleepAngularVelocity;
            if (!moving) return;
        }

        rigid->sleepTimer = 0.0f;
        if (!rigid->isSleeping) return;
        rigid->isSleeping = false;

        // The same expressions collect uses, own-axes extent and orientation
        // included. Passing the world AABB extent here instead would quietly
        // give a woken body a different inertia from an identical one that
        // never slept, and nothing would ever report it.
        sleeper.inverseMass = inverseMassOf(rigid);
        sleeper.inverseInertia = withLocks(sleeper, worldInverseInertia(
            inverseInertiaLocal(rigid, sleeper.shape, sleeper.localHalfExtent, sleeper.radius),
            sleeper.axes));
    };

    for (const auto& [pi, pj] : pairs) {
        Body& a = bodies[proxies[pi].index];
        Body& b = bodies[proxies[pj].index];

        glm::vec3 normal(0.0f, 1.0f, 0.0f);
        float penetration = 0.0f;
        glm::vec3 point = (a.centre + b.centre) * 0.5f;
        bool hit = false;

        // A speculative contact is a pair that is APART but closing fast enough
        // to meet inside this step. `penetration` is negative for one, and it
        // must not be pushed apart or reported as an overlap - the bodies are
        // not touching yet.
        bool speculative = false;

        // Where this pair touches. One entry for the shapes that produce a
        // single point, up to four for a box face resting on a box face.
        glm::vec3 manifoldPoints[CollisionSAT::kMaxContactPoints];
        int manifoldCount = 0;

        // One normal per POINT, which only terrain needs. Two boxes touching
        // face to face really do share a normal, and so do a capsule's two ends
        // resting on one flat surface; a capsule lying across a ridge does not,
        // and holding both its ends to the ridge's average would push each of
        // them into the ground on its own side.
        glm::vec3 manifoldNormals[CollisionSAT::kMaxContactPoints];
        bool perPointNormals = false;

        // How far this pair can close during the step. Anything further apart
        // than this cannot meet before the next step, so reporting it would only
        // make the solver brake for something it will never reach.
        const float pairMargin = glm::length(a.sweep) + glm::length(b.sweep);

        if ((a.shape == Shape::Hull || b.shape == Shape::Hull) &&
            a.shape != Shape::Heightfield && b.shape != Shape::Heightfield) {
            // A hull against anything that is not terrain.
            //
            // An oriented box IS the unit cube hull with its half extents on the
            // basis, so box-against-hull goes through the hull path rather than
            // being a fifth pair test with its own bugs.
            const bool hullIsA = a.shape == Shape::Hull;
            const Body& hullBody = hullIsA ? a : b;
            const Body& other = hullIsA ? b : a;

            // A collider is a DECOMPOSITION now, so it can be several convex
            // pieces. Each is tested on its own and every contact is kept with
            // its own normal - which needs nothing new from the solver, because
            // terrain already needed per-point normals and has had them since
            // heightfield collision landed.
            //
            // Keeping them all rather than only the deepest piece is the
            // difference between a chair that rests on its seat AND its legs and
            // one that rocks between them a step at a time.
            //
            // Every piece shares one hull-local origin: the body centre is the
            // centre of the WHOLE decomposition bounds, so subtracting each
            // piece's own centroid instead would stack them on top of each
            // other.
            const glm::vec3 wholeCentroid =
                (hullBody.pieces->boundsMin() + hullBody.pieces->boundsMax()) * 0.5f;
            const glm::vec3 hullOrigin =
                glm::vec3(hullBody.centre) - hullBody.hullBasis * wholeCentroid;

            glm::vec3 centroidSum(0.0f);
            int totalPoints = 0;
            float deepest = -std::numeric_limits<float>::max();
            glm::vec3 deepestNormal(0.0f);
            bool anySpeculative = false;

            const auto keep = [&](const CollisionSAT::Manifold& manifold,
                                  const glm::vec3& pieceNormal) {
                anySpeculative = anySpeculative || manifold.speculative;
                for (int i = 0; i < manifold.pointCount; ++i) {
                    centroidSum += manifold.points[i].position;
                    ++totalPoints;
                    if (manifold.points[i].penetration > deepest) {
                        deepest = manifold.points[i].penetration;
                        deepestNormal = pieceNormal;
                    }
                    if (manifoldCount < CollisionSAT::kMaxContactPoints) {
                        manifoldPoints[manifoldCount] = manifold.points[i].position;
                        manifoldNormals[manifoldCount] = pieceNormal;
                        ++manifoldCount;
                    }
                }
            };

            for (const ConvexHull& piece : hullBody.pieces->pieces()) {
                CollisionHull::Instance hullInstance;
                if (!CollisionHull::MakeInstance(piece, hullOrigin, hullBody.hullBasis,
                                                 hullInstance)) {
                    continue;
                }

                if (other.shape == Shape::Hull) {
                    // Both sides can be several pieces, so the inner loop is the
                    // other body's. Quadratic in the piece counts, which is what
                    // the cap in ConvexDecomposition bounds.
                    const glm::vec3 otherCentroid =
                        (other.pieces->boundsMin() + other.pieces->boundsMax()) * 0.5f;
                    const glm::vec3 otherOrigin =
                        glm::vec3(other.centre) - other.hullBasis * otherCentroid;

                    for (const ConvexHull& otherPiece : other.pieces->pieces()) {
                        CollisionHull::Instance otherInstance;
                        if (!CollisionHull::MakeInstance(otherPiece, otherOrigin, other.hullBasis,
                                                         otherInstance)) {
                            continue;
                        }
                        const CollisionSAT::Manifold pairManifold =
                            CollisionHull::CollideHullHull(hullInstance, otherInstance, pairMargin);
                        if (!pairManifold.colliding || pairManifold.pointCount == 0) continue;

                        // From a toward b: the manifold speaks from the first
                        // hull, so it is already right when the hull is a.
                        keep(pairManifold, hullIsA ? pairManifold.normal : -pairManifold.normal);
                    }
                    continue;
                }

                CollisionSAT::Manifold manifold;
                bool normalPointsFromHull = true;

                if (other.shape == Shape::Box) {
                    manifold = CollisionHull::CollideHullHull(
                        hullInstance, CollisionHull::InstanceFromObb(obbOf(other)), pairMargin);
                } else {
                    glm::vec3 endA(0.0f);
                    glm::vec3 endB(0.0f);
                    capsuleEnds(other, endA, endB);
                    manifold = CollisionHull::CollideCapsuleHull(endA, endB, other.radius,
                                                                 hullInstance, pairMargin);
                    // That one reports from the HULL toward the round shape,
                    // which is what the hull-hull path above already does.
                    normalPointsFromHull = true;
                }

                if (!manifold.colliding || manifold.pointCount == 0) continue;

                glm::vec3 pieceNormal = manifold.normal;
                if (!hullIsA || !normalPointsFromHull) pieceNormal = -pieceNormal;
                keep(manifold, pieceNormal);
            }

            if (totalPoints > 0) {
                hit = true;
                // One normal per contact, filled above, because two pieces of
                // the same collider can face different ways.
                perPointNormals = true;
                normal = deepestNormal;
                penetration = deepest;
                speculative = anySpeculative;
                point = centroidSum / static_cast<float>(totalPoints);
            }
        } else if (a.shape == Shape::Heightfield || b.shape == Shape::Heightfield) {
            // Two of them never collide: both are immovable surfaces, so there
            // is nothing a contact between them could do to either.
            if (a.shape == Shape::Heightfield && b.shape == Shape::Heightfield) continue;

            const Body& terrain = (a.shape == Shape::Heightfield) ? a : b;
            const Body& shape = (a.shape == Shape::Heightfield) ? b : a;
            if (!terrain.field) continue;

            // Into the grid's own space, where a cell is one unit across and
            // the surface is the function Heightfield knows how to answer for.
            // The axes are orthonormal, so the transpose is the inverse.
            const glm::mat3 intoField = glm::transpose(terrain.axes);
            const float scale = terrain.fieldScale;
            const float inverseScale = 1.0f / scale;
            const auto toField = [&](const glm::vec3& position) {
                return intoField * (position - terrain.fieldOrigin) * inverseScale;
            };

            Heightfield::Manifold local;
            if (shape.shape == Shape::Hull) {
                // Every vertex of the hull against the surface, and every vertex
                // of the surface against the hull - the same two halves the box
                // path runs, with the corners read from the hull instead of
                // counted out.
                //
                // This used to collide as the BOX containing the hull, so a
                // wedge on a hill rested on a corner of something nobody could
                // see and floated by the gap between the two.
                //
                // Into the grid's space like every other shape here. A hull's
                // basis carries rotation and scale together, so the field
                // transform composes onto it rather than needing a case of its
                // own - which is exactly why the basis was built that way.
                const glm::vec3 worldOrigin =
                    glm::vec3(shape.centre) -
                    shape.hullBasis *
                        ((shape.pieces->boundsMin() + shape.pieces->boundsMax()) * 0.5f);

                // Every piece against the surface, merged into one manifold.
                // Manifold::Add already keeps the deepest points, so a collider
                // in several parts is held by the parts that are actually
                // touching rather than by whichever was tested first.
                for (const ConvexHull& piece : shape.pieces->pieces()) {
                    CollisionHull::Instance instance;
                    if (!CollisionHull::MakeInstance(piece, toField(worldOrigin),
                                                     intoField * shape.hullBasis * inverseScale,
                                                     instance)) {
                        continue;
                    }
                    const Heightfield::Manifold pieceManifold =
                        terrain.field->CollideHull(instance, pairMargin * inverseScale);
                    for (int i = 0; i < pieceManifold.count; ++i) {
                        local.Add(pieceManifold.points[i]);
                    }
                    // Speculative only while NOTHING is really touching: one
                    // piece in contact makes the whole collider in contact.
                    if (local.count == 0 || !pieceManifold.speculative) {
                        local.speculative = local.speculative && pieceManifold.speculative;
                    }
                }
            } else if (shape.shape == Shape::Box) {
                CollisionSAT::Obb box;
                box.centre = toField(shape.centre);
                box.halfExtent = shape.localHalfExtent * inverseScale;
                box.axes = intoField * shape.axes;
                local = terrain.field->CollideObb(box, pairMargin * inverseScale);
            } else {
                // A sphere is a capsule with no length, exactly as everywhere
                // else here, so this is both round shapes at once.
                glm::vec3 endA(0.0f);
                glm::vec3 endB(0.0f);
                capsuleEnds(shape, endA, endB);
                local = terrain.field->CollideCapsule(toField(endA), toField(endB),
                                                      shape.radius * inverseScale,
                                                      pairMargin * inverseScale);
            }

            if (local.count > 0) {
                hit = true;
                speculative = local.speculative;
                perPointNormals = true;

                // The contact normal comes out of the SURFACE toward the shape,
                // which is a-toward-b when the terrain is a and the reverse
                // when it is b.
                const float facing = (a.shape == Shape::Heightfield) ? 1.0f : -1.0f;

                glm::vec3 centroid(0.0f);
                float deepest = -std::numeric_limits<float>::max();
                for (int i = 0; i < local.count &&
                                manifoldCount < CollisionSAT::kMaxContactPoints; ++i) {
                    const glm::vec3 worldPoint =
                        terrain.fieldOrigin + terrain.axes * (local.points[i].position * scale);

                    manifoldNormals[manifoldCount] = terrain.axes * local.points[i].normal * facing;
                    manifoldPoints[manifoldCount] = worldPoint;
                    ++manifoldCount;

                    centroid += worldPoint;

                    // Same split as box-box: the deepest point says how far to
                    // push and along what, the centroid says where.
                    const float depth = local.points[i].penetration * scale;
                    if (depth > deepest) {
                        deepest = depth;
                        normal = manifoldNormals[manifoldCount - 1];
                    }
                }
                penetration = deepest;
                point = centroid / static_cast<float>(manifoldCount);
            }
        } else if (a.shape == Shape::Box && b.shape == Shape::Box) {
            // SAT over fifteen axes, against the boxes' own frames rather than
            // their world AABBs. The manifold can carry up to four points; the
            // solver still resolves one, so the deepest is used - the extra
            // points are what a warm-started multi-iteration solver will need,
            // and generating them now keeps that change to the solver alone.
            const CollisionSAT::Manifold manifold =
                CollisionSAT::CollideObbObb(obbOf(a), obbOf(b), pairMargin);
            if (manifold.colliding && manifold.pointCount > 0) {
                hit = true;
                normal = manifold.normal;

                // The CENTROID of the manifold, with the DEEPEST penetration.
                //
                // Two different questions. How far to push is set by the worst
                // point, or the deepest corner stays inside. Where to push is
                // the centre of the touching region, because the solver still
                // applies one impulse per pair: at a corner that impulse is a
                // torque about the centre of mass, and a crate resting flat
                // spins up out of nothing. Measured at 0.32 rad/s before this,
                // against a test that allows 0.05.
                //
                // Averaging is what a four-point manifold would achieve anyway
                // once the solver can apply all four with accumulated impulses.
                // Until then this is the same answer arrived at more cheaply,
                // and it is why the manifold's extra points are generated but
                // not yet consumed.
                glm::vec3 centroid(0.0f);
                float deepest = manifold.points[0].penetration;
                for (int i = 0; i < manifold.pointCount; ++i) {
                    centroid += manifold.points[i].position;
                    deepest = std::max(deepest, manifold.points[i].penetration);
                }
                penetration = deepest;
                point = centroid / static_cast<float>(manifold.pointCount);
                speculative = manifold.speculative;

                // Every point becomes its own velocity constraint. The centroid
                // above is still what the positional correction uses, because
                // pushing out once per point would move the body four times as
                // far as the overlap requires.
                for (int i = 0; i < manifold.pointCount; ++i) {
                    manifoldPoints[manifoldCount++] = manifold.points[i].position;
                    if (manifoldCount >= CollisionSAT::kMaxContactPoints) break;
                }
            }
        } else if (a.shape == Shape::Box || b.shape == Shape::Box) {
            // One box and one round thing. Solved with the box as the reference
            // and the normal flipped when the box is b, so there is one
            // implementation rather than two that can disagree about which face
            // a corner belongs to.
            const Body& box = (a.shape == Shape::Box) ? a : b;
            const Body& round = (a.shape == Shape::Box) ? b : a;

            glm::vec3 roundA(0.0f);
            glm::vec3 roundB(0.0f);
            capsuleEnds(round, roundA, roundB);

            const CollisionSAT::Manifold manifold = CollisionSAT::CollideCapsuleObb(
                roundA, roundB, round.radius, obbOf(box), pairMargin);

            if (manifold.colliding && manifold.pointCount > 0) {
                hit = true;
                normal = manifold.normal;
                speculative = manifold.speculative;

                // Same split as box-box: the deepest point says how far to push,
                // the centroid says where. A capsule lying along a surface is
                // held by both its ends, and one impulse at the average of them
                // is what stops it rocking about a single spot in between.
                glm::vec3 centroid(0.0f);
                float deepest = manifold.points[0].penetration;
                for (int i = 0; i < manifold.pointCount; ++i) {
                    centroid += manifold.points[i].position;
                    deepest = std::max(deepest, manifold.points[i].penetration);
                    manifoldPoints[manifoldCount++] = manifold.points[i].position;
                }
                penetration = deepest;
                point = centroid / static_cast<float>(manifold.pointCount);

                if (a.shape != Shape::Box) normal = -normal;
            }
        } else {
            // Two round things. A sphere is a capsule with no length, so this is
            // sphere-sphere, capsule-sphere and capsule-capsule at once.
            glm::vec3 firstA(0.0f);
            glm::vec3 firstB(0.0f);
            glm::vec3 secondA(0.0f);
            glm::vec3 secondB(0.0f);
            capsuleEnds(a, firstA, firstB);
            capsuleEnds(b, secondA, secondB);

            hit = CollisionSAT::CollideCapsuleCapsule(firstA, firstB, a.radius,
                                                      secondA, secondB, b.radius,
                                                      normal, penetration, point, pairMargin);
            speculative = hit && penetration < 0.0f;
        }

        if (!hit) continue;

        const bool isTrigger = a.isTrigger || b.isTrigger;

        // Only real overlaps are reported. A speculative pair has not touched,
        // so announcing it would fire a trigger volume for something that is
        // still on its way and may yet be stopped by something else.
        if (outContacts && !speculative) {
            outContacts->push_back(Contact{a.entity, b.entity, normal, penetration, isTrigger});
        }

        // A trigger reports the overlap and lets the body pass through, which is
        // the entire point of marking a collider as one.
        if (isTrigger) continue;

        // Before anything reads the masses, because waking restores them. A body
        // woken here is dynamic for THIS step's solve.
        //
        // Accepted limitation: a body woken this way still has zero velocity
        // while this step's pairs are being walked, so it does not itself wake
        // ITS sleeping neighbours until the next step. A toppled stack therefore
        // wakes one layer per step rather than all at once, which at 60Hz is not
        // something anyone sees.
        wake(a, b);
        wake(b, a);

        const float inverseSum = a.inverseMass + b.inverseMass;
        if (inverseSum <= 0.0f) continue;

        auto* transformA = registry.try_get<TransformComponent>(a.entity);
        auto* transformB = registry.try_get<TransformComponent>(b.entity);
        if (!transformA || !transformB) continue;

        // Positional correction, shared out by inverse mass so the heavier body
        // moves less and an immovable one does not move at all.
        // Never for a speculative contact: there is nothing to push out of.
        //
        // A locked axis takes no share: each body presents its inverse mass ALONG
        // the normal, and its push is masked by its factor - position and the
        // cached centre both, or the bounds disagree with the body.
        const float alongSum = linearInverseMass(a, normal) + linearInverseMass(b, normal);
        const float correctable =
            (speculative || alongSum <= 0.0f) ? 0.0f : std::max(penetration - kSlop, 0.0f);
        if (correctable > 0.0f) {
            const glm::vec3 push = normal * (correctable * kCorrection / alongSum);
            transformA->position -= a.worldToLocal * (push * a.inverseMass * a.linearFactor);
            transformB->position += b.worldToLocal * (push * b.inverseMass * b.linearFactor);

            // Keep the cached bounds honest for the pairs still to be resolved
            // this step, or a body wedged between two others gets pushed twice
            // as far as it should be.
            const glm::vec3 shiftA = -normal * (correctable * kCorrection / alongSum) * a.inverseMass * a.linearFactor;
            const glm::vec3 shiftB = normal * (correctable * kCorrection / alongSum) * b.inverseMass * b.linearFactor;
            a.centre += shiftA;
            b.centre += shiftB;
        }

        auto* rigidA = registry.try_get<RigidBodyComponent>(a.entity);
        auto* rigidB = registry.try_get<RigidBodyComponent>(b.entity);

        // Shapes other than box-box report one point; the box path filled the
        // array above.
        if (manifoldCount == 0) {
            manifoldPoints[0] = point;
            manifoldCount = 1;
        }

        // Every path but terrain's produces one normal for the whole manifold.
        if (!perPointNormals) {
            for (int i = 0; i < manifoldCount; ++i) manifoldNormals[i] = normal;
        }

        const float bounceA = rigidA ? rigidA->restitution : kRestitution;
        const float bounceB = rigidB ? rigidB->restitution : kRestitution;
        const float gripA = rigidA ? rigidA->friction : kFriction;
        const float gripB = rigidB ? rigidB->friction : kFriction;
        const float grip = combineFriction(gripA, gripB);

        // A speculative pair is apart; the allowance is how fast it may still
        // close without going through.
        const float allowedApproach = speculative ? (-penetration / deltaTime) : 0.0f;

        for (int pointIndex = 0; pointIndex < manifoldCount; ++pointIndex) {
            Constraint constraint;
            constraint.bodyA = proxies[pi].index;
            constraint.bodyB = proxies[pj].index;
            constraint.normal = manifoldNormals[pointIndex];
            constraint.armA = manifoldPoints[pointIndex] - a.centre;
            constraint.armB = manifoldPoints[pointIndex] - b.centre;
            constraint.friction = grip;

            // Effective mass along the normal, including how hard each body is
            // to turn about this contact. The linear term alone applies an
            // impulse far too large for a glancing hit near a corner.
            const glm::vec3 angularA = glm::cross(
                a.inverseInertia * glm::cross(constraint.armA, constraint.normal), constraint.armA);
            const glm::vec3 angularB = glm::cross(
                b.inverseInertia * glm::cross(constraint.armB, constraint.normal), constraint.armB);
            const float effectiveMass = linearInverseMass(a, constraint.normal) +
                                        linearInverseMass(b, constraint.normal) +
                                        glm::dot(angularA + angularB, constraint.normal);
            if (effectiveMass <= 1e-9f) continue;
            constraint.normalMass = effectiveMass;

            // Restitution from the velocities as they are NOW, before any
            // impulse. Recomputing it inside the iteration would feed the
            // solver its own output, and a resting stack climbs.
            const bool surfaceA = presentsVelocity(rigidA, a);
            const bool surfaceB = presentsVelocity(rigidB, b);
            const glm::vec3 spinA = surfaceA ? rigidA->angularVelocity : glm::vec3(0.0f);
            const glm::vec3 spinB = surfaceB ? rigidB->angularVelocity : glm::vec3(0.0f);
            const glm::vec3 velocityA =
                (surfaceA ? rigidA->velocity : glm::vec3(0.0f))
                + glm::cross(spinA, constraint.armA);
            const glm::vec3 velocityB =
                (surfaceB ? rigidB->velocity : glm::vec3(0.0f))
                + glm::cross(spinB, constraint.armB);
            const float alongNormal = glm::dot(velocityB - velocityA, constraint.normal);

            // Bounce dies out near rest, or a settling box jitters forever.
            const float restitution = (std::abs(alongNormal) < kRestVelocity)
                                    ? 0.0f
                                    : combineRestitution(bounceA, bounceB);

            // The bounce says how fast the body should be leaving; the
            // allowance says it may still approach fast enough to close the
            // remaining gap. The larger wins, so restitution decides when there
            // is a bounce and the allowance only decides anything when there is
            // not.
            constraint.targetVelocity = std::max(-restitution * alongNormal, -allowedApproach);

            constraints.push_back(constraint);
        }
    }

    // ---- Joints -------------------------------------------------------------
    //
    // Built after the contacts, because the contact correction has already
    // moved bodies this step and a joint has to measure where things ACTUALLY
    // are rather than where they were when the step began.
    //
    // A joint end may be an entity with no collider at all - an anchor, a bob
    // on a rope - so this cannot simply index into `bodies`. Where the entity
    // IS in there, its mass properties are taken from there rather than worked
    // out again: two different answers for one body's inertia is exactly the
    // kind of drift this engine keeps stamping out.
    struct JointEnd {
        TransformComponent* transform{nullptr};
        RigidBodyComponent* rigid{nullptr};
        glm::mat3 worldToLocal{1.0f};
        glm::mat3 basis{1.0f};        // world rotation AND scale

        // Kept so the position sweep can re-derive where the body is after
        // another joint has moved it, without re-walking the hierarchy.
        glm::mat4 parentWorld{1.0f};

        Joints::Body state;
        bool usable{false};
    };

    struct JointRuntime {
        Joints::Constraint constraint;
        JointEnd a;
        JointEnd b;
        float stiffness{0.8f};

        // Carried so the break check after the solve knows what to compare
        // against and what to mark.
        entt::entity owner{entt::null};
        float breakForce{0.0f};
        float breakTorque{0.0f};
        int32_t solveOrder{0};
    };

    std::vector<JointRuntime> joints;

    {
        std::unordered_map<entt::entity, size_t> byEntity;
        byEntity.reserve(bodies.size());
        for (size_t i = 0; i < bodies.size(); ++i) byEntity.emplace(bodies[i].entity, i);

        const auto makeEnd = [&](entt::entity entity) {
            JointEnd end;
            if (entity == entt::null || !registry.valid(entity)) return end;

            auto* transform = registry.try_get<TransformComponent>(entity);
            if (!transform) return end;

            end.transform = transform;
            end.rigid = registry.try_get<RigidBodyComponent>(entity);

            end.parentWorld = parentWorldMatrix(registry, entity);
            const glm::mat4 world = end.parentWorld * transform->getModelMatrix();
            end.worldToLocal = glm::inverse(glm::mat3(end.parentWorld));
            end.basis = glm::mat3(world);
            end.state.position = glm::vec3(world[3]);

            const auto found = byEntity.find(entity);
            if (found != byEntity.end()) {
                end.state.inverseMass = bodies[found->second].inverseMass;
                end.state.inverseInertia = bodies[found->second].inverseInertia;
                end.state.linearFactor = bodies[found->second].linearFactor;
                end.state.hasLinearLock = bodies[found->second].hasLinearLock;
                end.state.hasAngularLock = bodies[found->second].hasAngularLock;
            } else {
                end.state.inverseMass = (end.rigid && end.rigid->isSleeping)
                                            ? 0.0f
                                            : inverseMassOf(end.rigid);
                end.state.linearFactor = linearFactorOf(end.rigid);
                end.state.hasLinearLock = end.rigid && glm::any(end.rigid->lockPosition);
                // No collider means no extent, and no extent means no lever for
                // a torque to act on. Zero inverse inertia is the convention the
                // contact solver already uses for anything that must not turn.
                end.state.inverseInertia = glm::mat3(0.0f);
            }

            end.usable = true;
            return end;
        };

        for (auto entity : registry.view<JointComponent>()) {
            const auto& authored = registry.get<JointComponent>(entity);
            if (!authored.enabled) continue;
            // A joint that has already let go stays let go until something
            // clears the flag - a script, the inspector, or reloading the
            // scene, which does not carry it.
            if (authored.broken) continue;

            // A joint to itself has no two bodies to hold apart, and every
            // effective mass it produces is singular.
            if (authored.connectedBody == entity) continue;

            JointRuntime runtime;
            runtime.a = makeEnd(entity);
            if (!runtime.a.usable) continue;

            if (authored.connectedBody == entt::null) {
                // Anchored to a fixed point in the WORLD. The far end is an
                // immovable body sitting exactly on it, which is what makes a
                // pendulum one entity rather than two.
                runtime.b.state.position = authored.connectedAnchor;
                runtime.b.usable = true;
            } else {
                runtime.b = makeEnd(authored.connectedBody);
                // A joint pointing at an entity that has been destroyed, or one
                // that never had a transform, is ignored rather than crashed
                // on: a scene outlives the things it references.
                if (!runtime.b.usable) continue;
            }

            // Neither end can be moved, so the joint can never do anything and
            // every matrix it builds is singular.
            if (runtime.a.state.inverseMass <= 0.0f && runtime.b.state.inverseMass <= 0.0f &&
                glm::determinant(runtime.a.state.inverseInertia) == 0.0f &&
                glm::determinant(runtime.b.state.inverseInertia) == 0.0f) {
                continue;
            }

            Joints::Constraint& constraint = runtime.constraint;
            switch (authored.type) {
            case JointComponent::Type::Distance: constraint.type = Joints::Type::Distance; break;
            case JointComponent::Type::Hinge:    constraint.type = Joints::Type::Hinge; break;
            case JointComponent::Type::Weld:     constraint.type = Joints::Type::Weld; break;
            case JointComponent::Type::Point:    constraint.type = Joints::Type::Point; break;
            }

            // Through the full basis, scale included: an anchor is a point ON
            // the object, so scaling the object has to move it. Direction only,
            // hence no translation - the arm is an offset from the centre.
            constraint.armA = runtime.a.basis * authored.anchor;
            constraint.armB = (authored.connectedBody == entt::null)
                                  ? glm::vec3(0.0f)
                                  : runtime.b.basis * authored.connectedAnchor;

            constraint.distance = std::max(authored.distance, 0.0f);
            constraint.rope = authored.rope;

            const glm::vec3 axis = runtime.a.basis * authored.axis;
            const float axisLength = glm::length(axis);
            // An axis of zero length has no hinge in it. Straight up is the
            // only answer that leaves a door hanging the way it was built.
            constraint.axisA = axisLength > 1e-6f ? axis / axisLength : glm::vec3(0.0f, 1.0f, 0.0f);

            // The other end's axis, in world space. For a world anchor it is
            // already there; for a body it comes out through that body's basis,
            // so the two turn independently and the constraint is the
            // difference between them.
            const glm::vec3 other = (authored.connectedBody == entt::null)
                                        ? authored.connectedAxis
                                        : runtime.b.basis * authored.connectedAxis;
            const float otherLength = glm::length(other);
            constraint.axisB = otherLength > 1e-6f ? other / otherLength : constraint.axisA;

            // The reference directions the hinge angle is measured between.
            //
            // Derived from each end's LOCAL axis and then taken into the world,
            // so each rotates with its own body. Deriving them from the world
            // axes instead would give directions that wander as the solver
            // nudges the axes, and the angle - and with it the limit - would
            // wander too.
            constraint.referenceA =
                glm::normalize(runtime.a.basis * Joints::PerpendicularTo(authored.axis));
            const glm::vec3 localReferenceB = Joints::PerpendicularTo(authored.connectedAxis);
            constraint.referenceB =
                (authored.connectedBody == entt::null)
                    ? localReferenceB
                    : glm::normalize(runtime.b.basis * localReferenceB);

            constraint.useLimit = authored.useLimit;
            // Ordered, so a min above a max is an empty range that traps the
            // door between two stops rather than a range that means nothing.
            constraint.minAngle = std::min(authored.minAngle, authored.maxAngle);
            constraint.maxAngle = std::max(authored.minAngle, authored.maxAngle);

            constraint.useMotor = authored.useMotor;
            constraint.motorSpeed = authored.motorSpeed;
            // A torque becomes an impulse here, because the step is something
            // only this side knows. Negative is treated as zero rather than as
            // a motor that pulls the other way.
            constraint.maxMotorImpulse = std::max(authored.maxMotorTorque, 0.0f) * deltaTime;

            // The soft constraint, turned from what an author can reason about -
            // a frequency and a damping ratio - into the three numbers the
            // solver multiplies by. The step is in here, which is why this is
            // computed on this side rather than in Joints.
            //
            //   a1 = 2*zeta + h*omega        a2 = h*omega*a1        a3 = 1/(1+a2)
            //
            // This is constraint-force mixing written so gamma never has to be
            // materialised: 1/(K + gamma) reduces to m_eff * a2 * a3 and
            // gamma/(K + gamma) reduces to a3, so the solver needs a scale on
            // the mass and a scale on the accumulated impulse and nothing else.
            //
            // Both scales are mass-free, which is the point of authoring
            // (frequency, ratio): the same pair behaves the same on a light
            // door and a heavy one.
            constraint.useSpring = authored.useSpring;
            constraint.springRestAngle = authored.springRestAngle;
            {
                const float omega = 6.2831853f * std::max(authored.springFrequency, 0.0f);
                const float zeta = std::max(authored.springDamping, 0.0f);
                const float a1 = 2.0f * zeta + deltaTime * omega;

                if (omega > 0.0f && a1 > 0.0f) {
                    const float a2 = deltaTime * omega * a1;
                    const float a3 = 1.0f / (1.0f + a2);
                    constraint.springBiasRate = omega / a1;
                    constraint.springMassScale = a2 * a3;
                    constraint.springImpulseScale = a3;
                } else {
                    // Zero frequency means RIGID, not absent: these are the
                    // values that make solveSpring's expression reduce to
                    // -Cdot / mass exactly. A spring authored at 0 Hz holds the
                    // angle it is at, which is the honest reading of "infinitely
                    // stiff" and keeps the collapse to the existing float path
                    // something a test can assert.
                    constraint.springBiasRate = 0.0f;
                    constraint.springMassScale = 1.0f;
                    constraint.springImpulseScale = 0.0f;
                }
            }

            runtime.stiffness = std::clamp(authored.stiffness, 0.0f, 1.0f);
            // The angular half is corrected through the velocity solver rather
            // than by moving anything, so its share of the error arrives as a
            // rate. See JointComponent for why the two halves differ.
            constraint.angularBias = runtime.stiffness / deltaTime;

            runtime.owner = entity;
            runtime.breakForce = std::max(authored.breakForce, 0.0f);
            runtime.breakTorque = std::max(authored.breakTorque, 0.0f);

            runtime.solveOrder = authored.solveOrder;
            joints.push_back(runtime);
        }
    }

    // The order the author asked for, applied ONCE and before both sweeps.
    //
    // Both loops below are Gauss-Seidel over this vector, so the order is the
    // answer, not a detail of it. Sorting between them would let the position
    // sweep and the velocity sweep disagree, which is worse than either order
    // on its own.
    //
    // Stable, so joints that share a solveOrder keep the order the entity pool
    // gave them - which is what makes every scene written before this field
    // existed solve exactly as it did.
    std::stable_sort(joints.begin(), joints.end(),
                     [](const JointRuntime& left, const JointRuntime& right) {
                         return left.solveOrder < right.solveOrder;
                     });

    // The positional half, exactly where a contact's happens and for the same
    // reason: positions are integrated BEFORE this solve, so a velocity change
    // alone cannot take out the error this step introduced, and a hanging body
    // would settle a centimetre below where it belongs.
    //
    // Swept several times, and re-reading the transform at the top of each
    // joint rather than trusting the copy taken when the list was built. That
    // is the difference between a Jacobi sweep and a Gauss-Seidel one, and on a
    // chain it is the difference between a rope that hangs at its length and
    // one that hangs three per cent longer: link five has to see where link
    // four has just been put, not where it was at the start of the step.
    // Turns one end of a joint about a world axis, writing the result back
    // through the transform's OWN Euler convention, and returns the rotation it
    // applied so the caller can carry the joint's cached directions with it.
    //
    // World-space, so the spin multiplies on the LEFT - and, like the rotation
    // integrator it mirrors, it writes a world rotation into a parent-local
    // triple. For an unparented body those are the same thing; for a parented
    // one it is the same approximation the integrator has always made, and
    // fixing it belongs there rather than here.
    const auto turnBack = [](JointEnd& end, const glm::vec3& axis, float radians) {
        if (!end.transform || std::fabs(radians) < 1.0e-6f) return glm::mat3(1.0f);

        const glm::mat3 spin = glm::mat3_cast(DetMath::angleAxis(radians, axis));
        end.transform->rotation =
            TransformComponent::EulerFromRotation(spin * end.transform->getRotationMatrix());
        end.basis = spin * end.basis;
        return spin;
    };

    if (!joints.empty()) {
        for (int pass = 0; pass < kJointPositionIterations; ++pass) {
            for (JointRuntime& joint : joints) {
                // Where the bodies ACTUALLY are now. The pass above may have
                // moved either of them, and on the first pass this is what the
                // list was built with anyway.
                //
                // The ARMS are not re-derived and do not need to be: they are
                // offsets from each centre and depend on the body's rotation,
                // which nothing in this sweep changes. Anything that adds an
                // angular positional correction has to refresh them here.
                if (joint.a.transform) {
                    joint.a.state.position = glm::vec3(
                        (joint.a.parentWorld * joint.a.transform->getModelMatrix())[3]);
                }
                if (joint.b.transform) {
                    joint.b.state.position = glm::vec3(
                        (joint.b.parentWorld * joint.b.transform->getModelMatrix())[3]);
                }

                // A hinge past one of its stops is turned back, which is the
                // angular twin of the shift below. It only became writable once
                // EulerFromRotation existed: putting a rotation back into a
                // transform means going through its Euler triple, and until
                // that could be done in the convention getModelMatrix reads,
                // doing it would have corrupted the orientation.
                //
                // Split by how hard each end is to turn ABOUT THE AXIS, so a
                // door hinged to the world takes all of it and a hinge between
                // two crates shares it the way their inertias say.
                const float overshoot = Joints::LimitOvershoot(joint.constraint);
                if (std::fabs(overshoot) > 1.0e-5f) {
                    const glm::vec3 axis = joint.constraint.axisA;
                    const float aboutA = glm::dot(axis, joint.a.state.inverseInertia * axis);
                    const float aboutB = glm::dot(axis, joint.b.state.inverseInertia * axis);
                    const float total = aboutA + aboutB;

                    if (total > 1e-9f) {
                        const float corrected = overshoot * joint.stiffness;

                        // The joint's cached directions have to travel with the
                        // bodies, or the next pass measures the angle from
                        // where they used to be and corrects the same overshoot
                        // four times over.
                        const glm::mat3 spunA =
                            turnBack(joint.a, axis, corrected * (aboutA / total));
                        joint.constraint.referenceA = spunA * joint.constraint.referenceA;
                        joint.constraint.axisA = spunA * joint.constraint.axisA;

                        const glm::mat3 spunB =
                            turnBack(joint.b, axis, -corrected * (aboutB / total));
                        joint.constraint.referenceB = spunB * joint.constraint.referenceB;
                        joint.constraint.axisB = spunB * joint.constraint.axisB;
                    }
                }

                glm::vec3 shiftA(0.0f);
                glm::vec3 shiftB(0.0f);
                if (!Joints::SolvePosition(joint.constraint, joint.a.state, joint.b.state,
                                           joint.stiffness, shiftA, shiftB)) {
                    continue;
                }

                // Velocity is world space; position is relative to the parent.
                // Without the conversion a parented body is corrected along its
                // parent's axes instead of the world's.
                if (joint.a.transform) {
                    joint.a.transform->position += joint.a.worldToLocal * shiftA;
                    joint.a.state.position += shiftA;
                }
                if (joint.b.transform) {
                    joint.b.transform->position += joint.b.worldToLocal * shiftB;
                    joint.b.state.position += shiftB;
                }
            }
        }
    }

    if (constraints.empty() && joints.empty()) return;

    // ---- Velocity solve ----------------------------------------------------
    //
    // Sequential impulses. Each pass revisits every contact and applies only
    // the CHANGE needed to satisfy it, with the total per contact accumulated
    // and clamped so it can never pull. Without the accumulator, eight passes
    // apply eight full impulses and the scene launches; without the passes,
    // every contact is resolved as though it were alone and a stack sinks.
    for (int iteration = 0; iteration < kSolverIterations; ++iteration) {
        for (auto& constraint : constraints) {
            Body& bodyA = bodies[constraint.bodyA];
            Body& bodyB = bodies[constraint.bodyB];

            auto* rigidA = registry.try_get<RigidBodyComponent>(bodyA.entity);
            auto* rigidB = registry.try_get<RigidBodyComponent>(bodyB.entity);

            const bool movableA = rigidA && bodyA.inverseMass > 0.0f;
            const bool movableB = rigidB && bodyB.inverseMass > 0.0f;
            if (!movableA && !movableB) continue;

            // Read from whatever moves, pushed only where the solver moves it:
            // a kinematic body's velocity is part of the contact, and nothing
            // below writes to one.
            const bool surfaceA = presentsVelocity(rigidA, bodyA);
            const bool surfaceB = presentsVelocity(rigidB, bodyB);
            const glm::vec3 spinA = surfaceA ? rigidA->angularVelocity : glm::vec3(0.0f);
            const glm::vec3 spinB = surfaceB ? rigidB->angularVelocity : glm::vec3(0.0f);
            const glm::vec3 velocityA =
                (surfaceA ? rigidA->velocity : glm::vec3(0.0f)) + glm::cross(spinA, constraint.armA);
            const glm::vec3 velocityB =
                (surfaceB ? rigidB->velocity : glm::vec3(0.0f)) + glm::cross(spinB, constraint.armB);

            const glm::vec3 relative = velocityB - velocityA;

            // ---- Normal ----
            const float alongNormal = glm::dot(relative, constraint.normal);
            float lambda = (constraint.targetVelocity - alongNormal) / constraint.normalMass;

            // Clamp the ACCUMULATED impulse, not this pass's change. A contact
            // may pull during one pass as long as the total stays a push, which
            // is what lets later passes correct earlier over-corrections.
            const float previousNormal = constraint.normalImpulse;
            constraint.normalImpulse = std::max(previousNormal + lambda, 0.0f);
            lambda = constraint.normalImpulse - previousNormal;

            const glm::vec3 normalImpulse = constraint.normal * lambda;
            if (movableA) {
                rigidA->velocity -= normalImpulse * bodyA.inverseMass * bodyA.linearFactor;
                rigidA->angularVelocity -= bodyA.inverseInertia * glm::cross(constraint.armA, normalImpulse);
            }
            if (movableB) {
                rigidB->velocity += normalImpulse * bodyB.inverseMass * bodyB.linearFactor;
                rigidB->angularVelocity += bodyB.inverseInertia * glm::cross(constraint.armB, normalImpulse);
            }

            // ---- Friction ----
            //
            // Recomputed after the normal impulse and at the contact, spin
            // included: friction acts on the SURFACE speed, which is zero for a
            // ball rolling without slipping and is the entire reason a ball
            // rolls rather than slides.
            const glm::vec3 postSpinA = surfaceA ? rigidA->angularVelocity : glm::vec3(0.0f);
            const glm::vec3 postSpinB = surfaceB ? rigidB->angularVelocity : glm::vec3(0.0f);
            const glm::vec3 postRelative =
                ((surfaceB ? rigidB->velocity : glm::vec3(0.0f)) + glm::cross(postSpinB, constraint.armB)) -
                ((surfaceA ? rigidA->velocity : glm::vec3(0.0f)) + glm::cross(postSpinA, constraint.armA));

            glm::vec3 tangent = postRelative - constraint.normal * glm::dot(postRelative, constraint.normal);
            const float tangentLength = glm::length(tangent);
            if (tangentLength < 1e-5f) continue;
            tangent /= tangentLength;

            const glm::vec3 tangentialA =
                glm::cross(bodyA.inverseInertia * glm::cross(constraint.armA, tangent), constraint.armA);
            const glm::vec3 tangentialB =
                glm::cross(bodyB.inverseInertia * glm::cross(constraint.armB, tangent), constraint.armB);
            const float tangentMass = linearInverseMass(bodyA, tangent) + linearInverseMass(bodyB, tangent)
                                    + glm::dot(tangentialA + tangentialB, tangent);
            if (tangentMass <= 1e-9f) continue;

            float frictionLambda = -glm::dot(postRelative, tangent) / tangentMass;

            // Clamped against the ACCUMULATED normal impulse, so friction can
            // slow sliding but never reverse it, and never exceeds what the
            // contact is actually being pressed together with.
            const float maxFriction = constraint.friction * constraint.normalImpulse;
            const float previousTangent = constraint.tangentImpulse;
            constraint.tangentImpulse =
                std::clamp(previousTangent + frictionLambda, -maxFriction, maxFriction);
            frictionLambda = constraint.tangentImpulse - previousTangent;

            const glm::vec3 frictionImpulse = tangent * frictionLambda;
            if (movableA) {
                rigidA->velocity -= frictionImpulse * bodyA.inverseMass * bodyA.linearFactor;
                rigidA->angularVelocity -= bodyA.inverseInertia * glm::cross(constraint.armA, frictionImpulse);
            }
            if (movableB) {
                rigidB->velocity += frictionImpulse * bodyB.inverseMass * bodyB.linearFactor;
                rigidB->angularVelocity += bodyB.inverseInertia * glm::cross(constraint.armB, frictionImpulse);
            }
        }

        // Joints, INSIDE the same iteration as the contacts rather than in a
        // loop of their own. A body hanging from a rope and resting on the
        // ground has to satisfy both at once; solving them separately lets each
        // undo the other, and the body walks a little further out of place
        // every step it is held by two things.
        for (JointRuntime& joint : joints) {
            Joints::Body a = joint.a.state;
            Joints::Body b = joint.b.state;

            // The live velocities, because the contact pass just above may have
            // changed them. Read and written per pass rather than kept in the
            // Joints::Body, so there is one place a velocity lives and it is
            // the component - the same rule the contact solver follows.
            if (joint.a.rigid) {
                a.velocity = joint.a.rigid->velocity;
                a.angularVelocity = joint.a.rigid->angularVelocity;
            }
            if (joint.b.rigid) {
                b.velocity = joint.b.rigid->velocity;
                b.angularVelocity = joint.b.rigid->angularVelocity;
            }

            Joints::SolveVelocity(joint.constraint, a, b);

            // Only what can move gets written back. A static anchor's state was
            // built with a zero inverse mass, so the solver never gave it any
            // velocity to begin with - but a kinematic body has one that
            // something else owns, and writing to it would take it over.
            if (joint.a.rigid && a.inverseMass > 0.0f) {
                joint.a.rigid->velocity = a.velocity;
                joint.a.rigid->angularVelocity = a.angularVelocity;
            }
            if (joint.b.rigid && b.inverseMass > 0.0f) {
                joint.b.rigid->velocity = b.velocity;
                joint.b.rigid->angularVelocity = b.angularVelocity;
            }
        }
    }

    // ---- What broke ---------------------------------------------------------
    //
    // After every pass, not inside them: the impulse a joint applied is the
    // TOTAL over the step, and checking part way through would break a joint on
    // the first iteration's over-correction that the seventh was about to undo.
    //
    // Impulse back to force by dividing by the step, so the threshold an author
    // types is a force and stays the same number whatever the step is. Compared
    // separately from the torque, because they are not the same quantity: a
    // rope snapping under load and a hinge shearing off its frame are different
    // failures and adding their magnitudes would compare metres per second to
    // radians per second.
    for (const JointRuntime& joint : joints) {
        if (joint.breakForce <= 0.0f && joint.breakTorque <= 0.0f) continue;
        if (!registry.valid(joint.owner)) continue;

        auto* authored = registry.try_get<JointComponent>(joint.owner);
        if (!authored || authored->broken) continue;

        const float force = glm::length(joint.constraint.appliedLinear) / deltaTime;
        const float torque = glm::length(joint.constraint.appliedAngular) / deltaTime;

        const bool tore = joint.breakForce > 0.0f && force > joint.breakForce;
        const bool sheared = joint.breakTorque > 0.0f && torque > joint.breakTorque;
        if (!tore && !sheared) continue;

        authored->broken = true;
        SUPERSONIC_LOG_INFO("PhysicsSystem")
            << "A joint let go: " << (tore ? "pulled at " : "twisted at ")
            << (tore ? force : torque) << " against a limit of "
            << (tore ? joint.breakForce : joint.breakTorque) << "." << std::endl;
    }
}


namespace {

// Slab test against a world-space AABB. Returns the entry distance, which is
// what a ray hit means - the exit is behind the surface.
bool rayHitsAabb(const glm::vec3& origin, const glm::vec3& direction,
                 const glm::vec3& boxMin, const glm::vec3& boxMax,
                 float maxDistance, float& outDistance, glm::vec3& outNormal) {
    float tMin = 0.0f;
    float tMax = maxDistance;
    int entryAxis = 0;
    float entrySign = 1.0f;

    for (int axis = 0; axis < 3; ++axis) {
        // A ray parallel to a slab either misses entirely or is unconstrained
        // by it; dividing would produce an infinity that poisons the compare.
        if (std::fabs(direction[axis]) < 1e-8f) {
            if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis]) return false;
            continue;
        }

        const float inverse = 1.0f / direction[axis];
        float near = (boxMin[axis] - origin[axis]) * inverse;
        float far = (boxMax[axis] - origin[axis]) * inverse;
        float sign = -1.0f;
        if (near > far) { std::swap(near, far); sign = 1.0f; }

        if (near > tMin) {
            tMin = near;
            entryAxis = axis;
            entrySign = sign;
        }
        tMax = std::min(tMax, far);
        if (tMin > tMax) return false;
    }

    outDistance = tMin;
    outNormal = glm::vec3(0.0f);
    outNormal[entryAxis] = entrySign;
    return true;
}

bool rayHitsSphere(const glm::vec3& origin, const glm::vec3& direction,
                   const glm::vec3& centre, float radius,
                   float maxDistance, float& outDistance, glm::vec3& outNormal) {
    const glm::vec3 toCentre = centre - origin;
    const float projection = glm::dot(toCentre, direction);
    const float distanceSquared = glm::dot(toCentre, toCentre) - projection * projection;
    const float radiusSquared = radius * radius;
    if (distanceSquared > radiusSquared) return false;

    const float half = std::sqrt(radiusSquared - distanceSquared);
    float distance = projection - half;
    // Origin inside the sphere: the near root is behind us, so use the far one.
    if (distance < 0.0f) distance = projection + half;
    if (distance < 0.0f || distance > maxDistance) return false;

    outDistance = distance;
    const glm::vec3 point = origin + direction * distance;
    const glm::vec3 offset = point - centre;
    const float length = glm::length(offset);
    outNormal = length > 1e-6f ? offset / length : glm::vec3(0.0f, 1.0f, 0.0f);
    return true;
}

// The world-space shape of one collider, shared by every query.
struct QueryShape {
    // This was a bool called isSphere, which was fine while there were two
    // answers. Terrain is a third and cannot be either: its bounding box is the
    // box the hills fit inside, and answering a query with that would report
    // solid ground for anything anywhere above the landscape.
    enum class Kind { Box, Sphere, Heightfield };

    entt::entity entity{entt::null};
    Kind kind{Kind::Box};
    bool isTrigger{false};
    glm::vec3 centre{0.0f};
    glm::vec3 halfExtent{0.5f};
    float radius{0.5f};

    // Queries filter on the same mask the solver does, so "what can a bullet
    // hit" and "what does a bullet collide with" cannot disagree.
    uint32_t layer{1u};

    // Terrain only: the grid, and the frame a query has to be taken into.
    const Heightfield* field{nullptr};
    glm::vec3 fieldOrigin{0.0f};
    glm::mat3 fieldAxes{1.0f};
    float fieldScale{1.0f};
};

// A ray against terrain, marched over the grid's cells in its own space.
//
// The alternative - the bounding box, which is what a box collider gets here
// and is right for it - is not an approximation of a landscape, it is the sky
// above it. PhysicsSystem::IsGrounded fires a ray downwards, so a
// bounding-box answer is a character reporting that it is standing on the
// ground while it falls past a mountain.
bool rayHitsHeightfield(const QueryShape& shape, const glm::vec3& origin, const glm::vec3& ray,
                        float maxDistance, float& outDistance, glm::vec3& outNormal) {
    if (!shape.field || maxDistance <= 0.0f) return false;

    const glm::mat3 intoField = glm::transpose(shape.fieldAxes);
    const float inverseScale = 1.0f / shape.fieldScale;

    const glm::vec3 localOrigin = intoField * (origin - shape.fieldOrigin) * inverseScale;
    // Rotation only, so the direction stays unit length and what comes back is
    // a distance in the grid's units.
    const glm::vec3 localRay = intoField * ray;

    float localDistance = 0.0f;
    glm::vec3 localNormal(0.0f, 1.0f, 0.0f);
    if (!shape.field->Raycast(localOrigin, localRay, maxDistance * inverseScale,
                              localDistance, localNormal)) {
        return false;
    }

    outDistance = localDistance * shape.fieldScale;
    outNormal = shape.fieldAxes * localNormal;
    return true;
}

void gatherShapes(entt::registry& registry, std::vector<QueryShape>& out) {
    const auto collect = [&](entt::entity entity, QueryShape::Kind kind,
                             const glm::vec3& localHalfExtent, bool isTrigger,
                             const glm::vec3& localCenter, uint32_t layer) {
        const auto* transform = registry.try_get<TransformComponent>(entity);
        if (!transform) return;

        QueryShape shape;
        shape.entity = entity;
        shape.kind = kind;
        shape.isTrigger = isTrigger;

        const glm::mat4 world = parentWorldMatrix(registry, entity) * transform->getModelMatrix();
        worldBounds(world, localHalfExtent, shape.centre, shape.halfExtent);
        if (localCenter != glm::vec3(0.0f)) {
            shape.centre += glm::vec3(world * glm::vec4(localCenter, 0.0f));
        }
        shape.layer = layer;

        if (kind == QueryShape::Kind::Sphere) {
            // From the SCALE, the third and last place that had to be told.
            //
            // The box path above uses its world AABB deliberately and the
            // comment on the capsule says so: a query over-reports rather than
            // missing, which is the right direction for "what am I looking at".
            // A sphere is different in kind. Its rotation-dependence is not
            // conservatism, it is the same object being a different size
            // depending on which way it happens to be facing - so a rolling
            // ball's raycast radius swells and shrinks as it turns.
            const glm::mat3 basis(world);
            const float scale = std::max({glm::length(basis[0]), glm::length(basis[1]),
                                          glm::length(basis[2])});
            shape.radius = localHalfExtent.x * scale;
            shape.halfExtent = glm::vec3(shape.radius);
        }
        out.push_back(shape);
    };

    for (auto entity : registry.view<BoxColliderComponent>()) {
        const auto& box = registry.get<BoxColliderComponent>(entity);
        collect(entity, QueryShape::Kind::Box, box.size * 0.5f, box.isTrigger, box.center,
                box.layer);
    }
    for (auto entity : registry.view<CapsuleColliderComponent>()) {
        if (registry.all_of<BoxColliderComponent>(entity)) continue;
        const auto& capsule = registry.get<CapsuleColliderComponent>(entity);
        // Queried as the box that holds it, which is what a rotated box already
        // gets here. A ray can therefore hit a capsule slightly off its
        // shoulder; it over-reports rather than missing, which is the right
        // direction for "what am I looking at" and the wrong one for a bullet
        // that has to be fair.
        collect(entity, QueryShape::Kind::Box,
                glm::vec3(capsule.radius, capsule.height * 0.5f, capsule.radius),
                capsule.isTrigger, capsule.center, capsule.layer);
    }
    for (auto entity : registry.view<SphereColliderComponent>()) {
        // Matching the solver, in the same order: an entity with more than one
        // collider is gathered once, or a query would report it against itself.
        if (registry.any_of<BoxColliderComponent, CapsuleColliderComponent>(entity)) continue;
        const auto& sphere = registry.get<SphereColliderComponent>(entity);
        collect(entity, QueryShape::Kind::Sphere, glm::vec3(sphere.radius), sphere.isTrigger,
                sphere.center, sphere.layer);
    }

    // Hulls, as the box that holds them - the rule a rotated box and a capsule
    // already follow here, and the bounds the solver's broadphase uses for them.
    //
    // They were missing, and that is the failure the terrain comment below
    // describes: a hull is solid to the narrowphase, so a character lands on
    // one, and IsGrounded's ray found nothing under it. Found by the Magic
    // Portals spike, whose platforms are all hulls: a player at rest on one,
    // 0.28 px from where it should be, reported as never having landed.
    for (auto entity : registry.view<ConvexHullColliderComponent>()) {
        if (registry.any_of<BoxColliderComponent, CapsuleColliderComponent, SphereColliderComponent,
                            HeightfieldColliderComponent>(entity)) {
            continue;
        }
        const auto& authored = registry.get<ConvexHullColliderComponent>(entity);
        // Only the bounds are copied out, so no pointer into the cache outlives
        // this call and the step's Trim cannot leave one dangling.
        const ConvexDecomposition* hull = ConvexHullCache::For(registry).Get(registry, entity, authored);
        if (!hull) continue;
        const glm::vec3 localMin = hull->boundsMin();
        const glm::vec3 localMax = hull->boundsMax();
        collect(entity, QueryShape::Kind::Box, (localMax - localMin) * 0.5f, authored.isTrigger,
                (localMin + localMax) * 0.5f, authored.layer);
    }

    // Terrain last, which is the order the solver gathers in.
    //
    // Same rule as the solver's gather: trimmed before the loop, because the
    // shapes below hold pointers into the cache for as long as the query runs.
    HeightfieldCache::For(registry).Trim();

    for (auto entity : registry.view<HeightfieldColliderComponent>()) {
        if (registry.any_of<BoxColliderComponent, CapsuleColliderComponent,
                            SphereColliderComponent>(entity)) continue;

        const auto* transform = registry.try_get<TransformComponent>(entity);
        if (!transform) continue;

        const auto& terrain = registry.get<HeightfieldColliderComponent>(entity);
        const Heightfield* field = HeightfieldCache::For(registry).Get(terrain);
        if (!field) continue;

        const glm::mat4 world = parentWorldMatrix(registry, entity) * transform->getModelMatrix();

        QueryShape shape;
        shape.entity = entity;
        shape.kind = QueryShape::Kind::Heightfield;
        shape.isTrigger = terrain.isTrigger;
        shape.layer = terrain.layer;
        shape.field = field;
        shape.fieldOrigin = glm::vec3(world[3]);

        float largestScale = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const glm::vec3 column = glm::vec3(world[axis]);
            const float length = glm::length(column);
            shape.fieldAxes[axis] = length > 1e-6f ? column / length
                                                   : glm::vec3(axis == 0, axis == 1, axis == 2);
            largestScale = std::max(largestScale, length);
        }
        shape.fieldScale = largestScale > 1e-6f ? largestScale : 1.0f;

        // The bounds are filled in anyway, because they are what anything else
        // reading this list - an editor gizmo, a debug draw - expects to find.
        const glm::vec3 localMin = field->LocalMin();
        const glm::vec3 localMax = field->LocalMax();
        worldBounds(world, (localMax - localMin) * 0.5f, shape.centre, shape.halfExtent);
        shape.centre += glm::vec3(world * glm::vec4((localMin + localMax) * 0.5f, 0.0f));

        out.push_back(shape);
    }
}

} // namespace

PhysicsSystem::RayHit PhysicsSystem::Raycast(entt::registry& registry, const glm::vec3& origin,
                                             const glm::vec3& direction, float maxDistance,
                                             entt::entity ignore, bool includeTriggers,
                                             uint32_t layerMask) {
    RayHit result;

    const float length = glm::length(direction);
    // A zero-length direction has no meaning; normalising it would be a NaN
    // that silently reports a hit at an impossible distance.
    if (length < 1e-8f || maxDistance <= 0.0f) return result;
    const glm::vec3 ray = direction / length;

    std::vector<QueryShape> shapes;
    gatherShapes(registry, shapes);

    float nearest = maxDistance;
    for (const QueryShape& shape : shapes) {
        if (shape.entity == ignore) continue;
        if (shape.isTrigger && !includeTriggers) continue;
        if ((layerMask & shape.layer) == 0) continue;

        float distance = 0.0f;
        glm::vec3 normal(0.0f);
        bool hit = false;
        switch (shape.kind) {
        case QueryShape::Kind::Sphere:
            hit = rayHitsSphere(origin, ray, shape.centre, shape.radius, nearest, distance, normal);
            break;
        case QueryShape::Kind::Heightfield:
            hit = rayHitsHeightfield(shape, origin, ray, nearest, distance, normal);
            break;
        case QueryShape::Kind::Box:
            hit = rayHitsAabb(origin, ray, shape.centre - shape.halfExtent,
                              shape.centre + shape.halfExtent, nearest, distance, normal);
            break;
        }

        if (!hit || distance > nearest) continue;

        nearest = distance;
        result.hit = true;
        result.entity = shape.entity;
        result.distance = distance;
        result.point = origin + ray * distance;
        result.normal = normal;
    }
    return result;
}

void PhysicsSystem::OverlapSphere(entt::registry& registry, const glm::vec3& centre, float radius,
                                  std::vector<entt::entity>& outEntities, entt::entity ignore,
                                  bool includeTriggers, uint32_t layerMask) {
    if (radius <= 0.0f) return;

    std::vector<QueryShape> shapes;
    gatherShapes(registry, shapes);

    for (const QueryShape& shape : shapes) {
        if (shape.entity == ignore) continue;
        if (shape.isTrigger && !includeTriggers) continue;
        if ((layerMask & shape.layer) == 0) continue;

        if (shape.kind == QueryShape::Kind::Sphere) {
            const float reach = radius + shape.radius;
            if (glm::dot(shape.centre - centre, shape.centre - centre) <= reach * reach) {
                outEntities.push_back(shape.entity);
            }
            continue;
        }

        if (shape.kind == QueryShape::Kind::Heightfield) {
            // Against the SURFACE, for the same reason the ray is: an overlap
            // against a landscape's bounding box reports the whole sky above it.
            if (!shape.field) continue;
            const glm::mat3 intoField = glm::transpose(shape.fieldAxes);
            const float inverseScale = 1.0f / shape.fieldScale;
            const glm::vec3 local = intoField * (centre - shape.fieldOrigin) * inverseScale;
            if (shape.field->CollideSphere(local, radius * inverseScale).count > 0) {
                outEntities.push_back(shape.entity);
            }
            continue;
        }

        // Closest point on the box to the sphere's centre - exact, unlike
        // treating the sphere as its own box, which over-reports at corners.
        const glm::vec3 closest = glm::clamp(centre, shape.centre - shape.halfExtent,
                                             shape.centre + shape.halfExtent);
        const glm::vec3 delta = centre - closest;
        if (glm::dot(delta, delta) <= radius * radius) {
            outEntities.push_back(shape.entity);
        }
    }
}

bool PhysicsSystem::IsGrounded(entt::registry& registry, const glm::vec3& footPosition,
                               float distance, entt::entity ignore, uint32_t layerMask) {
    // The world ground plane counts when the scene has one, since the solver
    // treats it as solid even though no entity represents it. It must be asked
    // about rather than assumed: reporting solid ground on a plane that is
    // switched off is a character standing on nothing, and this check lives two
    // hundred lines from the clamp it has to agree with.
    if (const auto* settings = registry.ctx().find<PhysicsSettings>()) {
        if (settings->hasGroundPlane && footPosition.y - distance <= settings->groundPlaneY) {
            return true;
        }
    }

    const RayHit hit = Raycast(registry, footPosition, glm::vec3(0.0f, -1.0f, 0.0f),
                               distance, ignore, /*includeTriggers=*/false, layerMask);
    return hit.hit;
}

} // namespace Supersonic
