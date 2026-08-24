#pragma once

#include <entt/entt.hpp>
#include "platform/Window.hpp"
#include "core/Components.hpp"

namespace Supersonic {

class CameraSystem {
public:
    // allowKeyboard/allowMouse say whether something else owns the device this
    // frame - an editor panel, a focused text box. The keyboard here is still
    // polled straight from GLFW, which ImGui's callbacks cannot suppress, so it
    // has to be told.
    //
    // The MOUSE no longer is: it comes from Input, which means this inherits the
    // one thing that is genuinely hard about a captured pointer - the delta
    // rebase on the frame the mode changes - instead of keeping a second
    // baseline that would snap the view every time.
    static void Update(entt::registry& registry, Window& window, float deltaTime,
                       bool allowKeyboard = true, bool allowMouse = true);
    static void ProcessMouseInput(entt::registry& registry, bool allowMouse = true);

private:
    // Whether a right-drag look is in progress. Meaningless while the pointer is
    // locked, where looking is not something you hold a button for - which is
    // why the two baselines that used to live beside this are gone.
    static bool s_looking;
};

} // namespace Supersonic
