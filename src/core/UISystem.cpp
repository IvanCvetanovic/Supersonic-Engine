#include "core/UISystem.hpp"

#include "core/Components.hpp"
#include "core/UIInput.hpp"

#include "imgui.h"

namespace Supersonic {

namespace UISystem {

namespace {

ImU32 toColor(const glm::vec4& color) {
    return ImGui::GetColorU32(ImVec4(color.r, color.g, color.b, color.a));
}

ImVec2 toVec(const glm::vec2& v) { return ImVec2(v.x, v.y); }

// The rectangle the layout pass assigned, or the one the element places for
// itself. Mirrors UIInput's helper of the same shape, and exists for the same
// reason: an element inside a stack is no longer using its own anchor, and
// drawing it where the anchor says while hit-testing it where the stack says is
// a click target that does not match the picture.
UIRect placedRect(const UICanvas::StackedRects& stacked, entt::entity entity, UIAnchor anchor,
                  const glm::vec2& offset, const glm::vec2& size, const UIRect& gameRect,
                  float scale) {
    if (const auto it = stacked.find(entity); it != stacked.end()) return it->second;
    return UICanvas::Place(anchor, offset * scale, size * scale, gameRect);
}

// Runs every stack and records where each child ended up.
//
// Measuring is why this lives here rather than in UICanvas: a label's size is
// whatever the font says it is, and the font is an ImGui object. Panels,
// buttons and fields carry an authored size and need no measuring at all.
//
// The result is computed ONCE and used by both the input pass and the draw
// pass. Letting each derive it would be two places that have to agree about
// where a button is, which is the failure UISystem.hpp's header already warns
// about for anchors and would be worse here.
UICanvas::StackedRects layoutStacks(entt::registry& registry, const UIRect& gameRect,
                                    ImFont* font, float scale) {
    UICanvas::StackedRects stacked;
    auto stacks = registry.view<UIStackComponent>();
    if (stacks.begin() == stacks.end()) return stacked;

    for (auto [stackEntity, stack] : stacks.each()) {
        if (!stack.visible) continue;

        // Children, by the hierarchy the rest of the engine already uses.
        struct Child {
            entt::entity entity{entt::null};
            glm::vec2 size{0.0f};
            int32_t order{0};
        };
        std::vector<Child> children;

        for (auto [entity, hierarchy] : registry.view<HierarchyComponent>().each()) {
            if (hierarchy.parent != stackEntity) continue;

            glm::vec2 size(0.0f);
            bool isUi = false;
            if (const auto* text = registry.try_get<UITextComponent>(entity)) {
                if (!text->visible) continue;
                // Measured at the authored size and divided back out, because
                // LayoutStack works in authored units and applies scale itself.
                const ImVec2 measured =
                    font->CalcTextSizeA(text->fontSize * scale, FLT_MAX, 0.0f,
                                        text->text.c_str());
                size = glm::vec2(measured.x, measured.y) / scale;
                isUi = true;
            } else if (const auto* panel = registry.try_get<UIPanelComponent>(entity)) {
                if (!panel->visible) continue;
                size = panel->size;
                isUi = true;
            } else if (const auto* button = registry.try_get<UIButtonComponent>(entity)) {
                size = button->size;
                isUi = true;
            } else if (const auto* field = registry.try_get<UITextFieldComponent>(entity)) {
                size = field->size;
                isUi = true;
            }
            if (!isUi) continue;

            const auto* ordering = registry.try_get<UIOrderComponent>(entity);
            children.push_back(Child{entity, size, ordering ? ordering->order : 0});
        }

        if (children.empty()) continue;

        // Stable, so children nobody ranked keep the order the view produced -
        // the same rule the renderer's sort key follows.
        std::stable_sort(children.begin(), children.end(),
                         [](const Child& left, const Child& right) {
                             return left.order < right.order;
                         });

        std::vector<glm::vec2> sizes;
        sizes.reserve(children.size());
        for (const Child& child : children) sizes.push_back(child.size);

        const std::vector<UIRect> rects = UICanvas::LayoutStack(
            sizes, stack.horizontal, stack.spacing, stack.anchor, stack.offset, gameRect);

        for (size_t i = 0; i < children.size() && i < rects.size(); ++i) {
            stacked[children[i].entity] = rects[i];
        }
    }

    return stacked;
}

// Where a UI entity sits in the world.
//
// The resolved world transform when there is one, so a label parented to a unit
// follows it through the hierarchy rather than through anybody copying a
// position every frame. Falls back to the local transform for an entity the
// transform pass has not reached yet - a label created this frame, which is
// exactly what a floating damage number is.
glm::vec3 worldPositionOf(const entt::registry& registry, entt::entity entity) {
    if (const auto* world = registry.try_get<WorldTransformComponent>(entity)) {
        return glm::vec3(world->matrix[3]);
    }
    if (const auto* local = registry.try_get<TransformComponent>(entity)) {
        return local->position;
    }
    return glm::vec3(0.0f);
}

} // namespace

void Render(entt::registry& registry, const UIRect& gameRect,
            const UICanvas::UIPointer& pointer,
            const UICanvas::UIKeyboard& keyboard,
            const glm::mat4& viewProj) {
    const glm::vec2 screenSize = gameRect.size();
    if (screenSize.x < 1.0f || screenSize.y < 1.0f) return;

    // The window's own draw list rather than the foreground one, so the HUD
    // obeys ImGui's stacking: over the game image, under anything the user has
    // floating above the viewport while authoring it.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!draw) return;

    // Clipped to the game rectangle. In the editor that is the viewport panel,
    // and without this a HUD anchored to the bottom of the screen would spill
    // across the inspector below it.
    draw->PushClipRect(toVec(gameRect.min), toVec(gameRect.max), true);

    const float scale = UICanvas::ScaleFor(screenSize);

    ImFont* font = ImGui::GetFont();

    // Layout BEFORE input, and input before drawing. A stacked button has to
    // be hit-tested against where the stack put it, and drawn there too - all
    // three from one answer.
    const UICanvas::StackedRects stacked = layoutStacks(registry, gameRect, font, scale);

    // Before drawing, so a button drawn this frame reflects the pointer this
    // frame rather than lagging it by one.
    UIInput::Update(registry, gameRect, pointer, keyboard, stacked);

    // Panels first, then text, so a label always reads on top of its backdrop
    // regardless of the order the entities happen to be in.
    for (auto [entity, panel] : registry.view<UIPanelComponent>().each()) {
        if (!panel.visible) continue;

        const UIRect rect = placedRect(stacked, entity, panel.anchor, panel.offset,
                                       panel.size, gameRect, scale);
        const float rounding = panel.cornerRadius * scale;

        if (panel.drawTrack) {
            draw->AddRectFilled(toVec(rect.min), toVec(rect.max),
                                toColor(panel.trackColor), rounding);
        }

        const UIRect filled = UICanvas::FillHorizontal(rect, panel.fill);
        if (filled.size().x >= 1.0f) {
            // The fill keeps the panel's rounding, but a bar at 20% would
            // otherwise round its right edge in mid-air. Rounding only the left
            // corners when it is short of full is what makes it read as a bar
            // rather than a floating pill.
            const bool full = filled.size().x >= rect.size().x - 0.5f;
            const ImDrawFlags corners = full ? ImDrawFlags_RoundCornersAll
                                             : ImDrawFlags_RoundCornersLeft;
            draw->AddRectFilled(toVec(filled.min), toVec(filled.max),
                                toColor(panel.color), rounding, corners);
        }
    }


    // Interaction ran above; this only draws what it decided.
    for (auto [entity, button] : registry.view<UIButtonComponent>().each()) {
        if (!button.visible) continue;

        // The same placement UIInput used, from the same component - not a
        // second calculation that could disagree with it.
        const UIRect rect = placedRect(stacked, entity, button.anchor, button.offset,
                                       button.size, gameRect, scale);

        // Pressed beats hovered: while held, the button reads as held even
        // though the pointer is still over it.
        glm::vec4 fill = button.color;
        if (!button.enabled)      fill = button.disabledColor;
        else if (button.pressed)  fill = button.pressColor;
        else if (button.hovered)  fill = button.hoverColor;

        draw->AddRectFilled(toVec(rect.min), toVec(rect.max), toColor(fill),
                            button.cornerRadius * scale);

        if (!button.label.empty()) {
            const float size = button.fontSize * scale;
            if (size >= 1.0f) {
                const ImVec2 measured = font->CalcTextSizeA(size, FLT_MAX, 0.0f,
                                                            button.label.c_str());
                // Centred on the button rather than placed by anchor: a label
                // is part of the button, not an element in its own right.
                const glm::vec2 centre = (rect.min + rect.max) * 0.5f;
                const ImVec2 origin(centre.x - measured.x * 0.5f,
                                    centre.y - measured.y * 0.5f);

                glm::vec4 textColor = button.textColor;
                if (!button.enabled) textColor.a *= 0.45f;

                draw->AddText(font, size, origin, toColor(textColor), button.label.c_str());
            }
        }
    }

    // Fields between the buttons and the loose text, so a field's own contents
    // read over its box and under nothing.
    for (auto [entity, field] : registry.view<UITextFieldComponent>().each()) {
        if (!field.visible) continue;

        const UIRect rect = placedRect(stacked, entity, field.anchor, field.offset,
                                       field.size, gameRect, scale);

        const bool live = field.focused && field.enabled;
        draw->AddRectFilled(toVec(rect.min), toVec(rect.max),
                            toColor(live ? field.focusColor : field.color),
                            field.cornerRadius * scale);
        draw->AddRect(toVec(rect.min), toVec(rect.max),
                      toColor(live ? field.focusBorderColor : field.borderColor),
                      field.cornerRadius * scale, 0, live ? 2.0f * scale : 1.0f * scale);

        const float size = field.fontSize * scale;
        if (size < 1.0f) continue;

        // The placeholder only while there is nothing to show. Dimmed, and
        // never once a character has been typed, or a name would read as a
        // suggestion.
        const bool empty = field.text.empty();
        const std::string& shown = empty ? field.placeholder : field.text;

        const float padding = 10.0f * scale;
        const ImVec2 measured = font->CalcTextSizeA(size, FLT_MAX, 0.0f, shown.c_str());
        const float baseline = (rect.min.y + rect.max.y) * 0.5f - measured.y * 0.5f;
        const ImVec2 origin(rect.min.x + padding, baseline);

        if (!shown.empty()) {
            draw->PushClipRect(ImVec2(rect.min.x + padding * 0.5f, rect.min.y),
                               ImVec2(rect.max.x - padding * 0.5f, rect.max.y), true);
            draw->AddText(font, size, origin,
                          toColor(empty ? field.placeholderColor : field.textColor),
                          shown.c_str());
            draw->PopClipRect();
        }

        if (!live) continue;

        // The caret, measured by asking the font how wide the text BEFORE it
        // is. Deriving it from a character count would put it in the wrong
        // place the moment a name contains a wide letter or an accent.
        const std::string before = field.text.substr(
            0, static_cast<size_t>(std::max(0, field.caret)));
        const float caretX = origin.x +
            font->CalcTextSizeA(size, FLT_MAX, 0.0f, before.c_str()).x;

        // Blinking on the wall clock rather than on a frame counter, so it
        // keeps time when the frame rate does not - and it stays lit for the
        // longer half of the cycle, which is what makes a caret easy to find.
        if (std::fmod(ImGui::GetTime(), 1.06) < 0.66) {
            draw->AddLine(ImVec2(caretX, rect.min.y + padding),
                          ImVec2(caretX, rect.max.y - padding),
                          toColor(field.textColor), std::max(1.0f, scale));
        }
    }

    for (auto [entity, text] : registry.view<UITextComponent>().each()) {
        if (!text.visible || text.text.empty()) continue;

        const float size = text.fontSize * scale;
        if (size < 1.0f) continue;

        // Measured before placing: a right-anchored label has to know its own
        // width to put its right edge where it belongs.
        const ImVec2 measured = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.text.c_str());

        UIRect rect;
        if (text.worldSpace) {
            glm::vec3 screen(0.0f);
            if (!UICanvas::ProjectToScreen(viewProj, worldPositionOf(registry, entity), gameRect,
                                           screen)) {
                // Behind the camera, or outside the depth range. Drawn anyway,
                // a label behind the viewer lands mirrored in front of it.
                continue;
            }

            // Centred horizontally on the point and offset from it, which is
            // what a name plate over a unit means. The anchor is unused here -
            // there is no screen edge to hang from.
            rect.min = glm::vec2(screen.x - measured.x * 0.5f, screen.y - measured.y * 0.5f) +
                       text.offset * scale;
            rect.max = rect.min + glm::vec2(measured.x, measured.y);
        } else {
            if (const auto it = stacked.find(entity); it != stacked.end()) {
                // The stack decided where; the measurement decided how big.
                rect.min = it->second.min;
                rect.max = rect.min + glm::vec2(measured.x, measured.y);
            } else {
                rect = UICanvas::PlaceMeasured(text.anchor, text.offset * scale,
                                               glm::vec2(measured.x, measured.y), gameRect);
            }
        }

        if (text.shadow) {
            // Offset by a fraction of the size rather than a fixed pixel, so
            // the shadow stays proportional at every resolution. White text
            // over a bright sky is unreadable without it.
            const float drop = std::max(1.0f, size * 0.06f);
            draw->AddText(font, size, ImVec2(rect.min.x + drop, rect.min.y + drop),
                          IM_COL32(0, 0, 0, static_cast<int>(text.color.a * 160.0f)),
                          text.text.c_str());
        }

        draw->AddText(font, size, toVec(rect.min), toColor(text.color), text.text.c_str());
    }

    draw->PopClipRect();
}

} // namespace UISystem

} // namespace Supersonic
