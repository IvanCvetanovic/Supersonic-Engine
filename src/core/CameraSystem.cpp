#include "core/CameraSystem.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>

namespace Engine {

bool CameraSystem::s_firstMouse = true;
double CameraSystem::s_lastX = 640.0;
double CameraSystem::s_lastY = 360.0;

void CameraSystem::Update(entt::registry& registry, Window& window, float deltaTime,
                          bool allowKeyboard, bool allowMouse) {
    GLFWwindow* nativeWin = window.GetNativeWindow();

    // Raw GLFW polling bypasses ImGui's callbacks entirely, so the caller has
    // to tell us whether the UI currently owns the input. Without this, typing
    // "Rock" into the inspector strafed the camera and re-bound the gizmo.
    if (allowKeyboard) {
        auto view = registry.view<CameraComponent>();
        for (auto entity : view) {
            auto& camera = view.get<CameraComponent>(entity);
            const float velocity = camera.movementSpeed * deltaTime;

            if (glfwGetKey(nativeWin, GLFW_KEY_W) == GLFW_PRESS) camera.position += camera.front * velocity;
            if (glfwGetKey(nativeWin, GLFW_KEY_S) == GLFW_PRESS) camera.position -= camera.front * velocity;
            if (glfwGetKey(nativeWin, GLFW_KEY_A) == GLFW_PRESS) camera.position -= camera.right * velocity;
            if (glfwGetKey(nativeWin, GLFW_KEY_D) == GLFW_PRESS) camera.position += camera.right * velocity;
            if (glfwGetKey(nativeWin, GLFW_KEY_SPACE) == GLFW_PRESS) camera.position += camera.worldUp * velocity;
            if (glfwGetKey(nativeWin, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera.position -= camera.worldUp * velocity;
        }
    }

    ProcessMouseInput(registry, window, allowMouse);
}

void CameraSystem::ProcessMouseInput(entt::registry& registry, Window& window, bool allowMouse) {
    GLFWwindow* nativeWin = window.GetNativeWindow();

    if (!allowMouse || glfwGetMouseButton(nativeWin, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS) {
        // Resetting here is what prevents a jump on the next drag.
        s_firstMouse = true;
        return;
    }

    double xpos = 0.0, ypos = 0.0;
    glfwGetCursorPos(nativeWin, &xpos, &ypos);

    if (s_firstMouse) {
        s_lastX = xpos;
        s_lastY = ypos;
        s_firstMouse = false;
    }

    const float rawX = static_cast<float>(xpos - s_lastX);
    const float rawY = static_cast<float>(s_lastY - ypos); // screen Y grows downward

    s_lastX = xpos;
    s_lastY = ypos;

    auto view = registry.view<CameraComponent>();
    for (auto entity : view) {
        auto& camera = view.get<CameraComponent>(entity);

        // Scale into locals. These used to be the loop-invariant deltas
        // themselves, mutated in place, so the k-th camera received the raw
        // delta multiplied by sensitivity^k.
        const float xoffset = rawX * camera.mouseSensitivity;
        const float yoffset = rawY * camera.mouseSensitivity;

        camera.yaw += xoffset;
        camera.pitch = std::clamp(camera.pitch + yoffset, -89.0f, 89.0f);
        camera.updateCameraVectors();
    }
}

} // namespace Engine
