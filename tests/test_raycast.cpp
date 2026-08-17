// Regression tests for viewport picking.
//
// Two independent bugs made selection unusable: the ray builder applied the
// OpenGL NDC convention on top of an already Y-flipped Vulkan projection, and
// the slab test never clamped the near parameter on the Z axis.

#include "TestHarness.hpp"
#include "core/Raycast.hpp"
#include "core/TransformSystem.hpp"

using namespace Engine;

static CameraComponent makeCamera() {
    CameraComponent cam;
    cam.fov = 45.0f;
    cam.aspect = 1.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 100.0f;
    cam.position = glm::vec3(0.0f, 0.0f, 5.0f);
    cam.yaw = -90.0f;
    cam.pitch = 0.0f;
    cam.updateCameraVectors();
    return cam;
}

static void testRayCentreIsForward() {
    const CameraComponent cam = makeCamera();
    const Ray ray = Raycast::ScreenPointToRay({50.0f, 50.0f}, {100.0f, 100.0f}, cam);

    CHECK_NEAR(ray.direction.x, 0.0f);
    CHECK_NEAR(ray.direction.y, 0.0f);
    CHECK_MSG(ray.direction.z < 0.0f, "centre of viewport must look down -Z");
    CHECK_NEAR(glm::length(ray.direction), 1.0f);
}

static void testRayIsNotVerticallyMirrored() {
    const CameraComponent cam = makeCamera();

    // Screen Y grows downward, so the top of the viewport must produce a ray
    // aimed at world +Y. The old code negated this and mirrored every pick
    // about the horizontal centreline.
    const Ray top = Raycast::ScreenPointToRay({50.0f, 5.0f}, {100.0f, 100.0f}, cam);
    const Ray bottom = Raycast::ScreenPointToRay({50.0f, 95.0f}, {100.0f, 100.0f}, cam);

    CHECK_MSG(top.direction.y > 0.0f, "clicking near the top must aim upward (+Y)");
    CHECK_MSG(bottom.direction.y < 0.0f, "clicking near the bottom must aim downward (-Y)");
}

static void testRayHorizontalOrientation() {
    const CameraComponent cam = makeCamera();
    const Ray right = Raycast::ScreenPointToRay({95.0f, 50.0f}, {100.0f, 100.0f}, cam);
    const Ray left = Raycast::ScreenPointToRay({5.0f, 50.0f}, {100.0f, 100.0f}, cam);

    CHECK_MSG(right.direction.x > 0.0f, "clicking right of centre must aim toward +X");
    CHECK_MSG(left.direction.x < 0.0f, "clicking left of centre must aim toward -X");
}

static void testSlabEntryDistanceIsExact() {
    // A unit box centred at the origin, hit head-on from z = +5. The true entry
    // parameter is 4.5. Omitting the Z clamp left tNear at the X/Y slab value,
    // which for an axis-aligned ray is -infinity.
    const glm::vec3 boundsMin(-0.5f), boundsMax(0.5f);
    float tNear = 0.0f, tFar = 0.0f;

    const bool hit = Raycast::IntersectRayAABB(
        glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f),
        boundsMin, boundsMax, tNear, tFar);

    CHECK(hit);
    CHECK_NEAR(tNear, 4.5f);
    CHECK_NEAR(tFar, 5.5f);
}

static void testDepthSortOrderingIsCorrect() {
    // Two boxes along the same ray. The near one must report the smaller entry
    // distance, which is exactly what a missing clamp used to break.
    float nearT = 0.0f, nearFar = 0.0f, farT = 0.0f, farFar = 0.0f;

    const bool a = Raycast::IntersectRayAABB(
        glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(-0.5f, -0.5f, 1.5f), glm::vec3(0.5f, 0.5f, 2.5f), nearT, nearFar);

    const bool b = Raycast::IntersectRayAABB(
        glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(-0.5f, -0.5f, -2.5f), glm::vec3(0.5f, 0.5f, -1.5f), farT, farFar);

    CHECK(a);
    CHECK(b);
    CHECK_MSG(nearT < farT, "nearer box must have the smaller entry distance");
}

