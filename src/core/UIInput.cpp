#include "core/UIInput.hpp"

#include "core/Components.hpp"

namespace Supersonic {

namespace UIInput {

int Update(entt::registry& registry, const UIRect& gameRect,
           const UICanvas::UIPointer& pointer) {
    const glm::vec2 screenSize = gameRect.size();
    if (screenSize.x < 1.0f || screenSize.y < 1.0f) return 0;

    const float scale = UICanvas::ScaleFor(screenSize);
    int clicked = 0;

    for (auto [entity, button] : registry.view<UIButtonComponent>().each()) {
        // An invisible button is not a click target. Hiding a menu is how a
        // game closes it, and a hidden menu that still swallowed clicks would
        // block the game underneath it.
        if (!button.visible) {
            button.hovered = false;
            button.pressed = false;
            button.clicked = false;
            continue;
        }

        const UIRect rect = UICanvas::Place(button.anchor, button.offset * scale,
                                            button.size * scale, gameRect);

        // A disabled button is drawn but not live, so an unavailable menu item
        // stays where it is instead of moving everything below it.
        UICanvas::UIPointer effective = pointer;
        effective.active = pointer.active && button.enabled;

        const UICanvas::UIButtonState previous{ button.hovered, button.pressed, button.clicked };
        const UICanvas::UIButtonState state =
            UICanvas::UpdateButton(previous, rect, effective);

        button.hovered = state.hovered;
        button.pressed = state.pressed;
        button.clicked = state.clicked;

        if (state.clicked) ++clicked;
    }

    return clicked;
}

} // namespace UIInput

} // namespace Supersonic
