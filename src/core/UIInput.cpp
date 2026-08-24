#include "core/UIInput.hpp"

#include "core/Components.hpp"

namespace Supersonic {

namespace UIInput {

bool AnyTextFieldFocused(const entt::registry& registry) {
    for (auto [entity, field] : registry.view<const UITextFieldComponent>().each()) {
        if (field.focused) return true;
    }
    return false;
}

namespace {

// Focus, which is the first decision in this file that is about the SCENE
// rather than about one element.
//
// A button can be resolved entity by entity because the answer only depends on
// where the pointer is. Focus cannot: exactly one field may hold it, so a flat
// per-entity loop would hand the keystroke to every field under the cursor and
// to none of them correctly. One pass decides, a second applies.
void updateTextFields(entt::registry& registry, const UIRect& gameRect, float scale,
                      const UICanvas::UIPointer& pointer,
                      const UICanvas::UIKeyboard& keyboard) {
    auto view = registry.view<UITextFieldComponent>();

    // A press EDGE decides focus outright - on a field it takes it, anywhere
    // else it gives it up. Absent a press, focus is KEPT: moving the pointer
    // off a field, or off the viewport entirely, must not lose half a name.
    const bool pressEdge = pointer.active && pointer.down && !pointer.wasDown;

    entt::entity pressedField = entt::null;
    entt::entity focusedField = entt::null;

    for (auto entity : view) {
        auto& field = view.get<UITextFieldComponent>(entity);

        const bool live = field.visible && field.enabled;
        if (!live) {
            // A field hidden or disabled while focused gives it up. A menu
            // closes by hiding, and a hidden field that kept the keyboard would
            // leave the game unable to move with nothing on screen to explain
            // why.
            field.hovered = false;
            field.focused = false;
            continue;
        }

        const UIRect rect = UICanvas::Place(field.anchor, field.offset * scale,
                                            field.size * scale, gameRect);
        field.hovered = pointer.active && UICanvas::Contains(rect, pointer.position);

        if (field.focused) focusedField = entity;
        if (pressEdge && field.hovered) pressedField = entity;
    }

    if (pressEdge) focusedField = pressedField;

    for (auto entity : view) {
        auto& field = view.get<UITextFieldComponent>(entity);

        // Cleared unconditionally, like a button's `clicked`: a submit that
        // stayed true would be answered again every frame until the next
        // keystroke.
        field.submitted = false;

        const bool focused = entity == focusedField;
        if (field.focused != focused && focused) {
            // Taking focus puts the caret at the end, which is where someone
            // clicking into a box with a name already in it expects to carry on
            // from.
            field.caret = static_cast<int>(field.text.size());
        }
        field.focused = focused;
        if (!focused) continue;

        UICanvas::UITextEditState previous{};
        previous.caret = field.caret;

        const UICanvas::UITextEditState state =
            UICanvas::EditText(previous, field.text, field.maxLength, keyboard);

        field.caret = state.caret;
        field.submitted = state.submitted;

        // Escape abandons the field rather than the name: the text stands, and
        // the keyboard goes back to the game. Clearing it instead would make a
        // mistyped key an unrecoverable one.
        if (state.cancelled) field.focused = false;
    }
}

} // namespace

int Update(entt::registry& registry, const UIRect& gameRect,
           const UICanvas::UIPointer& pointer,
           const UICanvas::UIKeyboard& keyboard) {
    const glm::vec2 screenSize = gameRect.size();
    if (screenSize.x < 1.0f || screenSize.y < 1.0f) return 0;

    const float scale = UICanvas::ScaleFor(screenSize);
    int clicked = 0;

    updateTextFields(registry, gameRect, scale, pointer, keyboard);

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
