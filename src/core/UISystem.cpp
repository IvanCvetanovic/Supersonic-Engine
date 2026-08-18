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

} // namespace

void Render(entt::registry& registry, const UIRect& gameRect,
            const UICanvas::UIPointer& pointer) {
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

    // Before drawing, so a button drawn this frame reflects the pointer this
    // frame rather than lagging it by one.
    UIInput::Update(registry, gameRect, pointer);

    // Panels first, then text, so a label always reads on top of its backdrop
    // regardless of the order the entities happen to be in.
    for (auto [entity, panel] : registry.view<UIPanelComponent>().each()) {
        if (!panel.visible) continue;

        const UIRect rect = UICanvas::Place(panel.anchor, panel.offset * scale,
                                            panel.size * scale, gameRect);
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

    ImFont* font = ImGui::GetFont();

    // Interaction ran above; this only draws what it decided.
    for (auto [entity, button] : registry.view<UIButtonComponent>().each()) {
        if (!button.visible) continue;

        // The same placement UIInput used, from the same component - not a
        // second calculation that could disagree with it.
        const UIRect rect = UICanvas::Place(button.anchor, button.offset * scale,
                                            button.size * scale, gameRect);

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

    for (auto [entity, text] : registry.view<UITextComponent>().each()) {
        if (!text.visible || text.text.empty()) continue;

        const float size = text.fontSize * scale;
        if (size < 1.0f) continue;

        // Measured before placing: a right-anchored label has to know its own
        // width to put its right edge where it belongs.
        const ImVec2 measured = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.text.c_str());
        const UIRect rect = UICanvas::PlaceMeasured(text.anchor, text.offset * scale,
                                                    glm::vec2(measured.x, measured.y), gameRect);

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
