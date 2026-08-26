#pragma once

#include <entt/entt.hpp>

#include "core/UICanvas.hpp"

namespace Supersonic {

// Draws the scene's UI components over the game.
//
// The drawing itself goes through ImGui's draw list rather than a bespoke
// pipeline, for one reason: ImGui already owns a rasterised font atlas, and it
// is the only text in the binary. Writing a second glyph rasteriser to draw the
// same fonts into the same window would be a lot of code to arrive back where
// this starts. The game-facing surface is the components and UICanvas, so the
// renderer underneath can be replaced without a scene noticing.
//
// Called with the rectangle the game occupies on screen - the whole window for
// a packaged game, the viewport panel inside the editor - so a HUD authored in
// the editor sits where it will sit when the game ships.
namespace UISystem {

// Updates every interactive element against the pointer, then draws
// everything. One call rather than an update pass and a draw pass, because the
// two must agree on the rectangle each element occupies - and the surest way
// to have a click target that does not match what is on screen is to compute
// it twice.
// `viewProj` is the camera the viewport is SHOWING, so a world-space label
// lands on the object it names. It must be the same camera the scene was drawn
// with; passing a different one puts every name plate somewhere plausible and
// wrong, which is the hardest kind of wrong to notice.
void Render(entt::registry& registry, const UIRect& gameRect,
            const UICanvas::UIPointer& pointer,
            const UICanvas::UIKeyboard& keyboard,
            const glm::mat4& viewProj);

} // namespace UISystem

} // namespace Supersonic
