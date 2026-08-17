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
}

TEST_MAIN("test_physics")
