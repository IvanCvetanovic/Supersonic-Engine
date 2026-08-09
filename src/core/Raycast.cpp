#include "core/Raycast.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace Engine {

Ray Raycast::ScreenPointToRay(
    const glm::vec2& mousePos,
    const glm::vec2& viewportSize,
    const CameraComponent& camera) {

    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return Ray{};

    // Normalized Device Coordinates (-1 to +1)
    float ndcX = (2.0f * mousePos.x) / viewportSize.x - 1.0f;
    float ndcY = 1.0f - (2.0f * mousePos.y) / viewportSize.y;

    glm::vec4 clipCoords(ndcX, ndcY, -1.0f, 1.0f);
    glm::mat4 invProj = glm::inverse(camera.getProjectionMatrix());
    glm::vec4 eyeCoords = invProj * clipCoords;
    eyeCoords = glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f);

    glm::mat4 invView = glm::inverse(camera.getViewMatrix());
    glm::vec3 worldDir = glm::normalize(glm::vec3(invView * eyeCoords));

    Ray ray;
    ray.origin = camera.position;
    ray.direction = worldDir;
    return ray;
}

entt::entity Raycast::PickEntity(entt::registry& registry, const Ray& ray) {
    entt::entity closestEntity = entt::null;
    float minDistance = std::numeric_limits<float>::max();

    auto view = registry.view<TransformComponent, RenderableComponent>();
    for (auto entity : view) {
        const auto& transform = view.get<TransformComponent>(entity);

        glm::vec3 minBounds = transform.position - (transform.scale * 0.5f);
        glm::vec3 maxBounds = transform.position + (transform.scale * 0.5f);

        float tMin = (minBounds.x - ray.origin.x) / (ray.direction.x != 0.0f ? ray.direction.x : 0.0001f);
        float tMax = (maxBounds.x - ray.origin.x) / (ray.direction.x != 0.0f ? ray.direction.x : 0.0001f);

        if (tMin > tMax) std::swap(tMin, tMax);

        float tyMin = (minBounds.y - ray.origin.y) / (ray.direction.y != 0.0f ? ray.direction.y : 0.0001f);
        float tyMax = (maxBounds.y - ray.origin.y) / (ray.direction.y != 0.0f ? ray.direction.y : 0.0001f);

        if (tyMin > tyMax) std::swap(tyMin, tyMax);

        if ((tMin > tyMax) || (tyMin > tMax)) continue;

        if (tyMin > tMin) tMin = tyMin;
        if (tyMax < tMax) tMax = tyMax;

        float tzMin = (minBounds.z - ray.origin.z) / (ray.direction.z != 0.0f ? ray.direction.z : 0.0001f);
        float tzMax = (maxBounds.z - ray.origin.z) / (ray.direction.z != 0.0f ? ray.direction.z : 0.0001f);

        if (tzMin > tzMax) std::swap(tzMin, tzMax);

        if ((tMin > tzMax) || (tzMin > tMax)) continue;

        if (tMin > 0.0f && tMin < minDistance) {
            minDistance = tMin;
            closestEntity = entity;
        }
    }

    return closestEntity;
}

} // namespace Engine
