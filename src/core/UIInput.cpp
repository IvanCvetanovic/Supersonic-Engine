#include "core/UIInput.hpp"

#include <algorithm>

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

// The rectangle the layout pass assigned, or the one the element places for
// itself. One helper, used by both passes, so the two cannot disagree.
UIRect rectFor(const UICanvas::StackedRects& stacked, entt::entity entity, UIAnchor anchor,
               const glm::vec2& offset, const glm::vec2& size, const UIRect& gameRect,
               float scale) {
    if (const auto it = stacked.find(entity); it != stacked.end()) return it->second;
    return UICanvas::Place(anchor, offset * scale, size * scale, gameRect);
}

// Which layer an element is on. Absent means zero, exactly as UISystem reads it.
int32_t layerOf(const entt::registry& registry, entt::entity entity) {
    if (const auto* ordering = registry.try_get<UIOrderComponent>(entity)) return ordering->order;
    return 0;
}

// Where an element sits in the order UISystem draws things.
//
// One comparable value for the three rules that decide what ends up on top:
// the layer first, then the type - panels, then buttons, then fields - and
// then position within its own view, because within a type the last one drawn
// is the one you can see. Input has to sort by the same thing the renderer
// draws by, or the pointer goes to something that is not on screen.
struct DrawPosition {
    int32_t layer{0};
    int32_t type{0};
    int32_t index{0};

    bool operator<(const DrawPosition& other) const {
        if (layer != other.layer) return layer < other.layer;
        if (type != other.type) return type < other.type;
        return index < other.index;
    }
    bool operator==(const DrawPosition& other) const {
        return layer == other.layer && type == other.type && index == other.index;
    }
};

constexpr int32_t kPanelType = 0;
constexpr int32_t kButtonType = 1;
constexpr int32_t kFieldType = 2;

// The one element the pointer is actually on, or nothing.
//
// Every button used to answer for itself, which is right only while no two of
// them overlap. Two that do both highlighted and both fired on a single click,
// and once a pause menu can be drawn over a HUD the overlap stops being a
// mistake and becomes the ordinary case: an overlay that covers the health bar
// but lets the button under it keep taking clicks is not covering anything.
//
// Panels count, and are the reason a modal works without a flag saying so: a
// panel is drawn under every button on its own layer, so it can only ever
// block one that is on a LOWER layer - which is exactly the backdrop of a menu
// swallowing clicks meant for the game behind it. Labels do not count, the
// same way Godot's Label ignores the mouse: text over a button is a caption,
// not a lid.
struct Topmost {
    DrawPosition where{};
    bool any{false};

    void consider(const DrawPosition& candidate) {
        if (!any || where < candidate) {
            where = candidate;
            any = true;
        }
    }
    bool is(const DrawPosition& candidate) const { return any && where == candidate; }
};

Topmost topmostUnderPointer(const entt::registry& registry, const UIRect& gameRect, float scale,
                            const UICanvas::StackedRects& stacked,
                            const UICanvas::UIPointer& pointer) {
    Topmost topmost;
    if (!pointer.active) return topmost;

    int32_t index = 0;
    for (auto [entity, panel] : registry.view<const UIPanelComponent>().each()) {
        const int32_t at = index++;
        if (!panel.visible) continue;
        // The whole panel, not the drawn fraction: a health bar that has run
        // down is still a rectangle in the way, and a hit target that shrinks
        // as the player takes damage would be its own bug.
        const UIRect rect = rectFor(stacked, entity, panel.anchor, panel.offset,
                                    panel.size, gameRect, scale);
        if (UICanvas::Contains(rect, pointer.position)) {
            topmost.consider(DrawPosition{layerOf(registry, entity), kPanelType, at});
        }
    }

    index = 0;
    for (auto [entity, button] : registry.view<const UIButtonComponent>().each()) {
        const int32_t at = index++;
        // Disabled, not invisible: a greyed-out menu item is still drawn, so it
        // still covers what is beneath it. Hidden is what removes a target.
        if (!button.visible) continue;
        const UIRect rect = rectFor(stacked, entity, button.anchor, button.offset,
                                    button.size, gameRect, scale);
        if (UICanvas::Contains(rect, pointer.position)) {
            topmost.consider(DrawPosition{layerOf(registry, entity), kButtonType, at});
        }
    }

    index = 0;
    for (auto [entity, field] : registry.view<const UITextFieldComponent>().each()) {
        const int32_t at = index++;
        if (!field.visible) continue;
        const UIRect rect = rectFor(stacked, entity, field.anchor, field.offset,
                                    field.size, gameRect, scale);
        if (UICanvas::Contains(rect, pointer.position)) {
            topmost.consider(DrawPosition{layerOf(registry, entity), kFieldType, at});
        }
    }

    return topmost;
}

