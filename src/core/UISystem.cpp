#include "core/UISystem.hpp"

#include "core/Components.hpp"
#include "core/UIInput.hpp"

#include "imgui.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Supersonic {

namespace UISystem {

namespace {

ImU32 toColor(const glm::vec4& color) {
    return ImGui::GetColorU32(ImVec4(color.r, color.g, color.b, color.a));
}

ImVec2 toVec(const glm::vec2& v) { return ImVec2(v.x, v.y); }

// Which layer an element draws on. Absent means zero.
//
// NOT the same field a stack uses to order its children, though it was: see
// UIOrderComponent, where a menu with ranked buttons on a raised layer is the
// case that forced them apart. Within one layer the type order below still
// holds - shapes, panels, buttons, fields, text - so a label still reads on top
// of its own backdrop.
int32_t layerOf(const entt::registry& registry, entt::entity entity) {
    if (const auto* ordering = registry.try_get<UIOrderComponent>(entity)) return ordering->layer;
    return 0;
}

// Every distinct layer in the scene, low to high.
//
// Godot spells this CanvasLayer.layer, and Wolf Brigade uses exactly three
// values: the HUD at the default, the pause menu at 9 and the game-over overlay
// at 10. Without it a pause menu draws underneath the HUD it is meant to cover,
// which reads as the menu being broken rather than as a z-order question.
//
// Returns a single zero when nothing is ranked, so the common scene runs the
// draw loops once and pays nothing for a feature it is not using.
std::vector<int32_t> layersPresent(const entt::registry& registry) {
    std::vector<int32_t> layers;
    for (auto [entity, ordering] : registry.view<UIOrderComponent>().each()) {
        (void)entity;
        layers.push_back(ordering.layer);
    }
    layers.push_back(0);
    std::sort(layers.begin(), layers.end());
    layers.erase(std::unique(layers.begin(), layers.end()), layers.end());
    return layers;
}

// The rectangle the layout pass assigned, or the one the element places for
// itself. Mirrors UIInput's helper of the same shape, and exists for the same
// reason: an element inside a stack is no longer using its own anchor, and
// drawing it where the anchor says while hit-testing it where the stack says is
// a click target that does not match the picture.
UIRect placedRect(const UICanvas::StackedLayout& stacked, entt::entity entity, UIAnchor anchor,
                  const glm::vec2& offset, const glm::vec2& size, const UIRect& gameRect,
                  float scale) {
    if (const auto it = stacked.rects.find(entity); it != stacked.rects.end()) return it->second;
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
UICanvas::StackedLayout layoutStacksImpl(entt::registry& registry, const UIRect& gameRect,
                                         ImFont* font, float scale) {
    UICanvas::StackedLayout stacked;
    auto stacks = registry.view<UIStackComponent>();
    if (stacks.begin() == stacks.end()) return stacked;

    // Children by parent, gathered ONCE.
    //
    // The previous shape walked every HierarchyComponent per stack, which is
    // fine for one menu and quadratic for a dock. It also could not nest,
    // which is the reason this was rewritten.
    std::unordered_map<entt::entity, std::vector<entt::entity>> childrenOf;
    for (auto [entity, hierarchy] : registry.view<HierarchyComponent>().each()) {
        if (hierarchy.parent == entt::null) continue;
        childrenOf[hierarchy.parent].push_back(entity);
    }

    // What one element measures, in authored units.
    //
    // A NESTED STACK MEASURES ITS OWN CONTENTS, which is the whole of what was
    // missing: the chain below used to test text, panel, button and field and
    // stop, so a stack inside a stack failed every branch, was skipped, and
    // then laid itself out against the SCREEN - landing on top of whatever its
    // parent had put there. Nothing reported it, because both passes agreed.
    //
    // Recursive, and bounded by a depth limit. NOT for cycles - those are
    // unreachable, because an entity has one parent, so every member of a
    // cycle has a stack for a parent and is skipped by the root test below
    // before any of this runs. The cap is for legitimate DEPTH, and it is the
    // one thing here a malformed-but-acyclic tree could otherwise run away
    // with.
    std::function<bool(entt::entity, int, glm::vec2&)> measure =
        [&](entt::entity entity, int depth, glm::vec2& out) -> bool {
        if (depth > 16) return false;

        if (const auto* text = registry.try_get<UITextComponent>(entity)) {
            if (!text->visible) return false;
            // Measured at the authored size and divided back out, because
            // LayoutStack works in authored units and applies scale itself.
            //
            // AND MEASURED WRAPPED, which is the half that makes a wrapped
            // label usable inside a stack: the height a paragraph needs is
            // the answer to the wrap rather than something the author knows
            // in advance, so a stack that measured it unwrapped would reserve
            // one line and let the rest print over whatever came next.
            const ImVec2 measured =
                font->CalcTextSizeA(text->fontSize * scale, FLT_MAX, text->wrapWidth * scale,
                                    text->text.c_str());
            out = glm::vec2(measured.x, measured.y) / scale;
            return true;
        }
        if (const auto* panel = registry.try_get<UIPanelComponent>(entity)) {
            if (!panel->visible) return false;
            out = panel->size;
            return true;
        }
        if (const auto* image = registry.try_get<UIImageComponent>(entity)) {
            if (!image->visible) return false;
            out = image->size;
            return true;
        }
        if (const auto* button = registry.try_get<UIButtonComponent>(entity)) {
            // The `visible` check the four branches above have, which these two
            // did not. Without it a hidden button was measured in and given a
            // rectangle, then skipped by the draw pass - a hole exactly its own
            // size, with everything below pushed down by the hole plus a
            // separation. Wolf Brigade's main menu hides one 440x104 button
            // inside a twelve-child column whenever there is no run to
            // continue, so its boot screen sat off centre.
            if (!button->visible) return false;
            out = button->size;
            return true;
        }
        if (const auto* field = registry.try_get<UITextFieldComponent>(entity)) {
            if (!field->visible) return false;
            out = field->size;
            return true;
        }
        if (const auto* nested = registry.try_get<UIStackComponent>(entity)) {
            if (!nested->visible) return false;

            const auto it = childrenOf.find(entity);
            if (it == childrenOf.end()) return false;

            std::vector<glm::vec2> sizes;
            for (entt::entity child : it->second) {
                glm::vec2 childSize(0.0f);
                if (measure(child, depth + 1, childSize)) sizes.push_back(childSize);
            }
            if (sizes.empty()) return false;

            out = UICanvas::MeasureStack(sizes, nested->horizontal, nested->spacing);
            return true;
        }
        return false;
    };

    // One stack, into the area it was given. Recurses so a nested stack is
    // placed inside its parent's slot rather than against the screen.
    std::function<void(entt::entity, const UIStackComponent&, const UIRect&, int)> place =
        [&](entt::entity stackEntity, const UIStackComponent& stack, const UIRect& area,
            int depth) {
        if (depth > 16) return;

        const auto it = childrenOf.find(stackEntity);
        if (it == childrenOf.end()) return;

        struct Child {
            entt::entity entity{entt::null};
            glm::vec2 size{0.0f};
            int32_t order{0};
        };
        std::vector<Child> children;

        for (entt::entity child : it->second) {
            glm::vec2 size(0.0f);
            if (!measure(child, depth + 1, size)) continue;
            const auto* ordering = registry.try_get<UIOrderComponent>(child);
            children.push_back(Child{child, size, ordering ? ordering->order : 0});
        }
        if (children.empty()) return;

        // Stable, so children nobody ranked keep the order the view produced -
        // the same rule the renderer's sort key follows.
        std::stable_sort(children.begin(), children.end(),
                         [](const Child& left, const Child& right) {
                             return left.order < right.order;
                         });

        std::vector<glm::vec2> sizes;
        sizes.reserve(children.size());
        for (const Child& child : children) sizes.push_back(child.size);

        // The AREA places and the game rect's SCALE sizes. Passing the area as
        // both would shrink a nested row's contents in proportion to the row.
        const std::vector<UIRect> rects =
            UICanvas::LayoutStack(sizes, stack.horizontal, stack.spacing, stack.anchor,
                                  stack.offset, area, scale);

        for (size_t i = 0; i < children.size() && i < rects.size(); ++i) {
            stacked.rects[children[i].entity] = rects[i];

            if (const auto* nested = registry.try_get<UIStackComponent>(children[i].entity)) {
                place(children[i].entity, *nested, rects[i], depth + 1);
            }
        }
    };

    // A HIDDEN STACK TAKES ITS WHOLE SUBTREE WITH IT.
    //
    // Skipping it below is not enough, and the difference is not subtle. Its
    // children never get a rectangle, and an element without one falls back to
    // placing itself from its own anchor - correct for an element that was
    // never in a container, wrong for one whose container is hidden.
    // UIButtonComponent::anchor defaults to Center with a zero offset, so
    // hiding a menu did not remove it: every button in it landed on the middle
    // of the screen, on top of one another, invisible and still clickable over
    // whatever was really there. That is how the game opens a modal - hide the
    // menu, show the box - in three separate screens.
    //
    // Every hidden stack, not only the roots: a hidden stack nested in a
    // visible one is skipped by `measure` and strands its children the same
    // way. The insert doubles as the cycle guard, so a malformed parent chain
    // stops rather than recurring.
    std::function<void(entt::entity, int)> suppress = [&](entt::entity entity, int depth) {
        if (depth > 16) return;
        const auto it = childrenOf.find(entity);
        if (it == childrenOf.end()) return;
        for (entt::entity child : it->second) {
            if (!stacked.hidden.insert(child).second) continue;
            suppress(child, depth + 1);
        }
    };

    for (auto [stackEntity, stack] : stacks.each()) {
        if (!stack.visible) suppress(stackEntity, 0);
    }

    // Only ROOTS start from the screen. A stack whose parent is itself a stack
    // is reached by recursion, with its parent's slot as its area - running it
    // here as well would place it twice and the second answer would win.
    //
    // WHICH answer wins depends on entt's view order, and this file already
    // says elsewhere that that order is not a contract. So removing this skip
    // is a bug whose symptom is non-deterministic, and a test cannot pin it -
    // the suite covers the containment invariant it protects instead. Said
    // here so the guard is not mistaken for something the tests are watching.
    for (auto [stackEntity, stack] : stacks.each()) {
        if (!stack.visible) continue;

        const auto* hierarchy = registry.try_get<HierarchyComponent>(stackEntity);
        if (hierarchy != nullptr && hierarchy->parent != entt::null &&
            registry.valid(hierarchy->parent) &&
            registry.try_get<UIStackComponent>(hierarchy->parent) != nullptr) {
            continue;
        }

        place(stackEntity, stack, gameRect, 0);
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

UICanvas::StackedLayout LayoutStacks(entt::registry& registry, const UIRect& gameRect,
                                     ImFont* font, float scale) {
    return layoutStacksImpl(registry, gameRect, font, scale);
}

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
    const UICanvas::StackedLayout stacked = layoutStacksImpl(registry, gameRect, font, scale);

    // Before drawing, so a button drawn this frame reflects the pointer this
    // frame rather than lagging it by one.
    UIInput::Update(registry, gameRect, pointer, keyboard, stacked);

    // Once per layer, low to high, so a pause menu covers the HUD it is drawn
    // over. Within a layer: shapes, then panels, then text, so a label always
    // reads on top of its backdrop regardless of the order the entities happen
    // to be in, and a selection marker stays under the HUD it is not part of.
    for (const int32_t layer : layersPresent(registry)) {
    for (auto [entity, shape] : registry.view<UIShapeComponent>().each()) {
        if (!shape.visible || stacked.Hidden(entity)) continue;
        if (layerOf(registry, entity) != layer) continue;

        // The anchor point, in pixels. Everything below is measured from here.
        glm::vec2 origin(0.0f);
        if (shape.worldSpace) {
            glm::vec3 projected(0.0f);
            if (!UICanvas::ProjectToScreen(viewProj, worldPositionOf(registry, entity), gameRect,
                                           projected)) {
                // Behind the camera or outside the depth range. Drawn anyway, a
                // marker behind the viewer lands mirrored in front of it and
                // reads as a selection nobody made.
                continue;
            }
            origin = glm::vec2(projected) + shape.offset * scale;
        } else {
            // Placed as a zero-sized rectangle: an anchor with no extent is a
            // point, which is what a circle centre and a line start are.
            origin = UICanvas::Place(shape.anchor, shape.offset * scale, glm::vec2(0.0f),
                                     gameRect).min;
        }

        const ImU32 color = toColor(shape.color);

        // At least one pixel. A marker that rounds down to nothing on a small
        // window is indistinguishable from one that was never drawn at all.
        const float thickness = std::max(1.0f, shape.thickness * scale);

        // ImGui reads 0 as "pick a segment count for me", which for a marker
        // that is meant to look hand-drawn at 40 segments is not the same
        // picture. Clamped rather than passed through.
        const int segments = std::max(3, static_cast<int>(shape.segments));

        switch (shape.kind) {
        case UIShapeComponent::Kind::Ring:
            draw->AddCircle(toVec(origin), std::max(1.0f, shape.radius * scale), color,
                            segments, thickness);
            break;
        case UIShapeComponent::Kind::Disc:
            draw->AddCircleFilled(toVec(origin), std::max(1.0f, shape.radius * scale), color,
                                  segments);
            break;
        case UIShapeComponent::Kind::Line: {
            glm::vec2 tip(0.0f);  // not "far": windows.h has a macro for that
            if (shape.worldSpace) {
                glm::vec3 projected(0.0f);
                // Culled on the FAR end too, and separately: half a line, from
                // a real start to a point mirrored behind the viewer, is worse
                // than no line - it points somewhere nothing is.
                if (!UICanvas::ProjectToScreen(viewProj, shape.endpoint, gameRect, projected)) {
                    continue;
                }
                tip = glm::vec2(projected) + shape.offset * scale;
            } else {
                tip = origin + glm::vec2(shape.endpoint) * scale;
            }
            draw->AddLine(toVec(origin), toVec(tip), color, thickness);
            break;
        }
        }
    }

    // Images first within a layer, so a panel can frame one and a label can
    // read on top of it. The same reasoning the type order already follows.
    for (auto [entity, image] : registry.view<UIImageComponent>().each()) {
        if (!image.visible || stacked.Hidden(entity)) continue;
        if (image.texture == 0) continue;   // still uploading, or never set
        if (layerOf(registry, entity) != layer) continue;

        const UIRect rect = placedRect(stacked, entity, image.anchor, image.offset,
                                       image.size, gameRect, scale);

        // Rounded needs the rounded overload, and ImGui only offers that one
        // without UVs - so a rounded image shows the whole texture. Said here
        // rather than discovered: an atlas icon asking for a corner radius
        // would otherwise silently show the entire sheet.
        if (image.cornerRadius > 0.0f) {
            draw->AddImageRounded(static_cast<ImTextureID>(image.texture),
                                  toVec(rect.min), toVec(rect.max),
                                  ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                                  toColor(image.tint), image.cornerRadius * scale);
        } else {
            draw->AddImage(static_cast<ImTextureID>(image.texture),
                           toVec(rect.min), toVec(rect.max),
                           toVec(image.uvMin), toVec(image.uvMax),
                           toColor(image.tint));
        }
    }

    for (auto [entity, panel] : registry.view<UIPanelComponent>().each()) {
        if (!panel.visible || stacked.Hidden(entity)) continue;
        if (layerOf(registry, entity) != layer) continue;

        const UIRect rect = UICanvas::Stretch(
            placedRect(stacked, entity, panel.anchor, panel.offset, panel.size, gameRect, scale),
            gameRect, panel.fillWidth, panel.fillHeight);
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
        if (!button.visible || stacked.Hidden(entity)) continue;
        if (layerOf(registry, entity) != layer) continue;

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
        if (!field.visible || stacked.Hidden(entity)) continue;
        if (layerOf(registry, entity) != layer) continue;

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
        if (!text.visible || text.text.empty() || stacked.Hidden(entity)) continue;
        if (layerOf(registry, entity) != layer) continue;

        const float size = text.fontSize * scale;
        if (size < 1.0f) continue;

        // Measured before placing: a right-anchored label has to know its own
        // width to put its right edge where it belongs - and a wrapped one has
        // to know its own HEIGHT, which is what the wrap decides.
        //
        // Scaled, because the width is authored at the reference height like
        // every other size here. A wrap width in raw pixels would break at a
        // different word on every display, which is the bug the whole authored-
        // units convention exists to prevent.
        const float wrap = text.wrapWidth * scale;
        const ImVec2 measured = font->CalcTextSizeA(size, FLT_MAX, wrap, text.text.c_str());

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
            if (const auto it = stacked.rects.find(entity); it != stacked.rects.end()) {
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
                          text.text.c_str(), nullptr, wrap);
        }

        // THE SAME WRAP THE MEASUREMENT USED, and the shadow's too. Three
        // places have to agree about where the lines break: a draw that
        // wrapped where the measurement did not would overflow the box the
        // stack reserved for it, and a shadow that wrapped differently from
        // its own glyphs would read as two overlapping paragraphs.
        draw->AddText(font, size, toVec(rect.min), toColor(text.color), text.text.c_str(),
                      nullptr, wrap);
    }

    }  // layer

    draw->PopClipRect();
}

} // namespace UISystem

} // namespace Supersonic
