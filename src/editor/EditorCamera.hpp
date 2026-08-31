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
    // viewportHovered gates the *start* of a look, and holding the right button
    // is what enables both looking and the fly keys. Deliberately not ImGui's
    // WantCaptureMouse, which the viewport window sets unconditionally and which
    // therefore suppressed camera input across the entire viewport.
    void Update(Window& window, float deltaTime, bool viewportHovered);

    void SetAspect(float aspect) { m_camera.aspect = aspect; }

    // How tall the viewport is in PIXELS, which orthographic panning needs and
    // an aspect ratio cannot supply.
    //
    // A pan has to move the world by exactly as far as the cursor moved, or the
    // scene slides out from under the pointer and the drag feels like it is
    // fighting you. That conversion is orthoHeight world units over this many
    // pixels, so it needs the height itself and not the ratio of the two sides.
    void SetViewportHeight(float pixels) { m_viewportHeight = pixels > 1.0f ? pixels : 1.0f; }

    // Whether the viewport looks through a box rather than a pyramid.
    //
    // THE EDITOR COULD NOT SHOW AN ORTHOGRAPHIC VIEW AT ALL. CameraComponent has
    // had the projection, the ortho matrix, picking and serialisation since the
    // day a 2D game was contemplated - but nothing in src/editor/ ever wrote
    // `projection`, so the only way to look through one was to load a scene
    // whose camera already said so, and the only way to author that was to edit
    // the file by hand. A 2D scene could be built and could not be seen.
    void SetOrthographic(bool orthographic);
    bool IsOrthographic() const { return m_camera.isOrthographic(); }

    // How far the eye moves for a drag of dx, dy PIXELS.
    //
    // Static and pure because it is a SIGN, and a sign is the one thing here
    // that a screenshot cannot show and a person notices immediately: get it
    // backwards and the world runs away from the pointer instead of following
    // it. The rest of the 2D controls need GLFW and a window, which is exactly
    // the argument this class makes for splitting anything decidable out of
    // them - and having made that argument, the first version of this shipped
    // sixty lines that nothing had ever executed.
    static glm::vec3 PanOffset(float worldPerPixel, float dx, float dy,
                               const glm::vec3& right, const glm::vec3& up);

    // The vertical span after a wheel notch. Multiplicative, so it moves by the
    // same proportion of what you are looking at however far out you are, and
    // clamped at both ends because a multiplicative zoom has zero as a fixed
    // point it can never climb back out of.
    static float ZoomedHeight(float current, float scroll);

    const CameraComponent& Get() const { return m_camera; }

    // Frames a point, used by "focus on selection".
    void FocusOn(const glm::vec3& target, float distance = 5.0f);

private:
    void updateOrthographic(struct GLFWwindow* native, float deltaTime,
                            bool viewportHovered, bool rightHeld);

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

    float m_viewportHeight{720.0f};

    // What the fly camera was framing when the view went flat, so switching
    // back returns to it rather than to wherever the pan left the eye. An
    // orthographic pan moves the eye a long way sideways and the depth it sits
    // at stops meaning anything, so restoring the position is the only way the
    // toggle is reversible.
    glm::vec3 m_perspectivePosition{0.6f, 2.6f, 9.5f};
    float m_perspectiveFov{50.0f};
};

} // namespace Supersonic
