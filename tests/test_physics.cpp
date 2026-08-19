// Regression tests for the physics step.
//
// The integrator ran on the raw wall-clock frame delta with no clamp, and
// ground collision clamped the transform ORIGIN to y = 0 while reflecting
// velocity unconditionally.
//
// The collision half is newer. BoxColliderComponent and SphereColliderComponent
// were stored, shown in the inspector and serialised, but never tested against
// anything: two bodies could occupy the same space with no complaint, and
// RigidBodyComponent::mass was written and never read by anything.

#include "TestHarness.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/Components.hpp"
#include "core/TransformSystem.hpp"

#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <string>
#include <cstdio>
#include <vector>

using namespace Supersonic;

static void stepFor(entt::registry& registry, float seconds, float step = 1.0f / 60.0f) {
    for (float t = 0.0f; t < seconds; t += step) {
        PhysicsSystem::Update(registry, step);
    }
}

static entt::entity makeBox(entt::registry& registry, const glm::vec3& position,
                            float mass = 1.0f, const glm::vec3& scale = glm::vec3(1.0f)) {
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = position;
    transform.scale = scale;
    auto& body = registry.emplace<RigidBodyComponent>(entity);
    body.mass = mass;
    registry.emplace<BoxColliderComponent>(entity);
    return entity;
}

// A collider with no rigid body: an immovable obstacle, which is how level
// geometry takes part without being simulated.
static entt::entity makeStaticBox(entt::registry& registry, const glm::vec3& position,
                                  const glm::vec3& scale = glm::vec3(1.0f)) {
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = position;
    transform.scale = scale;
    registry.emplace<BoxColliderComponent>(entity);
    return entity;
}

static entt::entity makeSphere(entt::registry& registry, const glm::vec3& position,
                               float radius = 0.5f, float mass = 1.0f) {
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = position;
    auto& body = registry.emplace<RigidBodyComponent>(entity);
    body.mass = mass;
    auto& collider = registry.emplace<SphereColliderComponent>(entity);
    collider.radius = radius;
    return entity;
}

static PhysicsSystem::Proxy makeProxy(uint32_t id, size_t index,
                                      const glm::vec3& min, const glm::vec3& max,
                                      float inverseMass) {
    PhysicsSystem::Proxy proxy;
    proxy.entity = static_cast<entt::entity>(id);
    proxy.index = index;
    proxy.min = min;
    proxy.max = max;
    proxy.inverseMass = inverseMass;
    return proxy;
}

static void testSweepAndPruneFindsOverlappingPairs() {
    std::vector<PhysicsSystem::Proxy> proxies;
    proxies.push_back(makeProxy(1, 0, glm::vec3(0.0f), glm::vec3(1.0f), 1.0f));
    proxies.push_back(makeProxy(2, 1, glm::vec3(0.5f), glm::vec3(1.5f), 1.0f));
    proxies.push_back(makeProxy(3, 2, glm::vec3(50.0f), glm::vec3(51.0f), 1.0f));

    std::vector<std::pair<size_t, size_t>> pairs;
    PhysicsSystem::SweepAndPrune(proxies, pairs);
    CHECK_EQ(pairs.size(), size_t{1});
}

static void testSweepAndPruneSkipsPairsSeparatedOffAxis() {
    // Overlapping on the sweep axis but disjoint on Y. Testing X alone is not
    // enough, and that is the classic sort-and-sweep mistake.
    std::vector<PhysicsSystem::Proxy> proxies;
    proxies.push_back(makeProxy(1, 0, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), 1.0f));
    proxies.push_back(makeProxy(2, 1, glm::vec3(0.5f, 9.0f, 0.0f), glm::vec3(1.5f, 10.0f, 1.0f), 1.0f));

    std::vector<std::pair<size_t, size_t>> pairs;
    PhysicsSystem::SweepAndPrune(proxies, pairs);
    CHECK_MSG(pairs.empty(), "boxes apart on Y must not be reported as candidates");
}

static void testSweepAndPruneIgnoresTwoStatics() {
    std::vector<PhysicsSystem::Proxy> proxies;
    proxies.push_back(makeProxy(1, 0, glm::vec3(0.0f), glm::vec3(1.0f), 0.0f));
    proxies.push_back(makeProxy(2, 1, glm::vec3(0.5f), glm::vec3(1.5f), 0.0f));

    std::vector<std::pair<size_t, size_t>> pairs;
    PhysicsSystem::SweepAndPrune(proxies, pairs);
    CHECK_MSG(pairs.empty(), "two immovable colliders have nothing to resolve");
}

