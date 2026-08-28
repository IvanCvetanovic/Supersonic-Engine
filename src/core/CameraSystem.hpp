#pragma once

#include <entt/entt.hpp>
#include "core/Components.hpp"

namespace Supersonic {

class CameraSystem {
public:
    // Flies the scene's primary camera, if that camera says it may be flown.
    //
    // allowKeyboard/allowMouse say whether something else owns the device this
    // frame - an editor panel, a focused text box. STILL REQUIRED, even though
    // nothing here touches GLFW any more: Input's raw key queries are
    // deliberately ungated, so the text veto that stops A and D being letters
    // covers actions and axes and not IsKeyDown. A caller that stopped passing
    // them would put back the bug where typing a name into a HUD field flies
    // the camera forward.
    //
    // No Window. This used to poll glfwGetKey directly, which meant the system
    // could not be called at all without a real window - so the one system that
    // reaches into a shipped game was the one with no tests. It reads Input's
    // snapshot now, which the polling layer filled from the same glfwGetKey
    // earlier in the same frame, so the behaviour is unchanged and a test can
    // hand it a struct.
    static void Update(entt::registry& registry, float deltaTime,
                       bool allowKeyboard = true, bool allowMouse = true);

private:
    // Both halves go through Update, so the fly-controls gate has exactly one
    // place to be. This was public and looked up the primary camera itself,
    // which would have left the mouse half ungated for any direct caller - and
    // it was the only entry a test could reach, so the gate could have been
    // green and the shipped path still open.
    static void ProcessMouseInput(CameraComponent& camera, bool allowMouse);

    // Whether a right-drag look is in progress. Meaningless while the pointer is
    // locked, where looking is not something you hold a button for - which is
    // why the two baselines that used to live beside this are gone.
    static bool s_looking;
};

} // namespace Supersonic
