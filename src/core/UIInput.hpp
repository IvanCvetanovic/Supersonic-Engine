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

// Updates the hovered/pressed/clicked flags on every UIButtonComponent, and
// runs focus and typing over every UITextFieldComponent.
//
// Returns the number of buttons that were clicked this frame, which is what
// lets a caller cheaply ask "did the UI take this click?" before letting it
// through to the world underneath.
//
// The keyboard is NOT defaulted, and that is on purpose. An inert default is
// exactly the argument a caller forgets, and the symptom would be a field that
// draws, focuses, blinks its caret and silently cannot be typed into - which is
// the whole feature, missing, with nothing to point at. Seventeen call sites
// the compiler names are cheaper than one that fails quietly.
int Update(entt::registry& registry, const UIRect& gameRect,
           const UICanvas::UIPointer& pointer,
           const UICanvas::UIKeyboard& keyboard);

// Whether any field currently holds focus.
//
// A query rather than something this file acts on: the keyboard veto belongs to
// whoever owns the frame, so UIInput stays free of global state as well as free
// of a window, and its tests need no reset.
bool AnyTextFieldFocused(const entt::registry& registry);

} // namespace UIInput

} // namespace Supersonic