static void testBoxesSeparateInsteadOfOverlapping() {
    entt::registry registry;
    const auto a = makeBox(registry, glm::vec3(0.0f, 5.0f, 0.0f));
    const auto b = makeBox(registry, glm::vec3(0.5f, 5.0f, 0.0f));

    stepFor(registry, 0.5f);

    const float separation = std::fabs(registry.get<TransformComponent>(a).position.x -
                                       registry.get<TransformComponent>(b).position.x);
    CHECK_MSG(separation > 0.5f, "overlapping boxes must be pushed apart, not left interpenetrating");
}

static void testBodyRestsOnStaticPlatformNotThroughIt() {
    entt::registry registry;
    // Platform top surface sits at y = 1.15.
    makeStaticBox(registry, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(4.0f, 0.3f, 4.0f));
    const auto falling = makeBox(registry, glm::vec3(0.0f, 5.0f, 0.0f));

    stepFor(registry, 4.0f);

    const float y = registry.get<TransformComponent>(falling).position.y;
    CHECK_MSG(y > 1.0f, "a body must land on the platform, not fall through to the ground plane");
    CHECK_MSG(y < 2.2f, "and must settle on it rather than being flung upward");
}

static void testStaticColliderDoesNotMove() {
    entt::registry registry;
    const auto platform = makeStaticBox(registry, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(4.0f, 0.3f, 4.0f));
    makeBox(registry, glm::vec3(0.0f, 3.0f, 0.0f), 50.0f);

    stepFor(registry, 2.0f);

    const auto& transform = registry.get<TransformComponent>(platform);
    CHECK_NEAR(transform.position.y, 1.0f);
    CHECK_NEAR(transform.position.x, 0.0f);
}

static void testHeavierBodyMovesLessThanLighterOne() {
    // mass was previously stored and never read, so both bodies would have been
    // displaced identically whatever their masses.
    entt::registry registry;
    const auto light = makeBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), 1.0f);
    const auto heavy = makeBox(registry, glm::vec3(0.6f, 5.0f, 0.0f), 100.0f);

    const float lightStart = registry.get<TransformComponent>(light).position.x;
    const float heavyStart = registry.get<TransformComponent>(heavy).position.x;

    PhysicsSystem::Update(registry, 1.0f / 60.0f);

    const float lightMoved = std::fabs(registry.get<TransformComponent>(light).position.x - lightStart);
    const float heavyMoved = std::fabs(registry.get<TransformComponent>(heavy).position.x - heavyStart);

    CHECK_MSG(lightMoved > heavyMoved, "the lighter body must absorb more of the separation");
    CHECK_MSG(heavyMoved < lightMoved * 0.2f, "and a 100:1 mass ratio must show clearly");
}

static void testSpheresSeparate() {
    entt::registry registry;
    const auto a = makeSphere(registry, glm::vec3(0.0f, 5.0f, 0.0f), 0.5f);
    const auto b = makeSphere(registry, glm::vec3(0.4f, 5.0f, 0.0f), 0.5f);

    stepFor(registry, 0.5f);

    const glm::vec3 delta = registry.get<TransformComponent>(b).position -
                            registry.get<TransformComponent>(a).position;
    CHECK_MSG(glm::length(delta) > 0.5f, "overlapping spheres must be pushed apart");
}

static void testSphereRestsOnBox() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(4.0f, 0.4f, 4.0f));
    const auto ball = makeSphere(registry, glm::vec3(0.0f, 5.0f, 0.0f), 0.5f);

    stepFor(registry, 4.0f);

    const float y = registry.get<TransformComponent>(ball).position.y;
    CHECK_MSG(y > 1.1f, "a sphere must rest on top of a box, not sink into it");
    CHECK_MSG(y < 2.2f, "and not be ejected");
}

static void testTriggerReportsButDoesNotResolve() {
    entt::registry registry;
    const auto trigger = registry.create();
    auto& triggerTransform = registry.emplace<TransformComponent>(trigger);
    triggerTransform.position = glm::vec3(0.0f, 5.0f, 0.0f);
    auto& triggerCollider = registry.emplace<BoxColliderComponent>(trigger);
    triggerCollider.isTrigger = true;

    const auto body = makeBox(registry, glm::vec3(0.1f, 5.0f, 0.0f));
    const float startX = registry.get<TransformComponent>(body).position.x;

    std::vector<PhysicsSystem::Contact> contacts;
    PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);

    CHECK_MSG(!contacts.empty(), "an overlap with a trigger must still be reported");
    bool sawTrigger = false;
    for (const auto& contact : contacts) {
        if (contact.isTrigger) sawTrigger = true;
    }
    CHECK_MSG(sawTrigger, "and must be flagged as a trigger contact");
    CHECK_NEAR(registry.get<TransformComponent>(body).position.x, startX);
}

