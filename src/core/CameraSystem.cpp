#include "core/CameraSystem.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>

namespace Engine {

bool CameraSystem::s_firstMouse = true;
double CameraSystem::s_lastX = 640.0;
double CameraSystem::s_lastY = 360.0;

void CameraSystem::Update(entt::registry& registry, Window& window, float deltaTime) {
    GLFWwindow* nativeWin = window.GetNativeWindow();

    auto view = registry.view<CameraComponent>();
    for (auto entity : view) {
        auto& camera = view.get<CameraComponent>(entity);

        float velocity = camera.movementSpeed * deltaTime;

        if (glfwGetKey(nativeWin, GLFW_KEY_W) == GLFW_PRESS) {
            camera.position += camera.front * velocity;
        }
        if (glfwGetKey(nativeWin, GLFW_KEY_S) == GLFW_PRESS) {
            camera.position -= camera.front * velocity;
        }
        if (glfwGetKey(nativeWin, GLFW_KEY_A) == GLFW_PRESS) {
            camera.position -= camera.right * velocity;
        }
        if (glfwGetKey(nativeWin, GLFW_KEY_D) == GLFW_PRESS) {
            camera.position += camera.right * velocity;
        }
        if (glfwGetKey(nativeWin, GLFW_KEY_SPACE) == GLFW_PRESS) {
            camera.position += camera.worldUp * velocity;
        }
        if (glfwGetKey(nativeWin, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            camera.position -= camera.worldUp * velocity;
        }
    }

    ProcessMouseInput(registry, window);
}

void CameraSystem::ProcessMouseInput(entt::registry& registry, Window& window) {
    GLFWwindow* nativeWin = window.GetNativeWindow();

    // Check right mouse button drag for camera orientation
    if (glfwGetMouseButton(nativeWin, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS) {
        s_firstMouse = true;
        return;
    }

    double xpos, ypos;
    glfwGetCursorPos(nativeWin, &xpos, &ypos);

    if (s_firstMouse) {
        s_lastX = xpos;
        s_lastY = ypos;
        s_firstMouse = false;
    }

    float xoffset = static_cast<float>(xpos - s_lastX);
    float yoffset = static_cast<float>(s_lastY - ypos); // Reversed since y-coordinates go from bottom to top

    s_lastX = xpos;
    s_lastY = ypos;

    auto view = registry.view<CameraComponent>();
    for (auto entity : view) {
        auto& camera = view.get<CameraComponent>(entity);

        xoffset *= camera.mouseSensitivity;
        yoffset *= camera.mouseSensitivity;

        camera.yaw += xoffset;
        camera.pitch += yoffset;

        camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);
        camera.updateCameraVectors();
    }
}

} // namespace Engine
