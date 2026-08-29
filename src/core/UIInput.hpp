#pragma once

#include <cstdint>
#include <vector>

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
// This used to return the number of buttons clicked, as a guard a game could
// ask before letting a click through to the world underneath. Nothing ever
// called it, and NO CORRECT GUARD COULD HAVE BEEN BUILT FROM IT - which is why
// it is gone rather than wired up.
//
// It was wrong about the edge. A click lands on the RELEASE, and everything
// that needs guarding fires on the PRESS, so on the frame a guard would read
// the count it is zero for every button. Gating on it would have been a no-op
// that looked like a fix.
//
// It was wrong about the set. Only buttons were counted, and a full-screen
// backdrop panel - the thing this file supports on purpose so a menu swallows
// clicks meant for the game behind it - is not a button. The count read zero in
// exactly the case the guard existed for.
//
// The question a guard has to ask is who owned the pointer when it went DOWN.
// `topmostUnderPointer` below already computes that, over panels and fields as
// well as buttons, and throws it away. Whoever builds the guard should start
// there; and anything the SIMULATION reads has to come off clickedThisTick
// instead, because that is what a replay assigns and no frame count is recorded.
//
// The keyboard is NOT defaulted, and that is on purpose. An inert default is
// exactly the argument a caller forgets, and the symptom would be a field that
// draws, focuses, blinks its caret and silently cannot be typed into - which is
// the whole feature, missing, with nothing to point at. Seventeen call sites
// the compiler names are cheaper than one that fails quietly.
// `stacked` is where the layout pass put anything inside a container. An
// element in it is hit-tested against THAT rectangle rather than against the
// anchor it is no longer using; anything absent places itself as before.
void Update(entt::registry& registry, const UIRect& gameRect,
            const UICanvas::UIPointer& pointer,
            const UICanvas::UIKeyboard& keyboard,
            const UICanvas::StackedLayout& stacked);

// Hand this tick the clicks nothing has consumed yet.
//
// Update above runs once per FRAME and a script runs on the TICK, so a click
// left at frame scope is wrong in both directions: a frame that runs no tick
// loses it - six frames out of seven for a 20 Hz game on a 144 Hz display - and
// a frame that runs three reports it to all three, so one press of a button
// does its action three times.
//
// This is the same latch the keyboard already has in Input::BeginTickInput, and
// it lives here rather than beside it because a click belongs to an entity and
// Input deliberately knows nothing about the registry. Call once at the top of
// every tick, before anything reads a click.
void BeginTickClicks(entt::registry& registry);

// Throw away clicks no tick is coming for: the editor, a pause menu that stops
// the simulation, a time-travel scrub. Without it they queue and arrive
// together on the first tick after Play, so a button pressed while authoring
// fires when the game starts. Call once on every frame that runs no tick.
void DiscardPendingClicks(entt::registry& registry);

// --- Recording and replaying a tick's clicks ------------------------------
//
// The clicks half of Input::TickInput, which lives here rather than there
// because a click belongs to an entity and Input does not know what one is.
// Ids travel as plain uint32_t for the same reason key codes are plain ints in
// that file: the value passes through, and no header describing it has to.

// The ids BeginTickClicks just handed this tick, for a recording to keep.
// Call after BeginTickClicks and before anything reads a click.
std::vector<uint32_t> ClicksThisTick(const entt::registry& registry);

// Install a recorded tick's clicks in place of the live ones.
//
// The replacement for BeginTickClicks, not a supplement to it: it assigns every
// button's clickedThisTick, so an id absent from the recording reads as not
// clicked rather than as whatever the live pointer happened to be doing. A
// replay that let a real click through would be a replay of something else.
//
// An id naming an entity that no longer exists, or one that is not a button, is
// ignored. That is a scene the recording does not match, which checkpoint
// hashing is what actually reports - failing here would only say it later and
// less clearly.
void BeginReplayedTickClicks(entt::registry& registry, const std::vector<uint32_t>& clicked);

// Whether any field currently holds focus.
//
// A query rather than something this file acts on: the keyboard veto belongs to
// whoever owns the frame, so UIInput stays free of global state as well as free
// of a window, and its tests need no reset.
bool AnyTextFieldFocused(const entt::registry& registry);

} // namespace UIInput

} // namespace Supersonic
