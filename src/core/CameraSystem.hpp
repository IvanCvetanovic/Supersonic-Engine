#pragma once

#include <entt/entt.hpp>
#include "platform/Window.hpp"
#include "core/Components.hpp"

namespace Engine {

class CameraSystem {
public:
    // allowKeyboard/allowMouse come from ImGui's WantCapture flags. Camera input
    // is polled straight from GLFW, which ImGui's callbacks cannot suppress.
    static void Update(entt::registry& registry, Window& window, float deltaTime,
                       bool allowKeyboard = true, bool allowMouse = true);
    static void ProcessMouseInput(entt::registry& registry, Window& window, bool allowMouse = true);

private:
    static bool s_firstMouse;
    static double s_lastX;
    static double s_lastY;
};

} // namespace Engine