static void testContactsAreClearedEachStep() {
    entt::registry registry;
    makeBox(registry, glm::vec3(0.0f, 5.0f, 0.0f));
    makeBox(registry, glm::vec3(0.4f, 5.0f, 0.0f));

    std::vector<PhysicsSystem::Contact> contacts;
    PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);
    const size_t first = contacts.size();
    CHECK_MSG(first > 0, "the overlap must be reported");

    PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);
    CHECK_MSG(contacts.size() <= first, "the list must be cleared per step, not appended to");
}

static void testKinematicBodyIsNotPushed() {
    entt::registry registry;
    const auto kinematic = makeBox(registry, glm::vec3(0.0f, 5.0f, 0.0f));
    registry.get<RigidBodyComponent>(kinematic).isKinematic = true;
    makeBox(registry, glm::vec3(0.5f, 5.0f, 0.0f));

    stepFor(registry, 0.5f);

    const auto& transform = registry.get<TransformComponent>(kinematic);
    CHECK_NEAR(transform.position.x, 0.0f);
    CHECK_NEAR(transform.position.y, 5.0f);
}

static void testDistantBodiesNeverContact() {
    entt::registry registry;
    makeBox(registry, glm::vec3(-40.0f, 5.0f, 0.0f));
    makeBox(registry, glm::vec3(40.0f, 5.0f, 0.0f));

    std::vector<PhysicsSystem::Contact> contacts;
    PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);
    CHECK_MSG(contacts.empty(), "bodies 80 units apart must not generate a contact");
}

static void testParentedBodyFallsInWorldSpace() {
    // The parent is turned 90 degrees about Y. Velocity is world space but
    // position is relative to the parent, so applying the fall to the local
    // position without converting sends the child sideways instead of down.
    entt::registry registry;

    const auto parent = registry.create();
    auto& parentTransform = registry.emplace<TransformComponent>(parent);
    parentTransform.position = glm::vec3(5.0f, 10.0f, 0.0f);
    parentTransform.rotation = glm::vec3(0.0f, glm::radians(90.0f), 0.0f);

    const auto child = registry.create();
    registry.emplace<TransformComponent>(child);
    registry.emplace<HierarchyComponent>(child, parent);
    registry.emplace<RigidBodyComponent>(child);
    registry.emplace<BoxColliderComponent>(child);

    TransformSystem::UpdateWorldTransforms(registry);
    const glm::vec3 startWorld = glm::vec3(registry.get<WorldTransformComponent>(child).matrix[3]);

    for (int i = 0; i < 30; ++i) {
        PhysicsSystem::Update(registry, 1.0f / 60.0f);
        TransformSystem::UpdateWorldTransforms(registry);
    }

    const glm::vec3 endWorld = glm::vec3(registry.get<WorldTransformComponent>(child).matrix[3]);

    CHECK_MSG(endWorld.y < startWorld.y - 0.05f, "a parented body must fall in world space");
    CHECK_NEAR(endWorld.x, startWorld.x);
    CHECK_NEAR(endWorld.z, startWorld.z);
}

static void testGravityAccelerates() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 10.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(entity);

    PhysicsSystem::Update(registry, 1.0f / 60.0f);

    const auto& body = registry.get<RigidBodyComponent>(entity);
    CHECK_MSG(body.velocity.y < 0.0f, "gravity must produce downward velocity");
    CHECK_MSG(transform.position.y < 10.0f, "the body must have fallen");
}

static void testKinematicBodiesDoNotMove() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 10.0f, 0.0f);
    auto& body = registry.emplace<RigidBodyComponent>(entity);
    body.isKinematic = true;

    stepFor(registry, 1.0f);
    CHECK_NEAR(transform.position.y, 10.0f);
}

