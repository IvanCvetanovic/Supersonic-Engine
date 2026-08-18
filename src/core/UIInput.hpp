#pragma once

#include <entt/entt.hpp>

#include "core/UICanvas.hpp"

namespace Supersonic {

// Runs the pointer over the scene's interactive UI.
//
// Separate from UISystem, which draws, for one reason: drawing needs ImGui and
// therefore a window, a device and a frame in flight, and none of that can be
// linked into a test. Whether a click lands on the right button is not a
// question about a renderer, and it is the question most worth answering
// without one.
//
// UISystem::Render calls this before it draws, so the two always agree about
// where an element is: both place it with UICanvas::Place from the same
// component, rather than each working it out its own way.
namespace UIInput {

// Updates the hovered/pressed/clicked flags on every UIButtonComponent.
// Returns the number of buttons that were clicked this frame, which is what
// lets a caller cheaply ask "did the UI take this click?" before letting it
// through to the world underneath.
int Update(entt::registry& registry, const UIRect& gameRect,
           const UICanvas::UIPointer& pointer);

} // namespace UIInput

} // namespace Supersonic
