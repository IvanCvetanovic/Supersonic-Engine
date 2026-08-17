#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/Components.hpp"

namespace Supersonic {

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

class Raycast {
public:
    // mousePos is relative to the viewport's top-left corner, in pixels.
    static Ray ScreenPointToRay(const glm::vec2& mousePos,
                                const glm::vec2& viewportSize,
                                const CameraComponent& camera);

    static entt::entity PickEntity(entt::registry& registry, const Ray& ray);

    // Slab test against an axis-aligned box. outTNear is the entry parameter
    // along the ray. Exposed for testing because the depth sort depends on it
    // being the true entry distance, not merely "some value inside the box".
    static bool IntersectRayAABB(const glm::vec3& origin,
                                 const glm::vec3& direction,
                                 const glm::vec3& boundsMin,
                                 const glm::vec3& boundsMax,
                                 float& outTNear,
                                 float& outTFar);
};

} // namespace Supersonic
