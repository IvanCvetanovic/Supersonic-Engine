#pragma once

#include <entt/entt.hpp>
#include "platform/Window.hpp"
#include "core/Components.hpp"

namespace Engine {

class CameraSystem {
public:
    static void Update(entt::registry& registry, Window& window, float deltaTime);
    static void ProcessMouseInput(entt::registry& registry, Window& window);

private:
    static bool s_firstMouse;
    static double s_lastX;
    static double s_lastY;
};

} // namespace Engine