static void testRestsOnColliderBottomNotOrigin() {
    // A unit cube scaled to 0.7 used to come to rest with its CENTRE at y = 0,
    // which buries it half-way through the floor it is supposedly resting on.
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 4.0f, 0.0f);
    transform.scale = glm::vec3(0.7f);
    registry.emplace<RigidBodyComponent>(entity);
    registry.emplace<BoxColliderComponent>(entity);

    stepFor(registry, 6.0f);

    const float expectedRestY = 0.5f * 0.7f; // half height above the plane
    CHECK_NEAR(transform.position.y, expectedRestY);
    CHECK_MSG(transform.position.y > 0.0f, "the body must rest ON the plane, not inside it");
}

static void testComesToRest() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 5.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(entity);
    registry.emplace<BoxColliderComponent>(entity);

    stepFor(registry, 10.0f);

    const auto& body = registry.get<RigidBodyComponent>(entity);
    CHECK_NEAR(body.velocity.y, 0.0f);
}

static void testRisingBodyIsNotReflected() {
    // Velocity used to be inverted on every frame the body was below the plane,
    // including frames where it was already moving upward, which re-launched
    // resting bodies.
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 0.1f, 0.0f);
    auto& body = registry.emplace<RigidBodyComponent>(entity);
    body.useGravity = false;
    body.velocity = glm::vec3(0.0f, 5.0f, 0.0f);
    registry.emplace<BoxColliderComponent>(entity);

    PhysicsSystem::Update(registry, 1.0f / 60.0f);

    CHECK_MSG(registry.get<RigidBodyComponent>(entity).velocity.y > 0.0f,
              "an upward-moving body must not have its velocity inverted");
}

static void testColliderSizeAffectsRestHeight() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 6.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(entity);
    auto& box = registry.emplace<BoxColliderComponent>(entity);
    box.size = glm::vec3(1.0f, 4.0f, 1.0f);

    stepFor(registry, 8.0f);
    CHECK_NEAR(transform.position.y, 2.0f);
}

static void testZeroAndNegativeDeltaAreIgnored() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 10.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(entity);

    PhysicsSystem::Update(registry, 0.0f);
    PhysicsSystem::Update(registry, -1.0f);

    CHECK_NEAR(transform.position.y, 10.0f);
}

static void testFixedStepDoesNotTunnel() {
    // Supersonic::Run feeds this a fixed 1/60 step. Handing the integrator a raw
    // two-second delta (a title-bar drag on Win32) moved a body 39 units in one
    // frame, straight through the floor.
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 2.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(entity);
    registry.emplace<BoxColliderComponent>(entity);

    // Two seconds of simulated time, delivered as fixed steps.
    stepFor(registry, 2.0f);

    CHECK_MSG(transform.position.y >= 0.0f, "a body must never end up below the ground plane");
    CHECK_MSG(transform.position.y < 3.0f, "and must not be launched into the air");
}


// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

static void testRaycastHitsTheNearestCollider() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -5.0f));
    makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -10.0f));

    const auto hit = PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 0.0f, 0.0f),
                                            glm::vec3(0.0f, 0.0f, -1.0f), 100.0f);
    CHECK_MSG(hit.hit, "the ray must find something");
    // Near face of the closer box: centre -5, half extent 0.5.
    CHECK_NEAR(hit.distance, 4.5f);
    CHECK_MSG(hit.normal.z > 0.9f, "the normal must face back along the ray");
}

static void testRaycastMissesAndRespectsRange() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -5.0f));

    CHECK_MSG(!PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 20.0f, 0.0f),
                                      glm::vec3(0.0f, 0.0f, -1.0f), 100.0f).hit,
              "a ray that passes overhead must miss");

    CHECK_MSG(!PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 0.0f, 0.0f),
                                      glm::vec3(0.0f, 0.0f, -1.0f), 2.0f).hit,
              "and a range shorter than the distance must not reach it");

    CHECK_MSG(!PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 0.0f, 0.0f),
                                      glm::vec3(0.0f, 0.0f, 1.0f), 100.0f).hit,
              "nor may a ray pointing the other way");
}

static void testRaycastIgnoresTheCasterAndTriggers() {
    entt::registry registry;
    const auto self = makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -2.0f));

    const auto trigger = registry.create();
    auto& triggerTransform = registry.emplace<TransformComponent>(trigger);
    triggerTransform.position = glm::vec3(0.0f, 0.0f, -6.0f);
    registry.emplace<BoxColliderComponent>(trigger).isTrigger = true;

    makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -9.0f));

    // Ignoring the caster is what stops a character's own collider from
    // absorbing every shot it fires.
    const auto hit = PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 0.0f, 0.0f),
                                            glm::vec3(0.0f, 0.0f, -1.0f), 100.0f, self);
    CHECK_MSG(hit.hit, "something must still be hit");
    CHECK_NEAR(hit.distance, 8.5f);   // the solid box, not the trigger at 5.5

    const auto withTriggers = PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 0.0f, 0.0f),
                                                     glm::vec3(0.0f, 0.0f, -1.0f), 100.0f,
                                                     self, /*includeTriggers=*/true);
    CHECK_NEAR(withTriggers.distance, 5.5f);
}

