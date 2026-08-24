#include "core/CameraSystem.hpp"
#include "core/EcsUtils.hpp"
#include "core/Input.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>

namespace Supersonic {

bool CameraSystem::s_looking = false;

void CameraSystem::Update(entt::registry& registry, Window& window, float deltaTime,
                          bool allowKeyboard, bool allowMouse) {
    GLFWwindow* nativeWin = window.GetNativeWindow();

    // Raw GLFW polling bypasses ImGui's callbacks entirely, so the caller has
    // to tell us whether the UI currently owns the input. Without this, typing
    // "Rock" into the inspector strafed the camera and re-bound the gizmo.
    // Only the primary camera. Driving every CameraComponent in lockstep meant
    // a second camera in the scene moved along with the one you were flying.
    const entt::entity primary = FindPrimaryCamera(registry);
    if (primary == entt::null) return;

    if (allowKeyboard) {
        {
            auto& camera = registry.get<CameraComponent>(primary);
            const float velocity = camera.movementSpeed * deltaTime;

            if (glfwGetKey(nativeWin, GLFW_KEY_W) == GLFW_PRESS) camera.position += camera.front * velocity;
            if (glfwGetKey(nativeWin, GLFW_KEY_S) == GLFW_PRESS) camera.position -= camera.front * velocity;
            if (glfwGetKey(nativeWin, GLFW_KEY_A) == GLFW_PRESS) camera.position -= camera.right * velocity;
            if (glfwGetKey(nativeWin, GLFW_KEY_D) == GLFW_PRESS) camera.position += camera.right * velocity;
            if (glfwGetKey(nativeWin, GLFW_KEY_SPACE) == GLFW_PRESS) camera.position += camera.worldUp * velocity;
            if (glfwGetKey(nativeWin, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera.position -= camera.worldUp * velocity;
        }
    }

    ProcessMouseInput(registry, allowMouse);
}

void CameraSystem::ProcessMouseInput(entt::registry& registry, bool allowMouse) {
    // A locked pointer IS the look.
    //
    // Everything below this branch exists because the cursor is a shared,
    // visible thing that has to be borrowed: hence a button to hold, a latch so
    // the borrow survives the pointer leaving the panel, and a gate so it can
    // only start over the viewport. A captured pointer is none of those - it is
    // invisible, it cannot leave, and there is nothing else on screen competing
    // for it. Keeping the button would mean a first-person game you steer by
    // holding right-click, which is the thing this was supposed to fix.
    if (Input::EffectiveCursorMode() == CursorMode::Locked) {
        // The latch means nothing here, and leaving it set would carry a look
        // into the frame after the pointer is released.
        s_looking = false;
    } else {
        // Latched: a look starts only when the pointer is over the viewport, but
        // continues once started, because a turn is a long sweep that leaves the
        // panel almost immediately.
        const bool rightHeld = Input::IsMouseButtonDown(MouseButton::Right);
        if (!s_looking) {
            if (!rightHeld || !allowMouse) return;
            s_looking = true;
        } else if (!rightHeld) {
            s_looking = false;
            return;
        }
    }

    // One delta for the frame, from the one place that reads the device.
    //
    // This used to keep its own last-position baseline, which is the same
    // arithmetic until the pointer teleports - and locking or releasing it
    // teleports it, by hundreds of pixels. A second baseline would have had to
    // learn about cursor modes to survive that; reading the shared delta means
    // it never has to.
    const glm::vec2 delta = Input::MouseDelta();
    const float rawX = delta.x;
    const float rawY = -delta.y;   // screen Y grows downward

    const entt::entity primary = FindPrimaryCamera(registry);
    if (primary == entt::null) return;
    {
        auto& camera = registry.get<CameraComponent>(primary);

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

} // namespace Supersonic
