#include "core/PhysicsSystem.hpp"
#include "core/CollisionSAT.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include "core/Components.hpp"

#include <algorithm>
#include <cmath>

namespace Supersonic {

namespace {

constexpr float kGravity = -9.81f;
constexpr float kGroundPlaneY = 0.0f;
// Defaults for a body that has no RigidBodyComponent at all - a static
// collider. A body that has one carries its own.
constexpr float kRestitution = 0.3f;
constexpr float kRestVelocity = 0.1f;

// Positional correction. Bodies are allowed to interpenetrate by kSlop before
// anything is pushed apart, and only kCorrection of the remaining overlap is
// removed per step: correcting to exactly zero makes resting stacks vibrate,
// because floating-point error re-creates the overlap every step.
constexpr float kSlop = 0.005f;
constexpr float kCorrection = 0.8f;

// Coulomb friction. Without it a box landing on a slope slides forever and a
// stack of boxes never settles laterally.
constexpr float kFriction = 0.4f;

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

enum class Shape { Box, Sphere };

struct Body {
    entt::entity entity{entt::null};
    Shape shape{Shape::Box};

    // World space.
    glm::vec3 centre{0.0f};
    glm::vec3 halfExtent{0.5f}; // boxes: WORLD-AXIS-ALIGNED, for the broadphase
    float radius{0.5f};         // spheres

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
    if (rigidBody->mass <= 0.0f) return glm::vec3(0.0f);

    const float mass = rigidBody->mass;