static void testRaycastHitsSpheres() {
    entt::registry registry;
    const auto ball = registry.create();
    auto& transform = registry.emplace<TransformComponent>(ball);
    transform.position = glm::vec3(0.0f, 0.0f, -4.0f);
    registry.emplace<SphereColliderComponent>(ball).radius = 1.0f;

    const auto hit = PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 0.0f, 0.0f),
                                            glm::vec3(0.0f, 0.0f, -1.0f), 100.0f);
    CHECK_MSG(hit.hit, "a sphere collider must be hittable");
    CHECK_NEAR(hit.distance, 3.0f);
    CHECK_MSG(hit.entity == ball, "and report which entity was hit");
}

static void testRaycastRejectsDegenerateInput() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -5.0f));

    // A zero direction would normalise to NaN and report an impossible hit.
    CHECK_MSG(!PhysicsSystem::Raycast(registry, glm::vec3(0.0f), glm::vec3(0.0f), 100.0f).hit,
              "a zero-length direction must simply miss");
    CHECK_MSG(!PhysicsSystem::Raycast(registry, glm::vec3(0.0f),
                                      glm::vec3(0.0f, 0.0f, -1.0f), -1.0f).hit,
              "and a negative range must too");
}

static void testRaycastDirectionNeedNotBeNormalised() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -5.0f));

    const auto hit = PhysicsSystem::Raycast(registry, glm::vec3(0.0f),
                                            glm::vec3(0.0f, 0.0f, -7.3f), 100.0f);
    CHECK_MSG(hit.hit, "an unnormalised direction must work");
    CHECK_NEAR(hit.distance, 4.5f);
}

static void testOverlapSphereFindsWhatItTouches() {
    entt::registry registry;
    const auto near = makeStaticBox(registry, glm::vec3(1.0f, 0.0f, 0.0f));
    makeStaticBox(registry, glm::vec3(30.0f, 0.0f, 0.0f));

    std::vector<entt::entity> found;
    PhysicsSystem::OverlapSphere(registry, glm::vec3(0.0f), 2.0f, found);

    CHECK_EQ(found.size(), size_t{1});
    if (!found.empty()) CHECK_MSG(found[0] == near, "the near box, not the far one");

    // Appends rather than clears, so several queries can accumulate.
    PhysicsSystem::OverlapSphere(registry, glm::vec3(0.0f), 2.0f, found);
    CHECK_EQ(found.size(), size_t{2});
}

static void testGroundCheck() {
    entt::registry registry;
    // A platform whose top surface is at y = 1.15.
    makeStaticBox(registry, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(4.0f, 0.3f, 4.0f));

    CHECK_MSG(PhysicsSystem::IsGrounded(registry, glm::vec3(0.0f, 1.2f, 0.0f), 0.2f),
              "standing just above the platform is grounded");
    CHECK_MSG(!PhysicsSystem::IsGrounded(registry, glm::vec3(0.0f, 6.0f, 0.0f), 0.2f),
              "six units up is not");

    // The world ground plane counts even though no entity represents it.
    entt::registry empty;
    CHECK_MSG(PhysicsSystem::IsGrounded(empty, glm::vec3(0.0f, 0.05f, 0.0f), 0.2f),
              "the world plane counts as ground");
}

// --- materials --------------------------------------------------------------
//
// Bounce and grip used to be two constants for the whole world, so a rubber
// ball and a wooden crate behaved identically. That is not a tuning problem;
// it is the absence of the setting.

static void testRestitutionTakesTheBouncierSurface() {
    // A superball dropped on concrete bounces. Taking the smaller - or the
    // average - would mean any dead surface killed every ball that touched it.
    CHECK_NEAR(PhysicsSystem::CombineRestitution(0.9f, 0.0f), 0.9f);
    CHECK_NEAR(PhysicsSystem::CombineRestitution(0.2f, 0.5f), 0.5f);
}

