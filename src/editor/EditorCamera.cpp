#include "editor/EditorCamera.hpp"

#include "core/Input.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace Supersonic {

namespace {

// How far the 2D view may be zoomed, in world units of vertical span.
//
// Bounded at both ends rather than left open: the zoom is multiplicative, so
// without a floor it converges on zero and the world-per-pixel conversion below
// it becomes a division that produces nothing, and without a ceiling a few
// seconds of scrolling puts the far plane inside the view.
constexpr float kMinOrthoHeight = 0.05f;
constexpr float kMaxOrthoHeight = 5000.0f;

} // namespace

glm::vec3 EditorCamera::PanOffset(float worldPerPixel, float dx, float dy,
                                  const glm::vec3& right, const glm::vec3& up) {
    // THE EYE MOVES AGAINST THE DRAG, so the world moves with it and whatever
    // is under the pointer stays under it. Dragging the cursor right carries
    // the scene right, which means the camera goes left.
    //
    // And PLUS up for a positive dy, because screen Y grows DOWN: dragging
    // down is a negative move in view space, so the eye rising is what carries
    // the scene down with the hand.
    return -right * (dx * worldPerPixel) + up * (dy * worldPerPixel);
}

float EditorCamera::ZoomedHeight(float current, float scroll) {
    if (scroll == 0.0f) return std::clamp(current, kMinOrthoHeight, kMaxOrthoHeight);

    // Under one, so scrolling UP - a positive notch, the direction every tool
    // means by "zoom in" - SHRINKS the span and the world gets bigger.
    return std::clamp(current * std::pow(0.88f, scroll), kMinOrthoHeight, kMaxOrthoHeight);
}

void EditorCamera::SetOrthographic(bool orthographic) {
    if (orthographic == m_camera.isOrthographic()) return;

    if (orthographic) {
        m_perspectivePosition = m_camera.position;
        m_perspectiveFov = m_camera.fov;
        m_camera.projection = CameraComponent::Projection::Orthographic;

        // Framed to show roughly what the perspective view was showing at the
        // distance the eye was standing off the origin, so the toggle does not
        // throw away where you were looking. Half the vertical span of a
        // frustum at that distance IS tan(fov/2) * distance, which is the same
        // arithmetic ClusterGrid::ViewVolume is built out of.
        const float distance = std::max(glm::length(m_camera.position), 1.0f);
        m_camera.orthoHeight =
            std::clamp(2.0f * std::tan(glm::radians(m_camera.fov) * 0.5f) * distance,
                       kMinOrthoHeight, kMaxOrthoHeight);
    } else {
        m_camera.projection = CameraComponent::Projection::Perspective;
        m_camera.position = m_perspectivePosition;
        m_camera.fov = m_perspectiveFov;
    }
    m_camera.updateCameraVectors();
}

void EditorCamera::Update(Window& window, float deltaTime, bool viewportHovered) {
#if !SUPERSONIC_WINDOW_GLFW
    // The fly camera reads the mouse and keys through GLFW. A borrowed window
    // (WindowBackend.hpp) only ever runs a game, which is never in edit mode
    // and so never flies it.
    (void)window;
    (void)deltaTime;
    (void)viewportHovered;
#else
    GLFWwindow* native = window.GetNativeWindow();

    const bool rightHeld = glfwGetMouseButton(native, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    if (m_camera.isOrthographic()) {
        updateOrthographic(native, deltaTime, viewportHovered, rightHeld);
        return;
    }

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

    // Fly keys require the right button held - the WASD-with-right-mouse idiom.
    //
    // Not merely "the viewport has focus": E and Q would then collide with the
    // E and R gizmo-mode hotkeys, so flying up silently turned the translate
    // gizmo into a rotate ring, and holding W to fly forward re-forced translate
    // every frame. Requiring the button resolves it outright, because the gizmo
    // hotkeys are already suppressed while the right button is down.
    if (m_looking) {
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
#endif
}

// The 2D controls, which are not the 3D ones with the projection swapped.
//
// PANNING RATHER THAN FLYING, and the reason is not taste. Under an
// orthographic projection, moving along the view direction changes nothing you
// can see - the cross-section is the same rectangle at every depth, so W and S
// would be two keys that appear broken while quietly walking the eye towards
// the near plane until geometry starts vanishing into it. So the four keys pan
// in the view plane, where they do something.
//
// LOOKING IS OFF for the same kind of reason: a 2D view is authored square to
// its plane, and a right-drag that rolled it a few degrees would put every
// subsequent placement subtly out of true with no obvious way back. The drag is
// spent on the pan instead, which is what the button is for in every 2D editor.
//
// Zoom is the scroll wheel, multiplicative. Adding a constant would crawl when
// zoomed out and jump when zoomed in; a ratio moves by the same proportion of
// what you are looking at either way.
void EditorCamera::updateOrthographic(GLFWwindow* native, float deltaTime,
                                      bool viewportHovered, bool rightHeld) {
#if !SUPERSONIC_WINDOW_GLFW
    // Update() never calls this without GLFW; see there.
    (void)native;
    (void)deltaTime;
    (void)viewportHovered;
    (void)rightHeld;
#else
    if (viewportHovered) {
        m_camera.orthoHeight = ZoomedHeight(m_camera.orthoHeight, Input::Scroll());
    }

    // World units per pixel of cursor travel. This is what makes a drag stick
    // to the thing under the pointer.
    const float worldPerPixel = m_camera.orthoHeight / m_viewportHeight;

    // Latched exactly as the fly camera's look is, and for the same reason: a
    // pan starts over the viewport and then leaves it, because a pan is a long
    // sweep.
    if (!m_looking) {
        if (rightHeld && viewportHovered) {
            m_looking = true;
            m_firstMouse = true;
        }
    } else if (!rightHeld) {
        m_looking = false;
    }

    if (m_looking) {
        double x = 0.0, y = 0.0;
        glfwGetCursorPos(native, &x, &y);
        if (m_firstMouse) {
            m_lastX = x;
            m_lastY = y;
            m_firstMouse = false;
        }

        const float dx = static_cast<float>(x - m_lastX);
        const float dy = static_cast<float>(y - m_lastY);   // screen Y grows down
        m_lastX = x;
        m_lastY = y;

        m_camera.position += PanOffset(worldPerPixel, dx, dy, m_camera.right, m_camera.up);
    }

    // Keys pan too, and unlike the fly camera they do not need a button held:
    // there is no gizmo hotkey to collide with here, because E and R rotate and
    // scale in a plane the 2D author is still allowed to use.
    //
    // Speed scales with the zoom, so one key press crosses the same fraction of
    // the screen however far out you are - the same argument the multiplicative
    // zoom rests on.
    if (viewportHovered) {
        float speed = m_camera.orthoHeight * 0.6f * deltaTime;
        if (glfwGetKey(native, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) speed *= 3.0f;

        if (glfwGetKey(native, GLFW_KEY_W) == GLFW_PRESS) m_camera.position += m_camera.up * speed;
        if (glfwGetKey(native, GLFW_KEY_S) == GLFW_PRESS) m_camera.position -= m_camera.up * speed;
        if (glfwGetKey(native, GLFW_KEY_A) == GLFW_PRESS) m_camera.position -= m_camera.right * speed;
        if (glfwGetKey(native, GLFW_KEY_D) == GLFW_PRESS) m_camera.position += m_camera.right * speed;
    }
#endif
}

void EditorCamera::FocusOn(const glm::vec3& target, float distance) {
    m_camera.position = target - m_camera.front * distance;
    m_camera.updateCameraVectors();
}

} // namespace Supersonic
