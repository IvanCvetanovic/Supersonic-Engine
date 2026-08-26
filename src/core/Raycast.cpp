#include "core/Raycast.hpp"
#include "core/TransformSystem.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace Supersonic {

Ray Raycast::ScreenPointToRay(
    const glm::vec2& mousePos,
    const glm::vec2& viewportSize,
    const CameraComponent& camera) {

    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return Ray{};

    // Vulkan clip space has +Y pointing DOWN, and getProjectionMatrix already
    // negates the Y row for that. Screen mouse coordinates also grow downward,
    // so NDC Y is a straight remap.
    //
    // The old code used the OpenGL convention (1 - 2y/h) and then unprojected
    // through the already-Y-flipped matrix, double-negating Y and mirroring
    // every pick about the horizontal centreline.
    const float ndcX = (2.0f * mousePos.x) / viewportSize.x - 1.0f;
    const float ndcY = (2.0f * mousePos.y) / viewportSize.y - 1.0f;

    const glm::mat4 invProj = glm::inverse(camera.getProjectionMatrix());

    // ORTHOGRAPHIC first, because it is the case the rest of this function is
    // wrong for.
    //
    // Under perspective every ray leaves the same point and only the DIRECTION
    // varies with the pixel, which is why the code below unprojects a direction
    // and hangs it off camera.position. Under orthographic that is exactly
    // inverted: the direction is the same everywhere and the ORIGIN varies. Run
    // the perspective arithmetic on an orthographic camera and every pick
    // resolves as though it were taken from the centre of the screen - the
    // failure is invisible in the middle of the viewport and grows toward the
    // edges, which is the worst way for a bug like this to present.
    if (camera.isOrthographic()) {
        // The near-plane point this pixel looks through, in eye space. w is 1
        // for an orthographic projection, so no divide is needed - but doing it
        // costs nothing and survives someone changing the matrix.
        glm::vec4 nearEye = invProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        if (std::fabs(nearEye.w) > 1e-8f) nearEye /= nearEye.w;

        const glm::mat4 invView = glm::inverse(camera.getViewMatrix());

        Ray ray;
        ray.origin = glm::vec3(invView * glm::vec4(glm::vec3(nearEye), 1.0f));
        // Eye space looks down -Z, and the view matrix is a rigid transform, so
        // its inverse takes that to the camera's world forward.
        ray.direction = glm::normalize(glm::vec3(invView * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        return ray;
    }

    glm::vec4 eyeCoords = invProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    if (std::fabs(eyeCoords.w) > 1e-8f) {
        eyeCoords /= eyeCoords.w;
    }
    // Direction only; drop the position and w.
    eyeCoords = glm::vec4(eyeCoords.x, eyeCoords.y, eyeCoords.z, 0.0f);

    const glm::mat4 invView = glm::inverse(camera.getViewMatrix());
    const glm::vec3 worldDir = glm::vec3(invView * eyeCoords);

    Ray ray;
    ray.origin = camera.position;
    const float len = glm::length(worldDir);
    ray.direction = len > 1e-8f ? worldDir / len : glm::vec3(0.0f, 0.0f, -1.0f);
    return ray;
}

bool Raycast::IntersectRayAABB(const glm::vec3& origin,
                               const glm::vec3& direction,
                               const glm::vec3& boundsMin,
                               const glm::vec3& boundsMax,
                               float& outTNear,
                               float& outTFar) {
    float tNear = -std::numeric_limits<float>::infinity();
    float tFar  =  std::numeric_limits<float>::infinity();

    for (int axis = 0; axis < 3; ++axis) {
        const float o = origin[axis];
        const float d = direction[axis];
        const float lo = boundsMin[axis];
        const float hi = boundsMax[axis];

        if (std::fabs(d) < 1e-8f) {
            // Ray is parallel to this slab: either it is inside it forever, or
            // it never enters. Substituting a tiny epsilon divisor instead
            // produced enormous finite t values that corrupted the comparison.
            if (o < lo || o > hi) return false;
            continue;
        }

        float t1 = (lo - o) / d;
        float t2 = (hi - o) / d;
        if (t1 > t2) std::swap(t1, t2);

        // Both clamps are required on every axis. Omitting the Z clamp left
        // tNear below the true entry point, so the depth sort was wrong on
        // every hit and boxes containing the ray origin in X and Y were
        // rejected outright.
        tNear = std::max(tNear, t1);
        tFar  = std::min(tFar, t2);

        if (tNear > tFar) return false;
    }

    if (tFar < 0.0f) return false; // box is entirely behind the ray

    outTNear = tNear;
    outTFar = tFar;
    return true;
}

entt::entity Raycast::PickEntity(entt::registry& registry, const Ray& ray) {
    entt::entity closestEntity = entt::null;
    float minDistance = std::numeric_limits<float>::max();

    auto view = registry.view<WorldTransformComponent, RenderableComponent>();
    for (auto entity : view) {
        const auto& renderable = view.get<RenderableComponent>(entity);

        if (!renderable.isVisible) continue;

        // Test in the entity's local space so rotation and non-uniform scale
        // are handled exactly, instead of approximating with a world-space AABB
        // that ignores both.
        // World matrix, so picking follows parenting.
        const glm::mat4 model = view.get<WorldTransformComponent>(entity).matrix;
        const float det = glm::determinant(model);
        if (std::fabs(det) < 1e-12f) continue; // degenerate (zero scale)

        const glm::mat4 invModel = glm::inverse(model);
        const glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(ray.origin, 1.0f));
        // Not renormalised: keeping the transformed length means the returned
        // t stays measured along the original world-space ray, so distances
        // remain comparable between entities with different scales.
        const glm::vec3 localDir = glm::vec3(invModel * glm::vec4(ray.direction, 0.0f));

        glm::vec3 boundsMin = renderable.localBoundsMin;
        glm::vec3 boundsMax = renderable.localBoundsMax;

        // An explicit collider overrides the mesh bounds.
        if (const auto* box = registry.try_get<BoxColliderComponent>(entity)) {
            boundsMin = -box->size * 0.5f;
            boundsMax = box->size * 0.5f;
        } else if (const auto* sphere = registry.try_get<SphereColliderComponent>(entity)) {
            boundsMin = glm::vec3(-sphere->radius);
            boundsMax = glm::vec3(sphere->radius);
        }

        float tNear = 0.0f;
        float tFar = 0.0f;
        if (!IntersectRayAABB(localOrigin, localDir, boundsMin, boundsMax, tNear, tFar)) {
            continue;
        }

        // When the origin is inside the box, tNear is negative; the first
        // surface ahead of the viewer is tFar.
        const float hitDistance = tNear >= 0.0f ? tNear : tFar;
        if (hitDistance >= 0.0f && hitDistance < minDistance) {
            minDistance = hitDistance;
            closestEntity = entity;
        }
    }

    return closestEntity;
}

} // namespace Supersonic
