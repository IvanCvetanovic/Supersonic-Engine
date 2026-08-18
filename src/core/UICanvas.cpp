#include "core/UICanvas.hpp"

#include <algorithm>

namespace Supersonic {

namespace UICanvas {

namespace {

// Horizontal and vertical position of an anchor within its rect, 0 = start,
// 0.5 = centre, 1 = end. Derived from the enum's order rather than switched on,
// so adding a row or column cannot leave one case behind.
glm::vec2 anchorFraction(UIAnchor anchor) {
    const auto index = static_cast<uint8_t>(anchor);
    const float column = static_cast<float>(index % 3) * 0.5f;
    const float row = static_cast<float>(index / 3) * 0.5f;
    return { column, row };
}

} // namespace

float ScaleFor(const glm::vec2& screenSize) {
    if (screenSize.y <= 0.0f) return 1.0f;
    return screenSize.y / kReferenceHeight;
}

UIRect Place(UIAnchor anchor, const glm::vec2& offset, const glm::vec2& size,
             const UIRect& screen) {
    const glm::vec2 screenSize = screen.size();
    const glm::vec2 fraction = anchorFraction(anchor);

    // The anchor point on the screen, then back off by the same fraction of the
    // element's own size. A centred element is centred on its own middle; a
    // right-anchored one has its right edge on the right.
    const glm::vec2 anchorPoint = screen.min + screenSize * fraction;
    glm::vec2 topLeft = anchorPoint - size * fraction;

    // Inward, so the same offset means the same thing at every anchor. Applied
    // per axis, because an anchor can be centred on one axis and not the other:
    // TopCenter takes the vertical offset downward and ignores the horizontal
    // direction entirely, since there is no edge to come in from.
    topLeft.x += (fraction.x == 0.0f) ?  offset.x
               : (fraction.x == 1.0f) ? -offset.x
                                      :  offset.x;
    topLeft.y += (fraction.y == 0.0f) ?  offset.y
               : (fraction.y == 1.0f) ? -offset.y
                                      :  offset.y;

    return { topLeft, topLeft + size };
}

bool Contains(const UIRect& rect, const glm::vec2& point) {
    return point.x >= rect.min.x && point.x <= rect.max.x &&
           point.y >= rect.min.y && point.y <= rect.max.y;
}

UIRect FillHorizontal(const UIRect& rect, float fraction) {
    const float clamped = std::clamp(fraction, 0.0f, 1.0f);
    return { rect.min, glm::vec2(rect.min.x + rect.size().x * clamped, rect.max.y) };
}


UIButtonState UpdateButton(const UIButtonState& previous, const UIRect& rect,
                           const UIPointer& pointer) {
    UIButtonState state;

    // Something else owns the pointer. Everything resets, including a press
    // that was in progress: a button left armed while a dialog is open would
    // fire the moment the dialog closed.
    if (!pointer.active) return state;

    state.hovered = Contains(rect, pointer.position);

    const bool pressEdge = pointer.down && !pointer.wasDown;

    if (previous.pressed && pointer.down) {
        // Still held. Deliberately not re-testing the rectangle: sliding off a
        // button and back on is how every real toolkit behaves, and a player
        // adjusting their aim mid-press should not lose the press.
        state.pressed = true;
    } else if (pressEdge && state.hovered) {
        state.pressed = true;
    }

    // The release has to happen over the same button the press started on.
    // Testing only "the pointer is up and over the button" would fire for a
    // press that began somewhere else entirely - which is how a player who
    // drags across a menu ends up activating whatever they let go over.
    state.clicked = previous.pressed && !pointer.down && state.hovered;

    return state;
}

} // namespace UICanvas

} // namespace Supersonic