// Focus, which is the first decision in this file that is about the SCENE
// rather than about one element.
//
// A button can be resolved entity by entity because the answer only depends on
// where the pointer is. Focus cannot: exactly one field may hold it, so a flat
// per-entity loop would hand the keystroke to every field under the cursor and
// to none of them correctly. One pass decides, a second applies.
void updateTextFields(entt::registry& registry, const UIRect& gameRect, float scale,
                      const UICanvas::StackedRects& stacked, const Topmost& topmost,
                      const UICanvas::UIPointer& pointer,
                      const UICanvas::UIKeyboard& keyboard) {
    auto view = registry.view<UITextFieldComponent>();

    // A press EDGE decides focus outright - on a field it takes it, anywhere
    // else it gives it up. Absent a press, focus is KEPT: moving the pointer
    // off a field, or off the viewport entirely, must not lose half a name.
    const bool pressEdge = pointer.active && pointer.down && !pointer.wasDown;

    entt::entity pressedField = entt::null;
    entt::entity focusedField = entt::null;

    int32_t index = 0;
    for (auto entity : view) {
        auto& field = view.get<UITextFieldComponent>(entity);
        const DrawPosition at{layerOf(registry, entity), kFieldType, index++};

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

        // Contains AND topmost. A field under an overlay is behind glass: it
        // still draws, and the pointer stops at whatever is over it.
        const UIRect rect = rectFor(stacked, entity, field.anchor, field.offset,
                                    field.size, gameRect, scale);
        field.hovered = pointer.active && topmost.is(at) &&
                        UICanvas::Contains(rect, pointer.position);

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

void Update(entt::registry& registry, const UIRect& gameRect,
            const UICanvas::UIPointer& pointer,
            const UICanvas::UIKeyboard& keyboard,
            const UICanvas::StackedRects& stacked) {
    const glm::vec2 screenSize = gameRect.size();
    if (screenSize.x < 1.0f || screenSize.y < 1.0f) return;

    const float scale = UICanvas::ScaleFor(screenSize);

    // Who the pointer belongs to, decided once for the whole scene before any
    // element is asked about itself - the same shape as focus below, and for
    // the same reason: "am I under the pointer" is answerable per entity,
    // "am I the one under the pointer" is not.
    const Topmost topmost = topmostUnderPointer(registry, gameRect, scale, stacked, pointer);

    updateTextFields(registry, gameRect, scale, stacked, topmost, pointer, keyboard);

    int32_t index = 0;
    for (auto [entity, button] : registry.view<UIButtonComponent>().each()) {
        const DrawPosition at{layerOf(registry, entity), kButtonType, index++};

        // An invisible button is not a click target. Hiding a menu is how a
        // game closes it, and a hidden menu that still swallowed clicks would
        // block the game underneath it.
        if (!button.visible) {
            button.hovered = false;
            button.pressed = false;
            button.clicked = false;
            continue;
        }

        const UIRect rect = rectFor(stacked, entity, button.anchor, button.offset,
                                    button.size, gameRect, scale);

        // A disabled button is drawn but not live, so an unavailable menu item
        // stays where it is instead of moving everything below it. A button
        // that is not the topmost thing under the pointer is treated the same
        // way: still drawn, still where it was, taking nothing.
        UICanvas::UIPointer effective = pointer;
        effective.active = pointer.active && button.enabled && topmost.is(at);

        const UICanvas::UIButtonState previous{ button.hovered, button.pressed, button.clicked };
        const UICanvas::UIButtonState state =
            UICanvas::UpdateButton(previous, rect, effective);

        button.hovered = state.hovered;
        button.pressed = state.pressed;
        button.clicked = state.clicked;

        // Held for whichever tick asks next. Set, never cleared here: clearing
        // is BeginTickClicks' job, because the whole point is that it survives
        // the frames between one tick and the next.
        if (state.clicked) button.clickPending = true;
    }
}

void BeginTickClicks(entt::registry& registry) {
    // The mirror of Input::BeginTickInput, and it is here rather than beside
    // that one because a click belongs to an entity and Input deliberately
    // knows nothing about the registry.
    for (auto [entity, button] : registry.view<UIButtonComponent>().each()) {
        button.clickedThisTick = button.clickPending;
        button.clickPending = false;
    }
}

std::vector<uint32_t> ClicksThisTick(const entt::registry& registry) {
    std::vector<uint32_t> clicked;
    for (auto [entity, button] : registry.view<const UIButtonComponent>().each()) {
        if (button.clickedThisTick) clicked.push_back(static_cast<uint32_t>(entt::to_entity(entity)));
    }

    // Sorted, because EnTT's iteration order comes from how components were
    // added rather than from the state - so two runs holding the same clicks
    // could write them in different orders and a byte comparison of two
    // recordings would call that a difference. The same argument StateHash
    // makes about being order-independent, made here by fixing the order.
    //
    // By INDEX, not by handle: the index is what survives a reload, and it is
    // what the hash is seeded on for the same reason.
    std::sort(clicked.begin(), clicked.end());
    return clicked;
}

void BeginReplayedTickClicks(entt::registry& registry, const std::vector<uint32_t>& clicked) {
    // Every button assigned, not only the ones in the list. Leaving the rest
    // alone would let a live click through beside the recorded ones, so the
    // replay would be of something that never happened.
    for (auto [entity, button] : registry.view<UIButtonComponent>().each()) {
        const auto index = static_cast<uint32_t>(entt::to_entity(entity));
        button.clickedThisTick =
            std::find(clicked.begin(), clicked.end(), index) != clicked.end();

        // The pending latch is emptied too. A replayed tick must not leave a
        // real click queued for the tick after it.
        button.clickPending = false;
    }
}

void DiscardPendingClicks(entt::registry& registry) {
    // Clicks made while nothing is ticking - in the editor, on a pause menu
    // that stops the sim, during a scrub. A latch is only correct while
    // something is coming to empty it, and while no tick is running nothing is,
    // so these would otherwise all arrive together on the first tick after
    // Play. `clickedThisTick` is left alone: it belongs to a tick that has
    // already begun and may still be being read.
    for (auto [entity, button] : registry.view<UIButtonComponent>().each()) {
        button.clickPending = false;
    }
}

} // namespace UIInput

} // namespace Supersonic
