#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Supersonic {

// Where an element attaches to the screen.
//
// A HUD authored against one window size has to survive every other one, and
// the only way that works is for each element to say which edge or corner it
// belongs to. An offset alone puts the score in the top-left at 1280x720 and
// somewhere in the middle of a 4K screen.
enum class UIAnchor : uint8_t {
    TopLeft = 0,
    TopCenter,
    TopRight,
    MiddleLeft,
    Center,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

// A rectangle in screen pixels: min is the top-left corner.
struct UIRect {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};

    glm::vec2 size() const { return max - min; }
};

// Screen-space layout for HUD elements. No renderer, no ImGui, no Vulkan - the
// arithmetic that decides where things land is the part that is wrong in subtle
// ways at resolutions nobody tested on, so it lives on its own and is tested on
// its own.
namespace UICanvas {

// Height the HUD is authored against. An element 40 pixels tall stays 40
// pixels tall at 1080p, and grows or shrinks proportionally elsewhere, so a
// health bar is the same fraction of the screen on a laptop and a 4K monitor.
inline constexpr float kReferenceHeight = 1080.0f;

// Multiplier taking authored units to pixels on a screen of this size.
float ScaleFor(const glm::vec2& screenSize);

// Places an element of `size` authored units at `offset` from its anchor.
//
// The offset always runs *inward* from the anchored edge, so the same offset of
// (16, 16) means "16 in from the corner" whichever corner is chosen, rather
// than pushing a bottom-right element off the screen.
UIRect Place(UIAnchor anchor, const glm::vec2& offset, const glm::vec2& size,
             const UIRect& screen);

// Same, for something whose size is only known after measuring - a run of text.
// Identical rules; separate name because the caller has to measure first.
inline UIRect PlaceMeasured(UIAnchor anchor, const glm::vec2& offset,
                            const glm::vec2& measuredSize, const UIRect& screen) {
    return Place(anchor, offset, measuredSize, screen);
}

// Left portion of a rect, for a bar that fills from 0 to 1. Clamped, because a
// health value can go negative in the same frame the death handler runs.
UIRect FillHorizontal(const UIRect& rect, float fraction);

} // namespace UICanvas

} // namespace Supersonic