static void testRayOriginInsideBox() {
    // The camera sitting inside the X and Y slabs used to make the whole test
    // reject, so clicking an object dead ahead selected nothing.
    float tNear = 0.0f, tFar = 0.0f;
    const bool hit = Raycast::IntersectRayAABB(
        glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(-1.0f), glm::vec3(1.0f), tNear, tFar);

    CHECK(hit);
    CHECK_MSG(tNear < 0.0f, "origin inside the box means a negative entry parameter");
    CHECK_NEAR(tFar, 1.0f);
}

static void testParallelRayMisses() {
    // Parallel to the Z slab and outside it. Substituting a tiny epsilon
    // divisor instead produced a huge finite t and a bogus hit.
    float tNear = 0.0f, tFar = 0.0f;
    const bool hit = Raycast::IntersectRayAABB(
        glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(-0.5f), glm::vec3(0.5f), tNear, tFar);

    CHECK_MSG(!hit, "ray parallel to and outside a slab must miss");
}

static void testBoxBehindRayMisses() {
    float tNear = 0.0f, tFar = 0.0f;
    const bool hit = Raycast::IntersectRayAABB(
        glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(-0.5f), glm::vec3(0.5f), tNear, tFar);

    CHECK_MSG(!hit, "a box entirely behind the ray must not be picked");
}

static void testPickHonoursRotation() {
    entt::registry registry;

    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
    transform.scale = glm::vec3(1.0f, 1.0f, 8.0f); // long along Z
    registry.emplace<RenderableComponent>(entity);

    // Aim at a point far along +X. Unrotated, the thin box does not reach it.
    const Ray ray{ glm::vec3(3.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f) };
    TransformSystem::UpdateWorldTransforms(registry);
    CHECK_MSG(Raycast::PickEntity(registry, ray) == entt::null,
              "unrotated thin box should not be under the ray");

    // Rotating 90 degrees about Y swings its long axis onto X, bringing it
    // under the ray. A world-space AABB built from position +/- scale ignores
    // rotation entirely and cannot distinguish these two cases.
    transform.rotation = glm::vec3(0.0f, glm::radians(90.0f), 0.0f);
    TransformSystem::UpdateWorldTransforms(registry);
    CHECK_MSG(Raycast::PickEntity(registry, ray) == entity,
              "rotated box should now be under the ray");
}

static void testPickUsesColliderSize() {
    entt::registry registry;

    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f);
    registry.emplace<RenderableComponent>(entity);

    const Ray ray{ glm::vec3(2.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f) };
    TransformSystem::UpdateWorldTransforms(registry);
    CHECK(Raycast::PickEntity(registry, ray) == entt::null);

    // BoxColliderComponent::size used to be an editor control that affected
    // nothing at all.
    auto& box = registry.emplace<BoxColliderComponent>(entity);
    box.size = glm::vec3(6.0f, 1.0f, 1.0f);
    TransformSystem::UpdateWorldTransforms(registry);
    CHECK_MSG(Raycast::PickEntity(registry, ray) == entity,
              "widening the collider must widen the pick volume");
}

static void testPickFollowsParenting() {
    // Picking reads world matrices, so a child must be selectable where its
    // parent has moved it to, not at its local offset.
    entt::registry registry;

    const auto parent = registry.create();
    registry.emplace<TransformComponent>(parent, glm::vec3(6.0f, 0.0f, 0.0f));

    const auto child = registry.create();
    registry.emplace<TransformComponent>(child, glm::vec3(0.0f));
    registry.emplace<RenderableComponent>(child);
    registry.emplace<HierarchyComponent>(child, parent);

    TransformSystem::UpdateWorldTransforms(registry);

    const Ray atParent{ glm::vec3(6.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f) };
    const Ray atOrigin{ glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f) };

    CHECK_MSG(Raycast::PickEntity(registry, atParent) == child,
              "the child must be pickable where its parent placed it");
    CHECK_MSG(Raycast::PickEntity(registry, atOrigin) == entt::null,
              "and not at its local origin");
}

static void runTests() {
    testRayCentreIsForward();
    testRayIsNotVerticallyMirrored();
    testRayHorizontalOrientation();
    testSlabEntryDistanceIsExact();
    testDepthSortOrderingIsCorrect();
    testRayOriginInsideBox();
    testParallelRayMisses();
    testBoxBehindRayMisses();
    testPickHonoursRotation();
    testPickUsesColliderSize();
    testPickFollowsParenting();
}

TEST_MAIN("test_raycast")