static void testRestitutionCannotGainEnergy() {
    // Above 1 a body leaves every impact faster than it arrived, and the scene
    // shakes itself apart within seconds.
    CHECK_MSG(PhysicsSystem::CombineRestitution(5.0f, 5.0f) < 1.0f,
              "restitution must stay below 1 or bouncing adds energy");
    CHECK_MSG(PhysicsSystem::CombineRestitution(-3.0f, 0.0f) >= 0.0f,
              "and negative restitution must not suck bodies together");
}

static void testFrictionIsZeroIfEitherSurfaceIsIce() {
    // The geometric mean has the property that matters: ice against anything
    // is still slippery. An average would let a rough floor grip a puck.
    CHECK_NEAR(PhysicsSystem::CombineFriction(0.0f, 1.0f), 0.0f);
    CHECK_NEAR(PhysicsSystem::CombineFriction(0.4f, 0.4f), 0.4f);
}

static void testABouncierBallReboundsHigher() {
    // The property has to reach the solver, not merely be stored.
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f));

    const auto dead = makeSphere(registry, glm::vec3(-4.0f, 3.0f, 0.0f));
    registry.get<RigidBodyComponent>(dead).restitution = 0.0f;

    const auto bouncy = makeSphere(registry, glm::vec3(4.0f, 3.0f, 0.0f));
    registry.get<RigidBodyComponent>(bouncy).restitution = 0.9f;

    stepFor(registry, 2.0f);

    const float deadHeight = registry.get<TransformComponent>(dead).position.y;
    const float bouncyHeight = registry.get<TransformComponent>(bouncy).position.y;

    CHECK_MSG(bouncyHeight > deadHeight + 0.05f,
              "the bouncier ball must still be higher: dead " + std::to_string(deadHeight) +
                  " vs bouncy " + std::to_string(bouncyHeight));
}

static void testDampingSlowsABodyWithNothingTouchingIt() {
    entt::registry registry;

    const auto drifting = registry.create();
    registry.emplace<TransformComponent>(drifting, glm::vec3(0.0f, 50.0f, 0.0f));
    auto& body = registry.emplace<RigidBodyComponent>(drifting);
    body.useGravity = false;
    body.velocity = glm::vec3(10.0f, 0.0f, 0.0f);
    body.linearDamping = 0.9f;

    stepFor(registry, 1.0f);

    const float speed = glm::length(registry.get<RigidBodyComponent>(drifting).velocity);
    CHECK_MSG(speed < 5.0f, "damping must bleed off speed: got " + std::to_string(speed));
    CHECK_MSG(speed > 0.0f, "but not stop the body dead in one second");
}

// --- rotation ---------------------------------------------------------------
//
// Nothing rotated at all: a crate dropped on its corner landed flat, a ball
// never rolled, and a hit off the centre of mass pushed a body without turning
// it.

static void testAngularVelocityTurnsTheTransform() {
    entt::registry registry;

    const auto spinner = registry.create();
    registry.emplace<TransformComponent>(spinner, glm::vec3(0.0f, 50.0f, 0.0f));
    auto& body = registry.emplace<RigidBodyComponent>(spinner);
    body.useGravity = false;
    body.angularDamping = 0.0f;
    // A quarter turn per second about Y.
    body.angularVelocity = glm::vec3(0.0f, glm::half_pi<float>(), 0.0f);

    stepFor(registry, 1.0f);

    // Compared as a rotation rather than as three numbers: the Euler triple
    // for a given orientation is not unique, so checking the angles directly
    // fails on a representation change that means nothing.
    const glm::quat actual(registry.get<TransformComponent>(spinner).rotation);
    const glm::quat expected =
        glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));

    const float alignment = std::abs(glm::dot(glm::normalize(actual), glm::normalize(expected)));
    CHECK_MSG(alignment > 0.99f,
              "a quarter turn per second must be a quarter turn after a second: "
              "alignment " + std::to_string(alignment));
}

static void testFrozenRotationNeverTurns() {
    // A character or a camera boom wants to be shoved around without tipping.
    entt::registry registry;

    const auto upright = registry.create();
    registry.emplace<TransformComponent>(upright, glm::vec3(0.0f, 50.0f, 0.0f));
    auto& body = registry.emplace<RigidBodyComponent>(upright);
    body.useGravity = false;
    body.freezeRotation = true;
    body.angularVelocity = glm::vec3(3.0f, 3.0f, 3.0f);

    stepFor(registry, 1.0f);

    const glm::vec3 rotation = registry.get<TransformComponent>(upright).rotation;
    CHECK_MSG(glm::length(rotation) < 1e-4f, "a frozen body must not turn at all");
    CHECK_MSG(glm::length(registry.get<RigidBodyComponent>(upright).angularVelocity) < 1e-6f,
              "and its spin must be cleared, not merely ignored");
}

