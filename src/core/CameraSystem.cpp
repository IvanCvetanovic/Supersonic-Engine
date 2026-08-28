#include "core/CameraSystem.hpp"
#include "core/EcsUtils.hpp"
#include "core/Input.hpp"
#include <algorithm>

namespace Supersonic {

bool CameraSystem::s_looking = false;

void CameraSystem::Update(entt::registry& registry, float deltaTime,
                          bool allowKeyboard, bool allowMouse) {
    // Only the primary camera. Driving every CameraComponent in lockstep meant
    // a second camera in the scene moved along with the one you were flying.
    const entt::entity primary = FindPrimaryCamera(registry);
    if (primary == entt::null) return;

    auto& camera = registry.get<CameraComponent>(primary);

    // THE GATE, ONCE, BEFORE EITHER HALF. A packaged game goes straight into
    // Play and this system runs in Play, so until there was a switch here a
    // shipped game had an editor fly camera bolted to the keys its own player
    // is bound to - W walked the character forward and flew the view through
    // the wall behind it at the same time.
    //
    // Covering the mouse as well as the keyboard, from the same read of the
    // component, because a game that has turned the flycam off and can still
    // have its view spun by a right-drag has not turned it off.
    if (!camera.flyControlsEnabled) {
        // A look already in progress is abandoned rather than left latched, for
        // the reason spelled out below: a latch that survives the thing that
        // set it carries a turn into a frame nobody asked for.
        s_looking = false;
        return;
    }

    // The caller's veto is still required and is not redundant. Input's raw key
    // queries are deliberately ungated - only actions and axes are silenced
    // while a name is being typed - so without this, typing into a HUD field
    // would fly the camera forward in exactly the shipped game this was built
    // for.
    if (allowKeyboard) {
        const float velocity = camera.movementSpeed * deltaTime;

        if (Input::IsKeyDown(Key::W)) camera.position += camera.front * velocity;
        if (Input::IsKeyDown(Key::S)) camera.position -= camera.front * velocity;
        if (Input::IsKeyDown(Key::A)) camera.position -= camera.right * velocity;
        if (Input::IsKeyDown(Key::D)) camera.position += camera.right * velocity;
        if (Input::IsKeyDown(Key::Space)) camera.position += camera.worldUp * velocity;
        if (Input::IsKeyDown(Key::LeftShift)) camera.position -= camera.worldUp * velocity;
    }

    ProcessMouseInput(camera, allowMouse);
}

void CameraSystem::ProcessMouseInput(CameraComponent& camera, bool allowMouse) {
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

    // Scale into locals. These used to be the loop-invariant deltas themselves,
    // mutated in place, so the k-th camera received the raw delta multiplied by
    // sensitivity^k.
    const float xoffset = rawX * camera.mouseSensitivity;
    const float yoffset = rawY * camera.mouseSensitivity;

    camera.yaw += xoffset;
    camera.pitch = std::clamp(camera.pitch + yoffset, -89.0f, 89.0f);
    camera.updateCameraVectors();
}

} // namespace Supersonic