    if (shape == Shape::Sphere) {
        // Solid sphere: 2/5 m r^2 about every axis.
        const float inertia = 0.4f * mass * radius * radius;
        return inertia > 1e-9f ? glm::vec3(1.0f / inertia) : glm::vec3(0.0f);
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

// Box against box, as world AABBs. The separating axis is the one of least
// overlap, which is what makes a body landing on top of another get pushed up
// rather than sideways.
bool collideBoxBox(const Body& a, const Body& b, glm::vec3& normal, float& penetration,
                   glm::vec3& point) {
    const glm::vec3 delta = b.centre - a.centre;
    const glm::vec3 overlap = (a.halfExtent + b.halfExtent) - glm::abs(delta);

    if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f) return false;

    // The middle of the overlapping region. A single point stands in for
    // what is really a face or an edge, which is why a box settling flat
    // still rocks slightly before it comes to rest - but it is on the right
    // side of the centre of mass, which is what decides which way a crate
    // tips when it lands on the corner of something.
    const glm::vec3 overlapMin = glm::max(a.centre - a.halfExtent, b.centre - b.halfExtent);
    const glm::vec3 overlapMax = glm::min(a.centre + a.halfExtent, b.centre + b.halfExtent);
    point = (overlapMin + overlapMax) * 0.5f;

    if (overlap.x <= overlap.y && overlap.x <= overlap.z) {
        penetration = overlap.x;
        normal = glm::vec3(delta.x < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
    } else if (overlap.y <= overlap.z) {
        penetration = overlap.y;
        normal = glm::vec3(0.0f, delta.y < 0.0f ? -1.0f : 1.0f, 0.0f);
    } else {
        penetration = overlap.z;
        normal = glm::vec3(0.0f, 0.0f, delta.z < 0.0f ? -1.0f : 1.0f);
    }
    return true;
}

bool collideSphereSphere(const Body& a, const Body& b, glm::vec3& normal, float& penetration,
                         glm::vec3& point) {
    const glm::vec3 delta = b.centre - a.centre;
    const float sum = a.radius + b.radius;
    const float distanceSquared = glm::dot(delta, delta);

    if (distanceSquared >= sum * sum) return false;

    const float distance = std::sqrt(distanceSquared);
    if (distance < 1e-6f) {
        // Concentric. Any axis is as good as another; up keeps them from being
        // launched sideways at enormous speed.
        normal = glm::vec3(0.0f, 1.0f, 0.0f);
        penetration = sum;
        point = a.centre;
        return true;
    }

    normal = delta / distance;
    penetration = sum - distance;
    // On the line of centres, between the two surfaces.
    point = a.centre + normal * (a.radius - penetration * 0.5f);
    return true;
}

// Sphere against box, via the closest point on the box. Exact, unlike treating
// the sphere as its own bounding box, which would let it catch on corners.
bool collideBoxSphere(const Body& box, const Body& sphere, glm::vec3& normal, float& penetration,
                      glm::vec3& point) {
    const glm::vec3 boxMin = box.centre - box.halfExtent;
    const glm::vec3 boxMax = box.centre + box.halfExtent;
    const glm::vec3 closest = glm::clamp(sphere.centre, boxMin, boxMax);

    const glm::vec3 delta = sphere.centre - closest;
    const float distanceSquared = glm::dot(delta, delta);

    if (distanceSquared > sphere.radius * sphere.radius) return false;

    // The closest point on the box IS the contact, which is exact here
    // rather than the approximation box-box has to make.
    point = closest;

    if (distanceSquared > 1e-12f) {
        const float distance = std::sqrt(distanceSquared);
        normal = delta / distance;
        penetration = sphere.radius - distance;
        return true;
    }

    // Centre is inside the box: push out along the nearest face.
    const glm::vec3 toMin = sphere.centre - boxMin;
    const glm::vec3 toMax = boxMax - sphere.centre;

    float best = toMin.x;
    normal = glm::vec3(-1.0f, 0.0f, 0.0f);
    if (toMax.x < best) { best = toMax.x; normal = glm::vec3(1.0f, 0.0f, 0.0f); }
    if (toMin.y < best) { best = toMin.y; normal = glm::vec3(0.0f, -1.0f, 0.0f); }
    if (toMax.y < best) { best = toMax.y; normal = glm::vec3(0.0f, 1.0f, 0.0f); }
    if (toMin.z < best) { best = toMin.z; normal = glm::vec3(0.0f, 0.0f, -1.0f); }
    if (toMax.z < best) { best = toMax.z; normal = glm::vec3(0.0f, 0.0f, 1.0f); }

    penetration = sphere.radius + best;
    return true;
}

} // namespace

void PhysicsSystem::SweepAndPrune(std::vector<Proxy>& proxies,
                                  std::vector<std::pair<size_t, size_t>>& outPairs) {
    outPairs.clear();
    if (proxies.size() < 2) return;

    // Sorted on X, then a forward scan that stops as soon as the next proxy
    // starts past the current one's end. This is what turns the O(n^2) all-pairs
    // test into something that costs a sort plus the actual overlaps.
    std::sort(proxies.begin(), proxies.end(),
              [](const Proxy& lhs, const Proxy& rhs) { return lhs.min.x < rhs.min.x; });

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

    // ---- Integrate, and resolve against the world ground plane ----
    auto dynamics = registry.view<TransformComponent, RigidBodyComponent>();
    for (auto entity : dynamics) {
        auto& transform = dynamics.get<TransformComponent>(entity);
        auto& rigidBody = dynamics.get<RigidBodyComponent>(entity);

        if (rigidBody.isKinematic) continue;

        if (rigidBody.useGravity) {
            rigidBody.velocity.y += kGravity * deltaTime;
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
            rigidBody.velocity *= std::pow(std::max(0.0f, 1.0f - rigidBody.linearDamping),
                                           deltaTime);
        }

        if (rigidBody.freezeRotation) {
            rigidBody.angularVelocity = glm::vec3(0.0f);
        } else {
            if (rigidBody.angularDamping > 0.0f) {
                rigidBody.angularVelocity *=
                    std::pow(std::max(0.0f, 1.0f - rigidBody.angularDamping), deltaTime);
            }

            // Integrated as a quaternion and written back as Euler angles.
            //
            // Adding the angular velocity to the Euler triple directly is only
            // correct for spin about one axis at a time: Euler rates are not
            // the angular velocity, and a body tumbling about two axes at once
            // would wander off in a way that looks like the physics is broken.
            // The transform stores Euler because that is what an inspector can
            // sensibly edit, so the conversion happens here, once per step.
            const float speed = glm::length(rigidBody.angularVelocity);
            if (speed > 1e-6f) {
                const glm::quat current(transform.rotation);
                const glm::quat spin =
                    glm::angleAxis(speed * deltaTime, rigidBody.angularVelocity / speed);
                transform.rotation = glm::eulerAngles(glm::normalize(spin * current));
            }
        }

        transform.position += worldToLocal * (rigidBody.velocity * deltaTime);

        // Resolve against the bottom of the collider, not the transform origin.
        // Clamping the origin to y = 0 buried every body by half its height and
        // made it impossible to rest anything below the world plane.
        //
        // Measured in WORLD space, through the collider's world bounds. Testing
        // the local position against y = 0 puts the floor wherever the parent
        // happens to be, so a body parented ten units up rested in mid-air and
        // never fell at all.
        glm::vec3 localHalfExtent(0.5f);
        if (const auto* box = registry.try_get<BoxColliderComponent>(entity)) {
            localHalfExtent = box->size * 0.5f;
        } else if (const auto* sphere = registry.try_get<SphereColliderComponent>(entity)) {
            localHalfExtent = glm::vec3(sphere->radius);
        }

        glm::vec3 centre(0.0f);
        glm::vec3 halfExtent(0.5f);
        worldBounds(parentWorld * transform.getModelMatrix(), localHalfExtent, centre, halfExtent);

        const float bottom = centre.y - halfExtent.y;
        if (bottom < kGroundPlaneY) {
            transform.position += worldToLocal * glm::vec3(0.0f, kGroundPlaneY - bottom, 0.0f);

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

        Body body;
        body.entity = entity;
        body.shape = shape;
        body.isTrigger = isTrigger;
        body.inverseMass = inverseMassOf(registry.try_get<RigidBodyComponent>(entity));

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
            // A sphere has one radius, so a non-uniform scale has to collapse to
            // one number; the largest keeps it conservative.
            body.radius = std::max({body.halfExtent.x, body.halfExtent.y, body.halfExtent.z});
            body.halfExtent = glm::vec3(body.radius);
        }

        body.min = body.centre - body.halfExtent;
        body.max = body.centre + body.halfExtent;

        // From the same extents the collision uses, and in the same frame.
        //
        // body.halfExtent is the WORLD-AXIS-ALIGNED extent - a rotated collider
        // is enclosed by it, which is how collision treats it too. Taking the
        // inertia of that box and then rotating it would describe neither the
        // box nor its bounding box, and would disagree with the very contacts
        // it is used to resolve. So: the mass properties of the collision
        // shape, which for a rotated crate means the shape it actually
        // collides as. Already world-aligned, so no rotation is applied.
        //
        // A sphere is isotropic, so this costs it nothing either way.
        body.inverseInertia = worldInverseInertia(
            inverseInertiaLocal(registry.try_get<RigidBodyComponent>(entity),
                                shape, body.halfExtent, body.radius),
            glm::mat3(1.0f));

        // How far this body travels in one step. The broadphase bound is
        // expanded by it so a pair that will meet during the step is found
        // BEFORE they touch - a projectile crossing a wall between two frames is
        // never a candidate pair otherwise, and nothing downstream gets a chance
        // to stop it.
        glm::vec3 sweep(0.0f);
        if (const auto* rigid = registry.try_get<RigidBodyComponent>(entity);
            rigid && body.inverseMass > 0.0f) {
            sweep = glm::abs(rigid->velocity) * deltaTime;
        }
        body.sweep = sweep;

        Proxy proxy;
        proxy.entity = entity;
        proxy.index = bodies.size();
        proxy.min = body.min - sweep;
        proxy.max = body.max + sweep;
        proxy.inverseMass = body.inverseMass;
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
    for (auto entity : registry.view<SphereColliderComponent>()) {
        // An entity carrying both colliders would otherwise be added twice and
        // then collide with itself.
        if (registry.all_of<BoxColliderComponent>(entity)) continue;
        const auto& sphere = registry.get<SphereColliderComponent>(entity);
        collect(entity, Shape::Sphere, glm::vec3(sphere.radius), sphere.isTrigger,
                sphere.center, sphere.layer, sphere.collidesWith);
    }

    if (bodies.size() < 2) return;

    std::vector<std::pair<size_t, size_t>> pairs;
    SweepAndPrune(proxies, pairs);
    if (pairs.empty()) return;

    // ---- Narrowphase and response ----
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

        // How far this pair can close during the step. Anything further apart
        // than this cannot meet before the next step, so reporting it would only
        // make the solver brake for something it will never reach.
        const float pairMargin = glm::length(a.sweep) + glm::length(b.sweep);

        if (a.shape == Shape::Box && b.shape == Shape::Box) {
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
            }
        } else if (a.shape == Shape::Sphere && b.shape == Shape::Sphere) {
            hit = collideSphereSphere(a, b, normal, penetration, point);
        } else if (a.shape == Shape::Box) {
            // The box is a, so the normal already points from a toward b.
            hit = CollisionSAT::CollideSphereObb(b.centre, b.radius, obbOf(a),
                                                 normal, penetration, point, pairMargin);
            speculative = hit && penetration < 0.0f;
        } else {
            // Sphere against box: solve it the other way round and flip, so
            // there is one implementation rather than two that can disagree.
            hit = CollisionSAT::CollideSphereObb(a.centre, a.radius, obbOf(b),
                                                 normal, penetration, point, pairMargin);
            speculative = hit && penetration < 0.0f;
            normal = -normal;
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

        const float inverseSum = a.inverseMass + b.inverseMass;
        if (inverseSum <= 0.0f) continue;

        auto* transformA = registry.try_get<TransformComponent>(a.entity);
        auto* transformB = registry.try_get<TransformComponent>(b.entity);
        if (!transformA || !transformB) continue;

        // Positional correction, shared out by inverse mass so the heavier body
        // moves less and an immovable one does not move at all.
        // Never for a speculative contact: there is nothing to push out of.
        const float correctable = speculative ? 0.0f : std::max(penetration - kSlop, 0.0f);
        if (correctable > 0.0f) {
            const glm::vec3 push = normal * (correctable * kCorrection / inverseSum);
            transformA->position -= a.worldToLocal * (push * a.inverseMass);
            transformB->position += b.worldToLocal * (push * b.inverseMass);

            // Keep the cached bounds honest for the pairs still to be resolved
            // this step, or a body wedged between two others gets pushed twice
            // as far as it should be.
            const glm::vec3 shiftA = -normal * (correctable * kCorrection / inverseSum) * a.inverseMass;
            const glm::vec3 shiftB = normal * (correctable * kCorrection / inverseSum) * b.inverseMass;
            a.centre += shiftA;
            b.centre += shiftB;
        }

        auto* rigidA = registry.try_get<RigidBodyComponent>(a.entity);
        auto* rigidB = registry.try_get<RigidBodyComponent>(b.entity);

        // Where the contact is, relative to each centre of mass. This is the
        // whole of the difference between a body that slides and one that
        // turns: an impulse through the centre only pushes, the same impulse
        // applied at arm's length also spins.
        const glm::vec3 armA = point - a.centre;
        const glm::vec3 armB = point - b.centre;

        const glm::vec3 spinA = (rigidA && a.inverseMass > 0.0f) ? rigidA->angularVelocity : glm::vec3(0.0f);
        const glm::vec3 spinB = (rigidB && b.inverseMass > 0.0f) ? rigidB->angularVelocity : glm::vec3(0.0f);

        const glm::vec3 velocityA = ((rigidA && a.inverseMass > 0.0f) ? rigidA->velocity : glm::vec3(0.0f))
                                  + glm::cross(spinA, armA);
        const glm::vec3 velocityB = ((rigidB && b.inverseMass > 0.0f) ? rigidB->velocity : glm::vec3(0.0f))
                                  + glm::cross(spinB, armB);

        const glm::vec3 relative = velocityB - velocityA;
        const float alongNormal = glm::dot(relative, normal);

        // For a speculative contact the target is not "stop", it is "close the
        // remaining gap and no more". The gap is -penetration, so approaching
        // at gap/dt lands the body exactly on the surface this step; anything
        // faster is what would carry it through.
        //
        // This is what prevents tunnelling without a swept test: the body is
        // slowed on the frame BEFORE it would have passed through, and arrives
        // as an ordinary contact on the next one.
        const float allowedApproach = speculative ? (-penetration / deltaTime) : 0.0f;

        // Already separating, or not closing fast enough to reach: an impulse
        // here would drag them together or brake for nothing.
        if (alongNormal + allowedApproach > 0.0f) continue;

        // Bounce dies out near rest, otherwise a settling box jitters forever.
        const float bounceA = rigidA ? rigidA->restitution : kRestitution;
        const float bounceB = rigidB ? rigidB->restitution : kRestitution;
        // Restitution applies to a speculative contact too.
        //
        // The first attempt zeroed it, reasoning that bodies which have not
        // touched should not bounce. But a falling ball is ALWAYS approaching
        // fast enough to have a speculative contact before a touching one, so
        // zeroing it there meant nothing ever bounced at all: two balls with
        // restitution 0.1 and 0.9 both landed dead at exactly the same height.
        //
        // The bias below already accounts for the gap, so the two terms
        // compose: the restitution term reverses the approach, the bias removes
        // whatever of it the remaining gap cannot absorb.
        const float restitution = (std::abs(alongNormal) < kRestVelocity)
                                ? 0.0f
                                : combineRestitution(bounceA, bounceB);

        // Effective mass along the normal, including how hard each body is to
        // turn about this contact. Using the linear term alone would apply an
        // impulse far too large for a glancing hit near a corner.
        const glm::vec3 angularA = glm::cross(a.inverseInertia * glm::cross(armA, normal), armA);
        const glm::vec3 angularB = glm::cross(b.inverseInertia * glm::cross(armB, normal), armB);
        const float effectiveMass = inverseSum + glm::dot(angularA + angularB, normal);
        if (effectiveMass <= 1e-9f) continue;

        // Expressed as a TARGET normal velocity rather than as a sum of terms.
        //
        // Adding the gap allowance to the restitution term makes the two fight:
        // a ball with restitution 0.9 dropped onto the ground has a speculative
        // contact before a touching one, the allowance cancels most of the
        // bounce, and it lands almost dead - measured at 0.008 units of bounce
        // against 0.05 the test asks for.
        //
        // As a target the two compose properly. The bounce says how fast the
        // body should be leaving; the allowance says it may still approach at
        // most fast enough to close the remaining gap this step. Taking the
        // larger means restitution decides when there is a bounce, and the
        // allowance only decides anything when there is not.
        //
        // For a touching contact the allowance is zero and this reduces exactly
        // to the previous formula, which is why no existing behaviour moves.
        const float bounceTarget = -restitution * alongNormal;
        const float targetVelocity = std::max(bounceTarget, -allowedApproach);
        const float impulse = (targetVelocity - alongNormal) / effectiveMass;
        const glm::vec3 impulseVector = normal * impulse;

        if (rigidA && a.inverseMass > 0.0f) {
            rigidA->velocity -= impulseVector * a.inverseMass;
            rigidA->angularVelocity -= a.inverseInertia * glm::cross(armA, impulseVector);
        }
        if (rigidB && b.inverseMass > 0.0f) {
            rigidB->velocity += impulseVector * b.inverseMass;
            rigidB->angularVelocity += b.inverseInertia * glm::cross(armB, impulseVector);
        }

        // Coulomb friction along the contact tangent, clamped to the normal
        // impulse so it can slow sliding but never reverse it.
        // Recomputed at the contact, spin included: friction on a rolling ball
        // acts on the surface speed, which is zero when it rolls without
        // slipping and is the entire reason a ball rolls instead of sliding.
        const glm::vec3 postSpinA = (rigidA && a.inverseMass > 0.0f) ? rigidA->angularVelocity : glm::vec3(0.0f);
        const glm::vec3 postSpinB = (rigidB && b.inverseMass > 0.0f) ? rigidB->angularVelocity : glm::vec3(0.0f);

        const glm::vec3 postRelative =
            (((rigidB && b.inverseMass > 0.0f) ? rigidB->velocity : glm::vec3(0.0f))
                + glm::cross(postSpinB, armB)) -
            (((rigidA && a.inverseMass > 0.0f) ? rigidA->velocity : glm::vec3(0.0f))
                + glm::cross(postSpinA, armA));

        glm::vec3 tangent = postRelative - normal * glm::dot(postRelative, normal);
        const float tangentLength = glm::length(tangent);
        if (tangentLength < 1e-5f) continue;
        tangent /= tangentLength;

        const glm::vec3 tangentialA = glm::cross(a.inverseInertia * glm::cross(armA, tangent), armA);
        const glm::vec3 tangentialB = glm::cross(b.inverseInertia * glm::cross(armB, tangent), armB);
        const float tangentMass = inverseSum + glm::dot(tangentialA + tangentialB, tangent);
        if (tangentMass <= 1e-9f) continue;

        float frictionImpulse = -glm::dot(postRelative, tangent) / tangentMass;
        const float gripA = rigidA ? rigidA->friction : kFriction;
        const float gripB = rigidB ? rigidB->friction : kFriction;
        const float maxFriction = combineFriction(gripA, gripB) * std::abs(impulse);
        frictionImpulse = std::clamp(frictionImpulse, -maxFriction, maxFriction);

        const glm::vec3 frictionVector = tangent * frictionImpulse;
        if (rigidA && a.inverseMass > 0.0f) {
            rigidA->velocity -= frictionVector * a.inverseMass;
            rigidA->angularVelocity -= a.inverseInertia * glm::cross(armA, frictionVector);
        }
        if (rigidB && b.inverseMass > 0.0f) {
            rigidB->velocity += frictionVector * b.inverseMass;
            rigidB->angularVelocity += b.inverseInertia * glm::cross(armB, frictionVector);
        }
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
    entt::entity entity{entt::null};
    bool isSphere{false};
    bool isTrigger{false};
    glm::vec3 centre{0.0f};
    glm::vec3 halfExtent{0.5f};
    float radius{0.5f};

    // Queries filter on the same mask the solver does, so "what can a bullet
    // hit" and "what does a bullet collide with" cannot disagree.
    uint32_t layer{1u};
};

void gatherShapes(entt::registry& registry, std::vector<QueryShape>& out) {
    const auto collect = [&](entt::entity entity, bool sphere,
                             const glm::vec3& localHalfExtent, bool isTrigger,
                             const glm::vec3& localCenter, uint32_t layer) {
        const auto* transform = registry.try_get<TransformComponent>(entity);
        if (!transform) return;

        QueryShape shape;
        shape.entity = entity;
        shape.isSphere = sphere;
        shape.isTrigger = isTrigger;

        const glm::mat4 world = parentWorldMatrix(registry, entity) * transform->getModelMatrix();
        worldBounds(world, localHalfExtent, shape.centre, shape.halfExtent);
        if (localCenter != glm::vec3(0.0f)) {
            shape.centre += glm::vec3(world * glm::vec4(localCenter, 0.0f));
        }
        shape.layer = layer;

        if (sphere) {
            shape.radius = std::max({shape.halfExtent.x, shape.halfExtent.y, shape.halfExtent.z});
            shape.halfExtent = glm::vec3(shape.radius);
        }
        out.push_back(shape);
    };

    for (auto entity : registry.view<BoxColliderComponent>()) {
        const auto& box = registry.get<BoxColliderComponent>(entity);
        collect(entity, false, box.size * 0.5f, box.isTrigger, box.center, box.layer);
    }
    for (auto entity : registry.view<SphereColliderComponent>()) {
        // Matching the solver: an entity with both colliders is a box, and must
        // not be gathered twice or a query would report it against itself.
        if (registry.all_of<BoxColliderComponent>(entity)) continue;
        const auto& sphere = registry.get<SphereColliderComponent>(entity);
        collect(entity, true, glm::vec3(sphere.radius), sphere.isTrigger,
                sphere.center, sphere.layer);
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
        const bool hit = shape.isSphere
            ? rayHitsSphere(origin, ray, shape.centre, shape.radius, nearest, distance, normal)
            : rayHitsAabb(origin, ray, shape.centre - shape.halfExtent,
                          shape.centre + shape.halfExtent, nearest, distance, normal);

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

        if (shape.isSphere) {
            const float reach = radius + shape.radius;
            if (glm::dot(shape.centre - centre, shape.centre - centre) <= reach * reach) {
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
    // The world ground plane counts, since the solver treats it as solid even
    // though no entity represents it.
    if (footPosition.y - distance <= 0.0f) return true;

    const RayHit hit = Raycast(registry, footPosition, glm::vec3(0.0f, -1.0f, 0.0f),
                               distance, ignore, /*includeTriggers=*/false, layerMask);
    return hit.hit;
}

} // namespace Supersonic
