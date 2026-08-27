#pragma once

#include <entt/entt.hpp>

#include "core/UICanvas.hpp"

struct ImFont;

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

// Where every stacked element ended up, without drawing anything.
//
// Render computes this and uses it for both the input pass and the draw pass;
// it is public because two other callers have a real claim on the answer. A
// game may want to know where its own HUD landed - to put a tooltip beside a
// button, or to place a world-space marker against a panel edge - and a test
// wants the arithmetic without an ImGui context.
//
// STACKS NEST. A stack that is a child of another stack is measured by its own
// contents and placed inside its parent's slot; only a stack whose parent is
// not a stack is placed against `gameRect`. Depth is capped, so a hierarchy
// with a cycle in it stops rather than hanging the frame.
//
// `font` may be null when nothing in the tree carries text - a dock of panels
// and buttons never measures a glyph. It is dereferenced only for
// UITextComponent, and a null font with text in the tree is a crash rather
// than a guess, because a UI measured with no font is not a UI.
//
// `scale` sizes and `gameRect` places, and they stay separate all the way
// down: an authored unit is the same size at every depth.
UICanvas::StackedRects LayoutStacks(entt::registry& registry, const UIRect& gameRect,
                                    ImFont* font, float scale);

} // namespace UISystem

} // namespace Supersonic
