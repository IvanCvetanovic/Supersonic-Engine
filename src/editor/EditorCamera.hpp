#pragma once

#include <glm/glm.hpp>

#include "core/Components.hpp"
#include "platform/Window.hpp"

namespace Supersonic {

// The viewport's own fly camera, owned by the editor rather than the scene.
//
// Before this there was only the scene's CameraComponent, so flying around in
// the editor permanently moved the game camera - and CameraSystem drove *every*
// camera entity in lockstep, which broke the moment a scene had two. Edit mode
// now flies this camera and leaves the scene untouched; play mode renders
// through the scene's active camera.
class EditorCamera {
public:
    // viewportHovered gates the *start* of a look; viewportFocused gates the
    // fly keys. Neither is ImGui's WantCaptureMouse, which the viewport window
    // sets unconditionally.
    void Update(Window& window, float deltaTime, bool viewportHovered, bool viewportFocused);

    void SetAspect(float aspect) { m_camera.aspect = aspect; }
    const CameraComponent& Get() const { return m_camera; }

    // Frames a point, used by "focus on selection".
    void FocusOn(const glm::vec3& target, float distance = 5.0f);

private:
    CameraComponent m_camera = [] {
        CameraComponent c;
        c.fov = 50.0f;
        c.position = glm::vec3(0.6f, 2.6f, 9.5f);
        c.yaw = -92.0f;
        c.pitch = -12.0f;
        c.movementSpeed = 6.0f;
        c.updateCameraVectors();
        return c;
    }();

    bool m_looking{false};
    bool m_firstMouse{true};
    double m_lastX{0.0};
    double m_lastY{0.0};
};

} // namespace Supersonic
