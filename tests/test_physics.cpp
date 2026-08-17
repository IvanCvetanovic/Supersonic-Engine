// Regression tests for the physics step.
//
// The integrator ran on the raw wall-clock frame delta with no clamp, and
// ground collision clamped the transform ORIGIN to y = 0 while reflecting
// velocity unconditionally.

#include "TestHarness.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/Components.hpp"

using namespace Supersonic;

static void stepFor(entt::registry& registry, float seconds, float step = 1.0f / 60.0f) {
    for (float t = 0.0f; t < seconds; t += step) {
        PhysicsSystem::Update(registry, step);
    }
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

static void runTests() {
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