static void testAngularDampingSlowsSpin() {
    entt::registry registry;

    const auto spinner = registry.create();
    registry.emplace<TransformComponent>(spinner, glm::vec3(0.0f, 50.0f, 0.0f));
    auto& body = registry.emplace<RigidBodyComponent>(spinner);
    body.useGravity = false;
    body.angularVelocity = glm::vec3(0.0f, 8.0f, 0.0f);
    body.angularDamping = 0.9f;

    stepFor(registry, 1.0f);

    const float speed = registry.get<RigidBodyComponent>(spinner).angularVelocity.y;
    CHECK_MSG(speed < 4.0f, "spin must decay: got " + std::to_string(speed));
    CHECK_MSG(speed > 0.0f, "but not reverse");
}

static void testAnOffCentreImpactCreatesSpin() {
    // The whole point. A box landing with only one corner over an obstacle
    // must start turning; before this it stayed perfectly level.
    entt::registry registry;

    // A small static block, offset so the falling box lands on its edge.
    const auto block = registry.create();
    registry.emplace<TransformComponent>(block, glm::vec3(0.9f, 0.0f, 0.0f));
    registry.emplace<BoxColliderComponent>(block);

    const auto falling = makeBox(registry, glm::vec3(0.0f, 1.6f, 0.0f));
    registry.get<RigidBodyComponent>(falling).angularDamping = 0.0f;

    stepFor(registry, 0.6f);

    const glm::vec3 spin = registry.get<RigidBodyComponent>(falling).angularVelocity;
    CHECK_MSG(glm::length(spin) > 0.05f,
              "landing on one corner must impart spin: got " + std::to_string(glm::length(spin)));

    // And about the right axis: the contact is offset along x, so the box
    // tips about z. A spin about the wrong axis would look like the box
    // spinning on the spot instead of toppling.
    CHECK_MSG(std::abs(spin.z) > std::abs(spin.y),
              "a contact offset along x must tip the box about z, not spin it about y");
}

static void testACentredImpactCreatesNoSpin() {
    // The control for the test above. A box landing squarely must not start
    // turning, or every stack in the scene would slowly come apart.
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f));

    const auto falling = makeBox(registry, glm::vec3(0.0f, 2.0f, 0.0f));
    registry.get<RigidBodyComponent>(falling).angularDamping = 0.0f;

    stepFor(registry, 1.5f);

    const float spin = glm::length(registry.get<RigidBodyComponent>(falling).angularVelocity);
    CHECK_MSG(spin < 0.05f,
              "a square landing must not impart spin: got " + std::to_string(spin));
}

static void testSpinDoesNotAppearFromNothing() {
    // A body resting undisturbed must stay still. Angular terms that leak
    // energy show up here first, as a stack that slowly rotates itself apart.
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f));

    // On a block well above the world ground plane, so it is genuinely
    // resting on a collider rather than on the integrator's floor clamp.
    const auto block = registry.create();
    registry.emplace<TransformComponent>(block, glm::vec3(0.0f, 1.0f, 0.0f),
                                         glm::vec3(0.0f), glm::vec3(6.0f, 1.0f, 6.0f));
    registry.emplace<BoxColliderComponent>(block);

    const auto resting = makeBox(registry, glm::vec3(0.0f, 2.05f, 0.0f));
    auto& body = registry.get<RigidBodyComponent>(resting);
    body.angularDamping = 0.0f;
    body.velocity = glm::vec3(0.0f);

    std::vector<PhysicsSystem::Contact> contacts;
    bool touched = false;
    for (int i = 0; i < 180; ++i) {
        PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);
        if (!contacts.empty()) touched = true;
    }
    // Without this the test could pass by never touching the floor at all,
    // which is the one way a stability test silently stops covering anything.
    CHECK_MSG(touched, "the body must actually be resting ON something");

    const float spin = glm::length(registry.get<RigidBodyComponent>(resting).angularVelocity);
    CHECK_MSG(spin < 0.05f, "a resting body must not spin up: got " + std::to_string(spin));
}

