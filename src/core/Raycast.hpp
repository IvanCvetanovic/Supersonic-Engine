#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include "core/Components.hpp"

namespace Engine {

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

class Raycast {
public:
    static Ray ScreenPointToRay(
        const glm::vec2& mousePos,
        const glm::vec2& viewportSize,
        const CameraComponent& camera
    );

    static entt::entity PickEntity(
        entt::registry& registry,
        const Ray& ray
    );
};

} // namespace Engine
