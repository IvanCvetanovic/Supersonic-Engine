#include "editor/EditorCamera.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>

namespace Supersonic {

void EditorCamera::Update(Window& window, float deltaTime, bool viewportHovered, bool viewportFocused) {
    GLFWwindow* native = window.GetNativeWindow();

    const bool rightHeld = glfwGetMouseButton(native, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    // Latched, not sampled per frame: a look starts only over the viewport, but
    // once it has started the drag continues even as the cursor travels off the
    // panel - which it always does, because turning is a long sweep.
    if (!m_looking) {
        if (rightHeld && viewportHovered) {
            m_looking = true;
            m_firstMouse = true;
        }
    } else if (!rightHeld) {
        m_looking = false;
    }

    // Fly keys work while the viewport has focus, and unconditionally while
    // looking, which is the WASD-with-right-mouse-held idiom every editor uses.
    if (m_looking || viewportFocused) {
        float speed = m_camera.movementSpeed * deltaTime;
        // Shift is the usual "move faster" modifier in an editor viewport.
        if (glfwGetKey(native, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) speed *= 3.0f;

        if (glfwGetKey(native, GLFW_KEY_W) == GLFW_PRESS) m_camera.position += m_camera.front * speed;
        if (glfwGetKey(native, GLFW_KEY_S) == GLFW_PRESS) m_camera.position -= m_camera.front * speed;
        if (glfwGetKey(native, GLFW_KEY_A) == GLFW_PRESS) m_camera.position -= m_camera.right * speed;
        if (glfwGetKey(native, GLFW_KEY_D) == GLFW_PRESS) m_camera.position += m_camera.right * speed;
        if (glfwGetKey(native, GLFW_KEY_E) == GLFW_PRESS) m_camera.position += m_camera.worldUp * speed;
        if (glfwGetKey(native, GLFW_KEY_Q) == GLFW_PRESS) m_camera.position -= m_camera.worldUp * speed;
    }

    if (!m_looking) return;

    double x = 0.0, y = 0.0;
    glfwGetCursorPos(native, &x, &y);

    if (m_firstMouse) {
        m_lastX = x;
        m_lastY = y;
        m_firstMouse = false;
    }

    const float dx = static_cast<float>(x - m_lastX) * m_camera.mouseSensitivity;
    const float dy = static_cast<float>(m_lastY - y) * m_camera.mouseSensitivity; // screen Y grows down
    m_lastX = x;
    m_lastY = y;

    m_camera.yaw += dx;
    m_camera.pitch = std::clamp(m_camera.pitch + dy, -89.0f, 89.0f);
    m_camera.updateCameraVectors();
}

void EditorCamera::FocusOn(const glm::vec3& target, float distance) {
    m_camera.position = target - m_camera.front * distance;
    m_camera.updateCameraVectors();
}

} // namespace Supersonic