static void testALongBoxIsHarderToTipAboutItsLongAxis() {
    // Inertia has to depend on the shape, not just the mass. A crate three
    // times longer in x resists turning about z far more than a cube of the
    // same mass, and a solver using mass alone would spin both identically.
    //
    // The comparison is controlled: same mass, same drop, and blocks placed so
    // the contact lands the same distance from each centre of mass, because
    // torque is force times arm and an uncontrolled arm would explain any
    // difference by itself.
    //
    // Everything sits well above y = 0: below that the integrator's world
    // ground plane catches a body before it ever reaches a collider, which is
    // how the first version of this test managed to make no contact at all.
    entt::registry registry;
    const float arm = 0.35f;

    const auto cube = makeBox(registry, glm::vec3(-20.0f, 2.5f, 0.0f), 1.0f, glm::vec3(1.0f));
    registry.get<RigidBodyComponent>(cube).angularDamping = 0.0f;
    const auto cubeBlock = registry.create();
    registry.emplace<TransformComponent>(cubeBlock, glm::vec3(-20.0f + arm, 1.0f, 0.0f));
    registry.emplace<BoxColliderComponent>(cubeBlock);

    const auto longBox = makeBox(registry, glm::vec3(20.0f, 2.5f, 0.0f), 1.0f,
                                 glm::vec3(3.0f, 1.0f, 1.0f));
    registry.get<RigidBodyComponent>(longBox).angularDamping = 0.0f;
    const auto longBlock = registry.create();
    registry.emplace<TransformComponent>(longBlock, glm::vec3(20.0f + arm, 1.0f, 0.0f));
    registry.emplace<BoxColliderComponent>(longBlock);

    std::vector<PhysicsSystem::Contact> contacts;
    bool touched = false;
    for (int i = 0; i < 45; ++i) {
        PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);
        if (!contacts.empty()) touched = true;
    }
    CHECK_MSG(touched, "the boxes must actually land on their blocks");

    const float cubeSpin = std::abs(registry.get<RigidBodyComponent>(cube).angularVelocity.z);
    const float longSpin = std::abs(registry.get<RigidBodyComponent>(longBox).angularVelocity.z);

    CHECK_MSG(cubeSpin > 1e-3f, "the cube must tip: got " + std::to_string(cubeSpin));
    CHECK_MSG(longSpin > 1e-4f, "and so must the long box: got " + std::to_string(longSpin));
    CHECK_MSG(cubeSpin > longSpin * 1.5f,
              "the longer box resists turning about z: cube " + std::to_string(cubeSpin) +
                  " vs long " + std::to_string(longSpin));
}

static void runTests() {
    testSweepAndPruneFindsOverlappingPairs();
    testSweepAndPruneSkipsPairsSeparatedOffAxis();
    testSweepAndPruneIgnoresTwoStatics();
    testBoxesSeparateInsteadOfOverlapping();
    testBodyRestsOnStaticPlatformNotThroughIt();
    testStaticColliderDoesNotMove();
    testHeavierBodyMovesLessThanLighterOne();
    testSpheresSeparate();
    testSphereRestsOnBox();
    testTriggerReportsButDoesNotResolve();
    testContactsAreClearedEachStep();
    testKinematicBodyIsNotPushed();
    testDistantBodiesNeverContact();
    testParentedBodyFallsInWorldSpace();
    testRaycastHitsTheNearestCollider();
    testRaycastMissesAndRespectsRange();
    testRaycastIgnoresTheCasterAndTriggers();
    testRaycastHitsSpheres();
    testRaycastRejectsDegenerateInput();
    testRaycastDirectionNeedNotBeNormalised();
    testOverlapSphereFindsWhatItTouches();
    testGroundCheck();
    testGravityAccelerates();
    testKinematicBodiesDoNotMove();
    testRestsOnColliderBottomNotOrigin();
    testComesToRest();
    testRisingBodyIsNotReflected();
    testColliderSizeAffectsRestHeight();
    testZeroAndNegativeDeltaAreIgnored();
    testFixedStepDoesNotTunnel();

    testRestitutionTakesTheBouncierSurface();
    testRestitutionCannotGainEnergy();
    testFrictionIsZeroIfEitherSurfaceIsIce();
    testABouncierBallReboundsHigher();
    testDampingSlowsABodyWithNothingTouchingIt();

    testAngularVelocityTurnsTheTransform();
    testFrozenRotationNeverTurns();
    testAngularDampingSlowsSpin();
    testAnOffCentreImpactCreatesSpin();
    testACentredImpactCreatesNoSpin();
    testSpinDoesNotAppearFromNothing();
    testALongBoxIsHarderToTipAboutItsLongAxis();
}

TEST_MAIN("test_physics", 56)
