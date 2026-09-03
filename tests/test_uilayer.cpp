// What order the HUD is drawn in.
//
// Every other UI test in this suite checks where an element lands. This one
// checks WHEN it is drawn, which is a different question with the same
// symptom: a pause menu placed exactly right is still broken if the health bar
// it is covering draws on top of it.
//
// The engine has no way to screenshot this. The HUD goes into an ImGui draw
// list which is composited into the swapchain, while --screenshot reads back
// the offscreen colour target the 3D scene rendered into - so the picture CI
// compares does not contain the HUD at all. What it does contain, exactly and
// in order, is the vertex buffer ImGui produced. So that is what is measured
// here: a headless context, one frame, and the order the coloured rectangles
// appear in ImDrawData.

#include "TestHarness.hpp"

#include "core/Components.hpp"
#include "core/UISystem.hpp"

#include <entt/entt.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

using namespace Supersonic;

namespace {

// Colours chosen to be unmistakable and unlike anything ImGui draws for
// itself, so "the first vertex of this colour" identifies one rectangle.
constexpr ImU32 kHudPanel = IM_COL32(255, 0, 0, 255);
constexpr ImU32 kHudButton = IM_COL32(0, 255, 0, 255);
constexpr ImU32 kOverlay = IM_COL32(0, 0, 255, 255);

glm::vec4 toVec4(ImU32 color) {
    return glm::vec4(static_cast<float>((color >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
                     static_cast<float>((color >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
                     static_cast<float>((color >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
                     static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
}

// A context with no graphics backend at all.
//
// ImGui 1.92 builds glyphs on demand instead of requiring an uploaded atlas,
// so declaring RendererHasTextures is the whole of what a backend has to
// provide here. Nothing is ever uploaded, and nothing needs to be: the draw
// data is produced on the CPU, and that is what is being read.
struct HeadlessImGui {
    HeadlessImGui() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1920.0f, 1080.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.IniFilename = nullptr;
    }
    ~HeadlessImGui() { ImGui::DestroyContext(); }
    HeadlessImGui(const HeadlessImGui&) = delete;
    HeadlessImGui& operator=(const HeadlessImGui&) = delete;
};

// A health bar, a button beside it, and an overlay meant to cover both.
void buildScene(entt::registry& registry, bool layered) {
    const auto bar = registry.create();
    auto& panel = registry.emplace<UIPanelComponent>(bar);
    panel.anchor = UIAnchor::TopLeft;
    panel.offset = glm::vec2(24.0f, 24.0f);
    panel.size = glm::vec2(320.0f, 32.0f);
    panel.color = toVec4(kHudPanel);
    panel.cornerRadius = 0.0f;

    const auto action = registry.create();
    auto& button = registry.emplace<UIButtonComponent>(action);
    button.label.clear();
    button.anchor = UIAnchor::TopLeft;
    button.offset = glm::vec2(24.0f, 80.0f);
    button.size = glm::vec2(200.0f, 48.0f);
    button.color = toVec4(kHudButton);
    button.cornerRadius = 0.0f;

    const auto pause = registry.create();
    auto& cover = registry.emplace<UIPanelComponent>(pause);
    cover.anchor = UIAnchor::Center;
    cover.offset = glm::vec2(0.0f, 0.0f);
    cover.size = glm::vec2(600.0f, 400.0f);
    cover.color = toVec4(kOverlay);
    cover.cornerRadius = 0.0f;
    if (layered) registry.emplace<UIOrderComponent>(pause).layer = 9;
}

// One frame, and what came out of it.
struct Frame {
    // Where each colour FIRST appears, in draw order. -1 for a colour that was
    // never drawn at all, which is a different failure from "drawn in the
    // wrong place" and worth telling apart.
    int firstHudPanel{-1};
    int firstHudButton{-1};
    int firstOverlay{-1};

    // And how many vertices each one contributed, which is how "drawn twice"
    // is told from "drawn once".
    int countHudPanel{0};
    int countHudButton{0};
    int countOverlay{0};

    bool complete() const {
        return firstHudPanel >= 0 && firstHudButton >= 0 && firstOverlay >= 0;
    }
};

Frame drawOnce(entt::registry& registry) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(1920.0f, 1080.0f));
    ImGui::Begin("game", nullptr, ImGuiWindowFlags_NoDecoration);

    UICanvas::UIPointer pointer;
    // Off the screen entirely: a hovered button recolours itself, and a test
    // that identifies rectangles by colour would then be looking for one that
    // is not there.
    pointer.position = glm::vec2(-4000.0f, -4000.0f);
    UICanvas::UIKeyboard keyboard;

    UISystem::Render(registry,
                     UIRect{glm::vec2(0.0f, 0.0f), glm::vec2(1920.0f, 1080.0f)},
                     pointer, keyboard, glm::mat4(1.0f));

    ImGui::End();
    ImGui::Render();

    Frame frame;
    int index = 0;
    const ImDrawData* data = ImGui::GetDrawData();
    for (int list = 0; data != nullptr && list < data->CmdListsCount; ++list) {
        const ImDrawList* commands = data->CmdLists[list];
        for (int v = 0; v < commands->VtxBuffer.Size; ++v, ++index) {
            const ImU32 color = commands->VtxBuffer[v].col;
            if (color == kHudPanel) {
                if (frame.firstHudPanel < 0) frame.firstHudPanel = index;
                ++frame.countHudPanel;
            } else if (color == kHudButton) {
                if (frame.firstHudButton < 0) frame.firstHudButton = index;
                ++frame.countHudButton;
            } else if (color == kOverlay) {
                if (frame.firstOverlay < 0) frame.firstOverlay = index;
                ++frame.countOverlay;
            }
        }
    }
    return frame;
}

void testAnUnlayeredHudDrawsPanelsThenButtons() {
    // The order this engine has always used, and the reason a layer is needed
    // at all: EVERY panel is drawn before ANY button, so a panel cannot be put
    // over a button by moving it in the scene or by creating it later.
    HeadlessImGui imgui;
    entt::registry registry;
    buildScene(registry, false);

    const Frame frame = drawOnce(registry);
    CHECK_MSG(frame.complete(), "all three elements must be drawn");
    if (!frame.complete()) return;

    CHECK_MSG(frame.firstOverlay < frame.firstHudButton,
              "unranked, the overlay draws with the other panels and under the button");

    // Nothing is claimed about which of the two PANELS comes first. entt's
    // iteration order is not a contract - this suite was first written
    // expecting creation order and got the reverse - and that is precisely why
    // UIOrderComponent exists rather than being read off the scene.
}

void testALayeredOverlayDrawsOverEverythingBelowIt() {
    // The whole point. Godot spells it CanvasLayer.layer; Wolf Brigade puts its
    // pause menu on 9 and its game-over screen on 10, over a HUD on the
    // default. If the overlay does not come last here, both of those screens
    // open with the HUD punched through them.
    HeadlessImGui imgui;
    entt::registry registry;
    buildScene(registry, true);

    const Frame frame = drawOnce(registry);
    CHECK_MSG(frame.complete(), "all three elements must still be drawn");
    if (!frame.complete()) return;

    CHECK_MSG(frame.firstOverlay > frame.firstHudButton,
              "a layer 9 panel must draw after a layer 0 button");
    CHECK_MSG(frame.firstOverlay > frame.firstHudPanel,
              "and after a layer 0 panel");
    CHECK_MSG(frame.firstHudPanel < frame.firstHudButton,
              "and the type order still holds inside a layer");
}

void testEveryElementIsStillDrawnExactlyOnce() {
    // Running the four loops once per layer is a loop over layers wrapped
    // around a loop over entities, and the guard that keeps an element out of
    // the layers it does not belong to is easy to leave off. Without it every
    // element draws once per layer present: correct-looking for an opaque
    // colour, twice the cost, and visible only as translucent HUD elements
    // getting darker the moment a menu opens.
    HeadlessImGui imgui;
    entt::registry registry;

    entt::registry plain;
    buildScene(plain, false);
    const Frame unlayered = drawOnce(plain);

    buildScene(registry, true);
    const Frame layered = drawOnce(registry);

    CHECK_MSG(unlayered.complete() && layered.complete(), "all three must draw either way");
    if (!unlayered.complete() || !layered.complete()) return;

    // Two layers are present once the overlay is ranked, so a missing guard
    // doubles all three of these. Compared against the same scene without the
    // ranking rather than against a number written down here, so the check
    // survives ImGui changing how many vertices a rectangle costs.
    CHECK_EQ(layered.countHudPanel, unlayered.countHudPanel);
    CHECK_EQ(layered.countHudButton, unlayered.countHudButton);
    CHECK_EQ(layered.countOverlay, unlayered.countOverlay);
}

// --- world space ------------------------------------------------------------
//
// A marker over a unit and a name plate over its head are not anchored to a
// corner of the screen; they are anchored to a place in the world. That the
// projection is right is UICanvas's business and is tested there. What is
// tested here is the other half, which no unit test reaches: that a running
// frame actually PUTS the thing at the projected point rather than at the
// anchor it is no longer using.

constexpr ImU32 kMarker = IM_COL32(255, 0, 255, 255);

// A camera looking down -Z from five units back, so the origin projects to the
// centre of the screen and everything below can be worked out by hand.
Supersonic::CameraComponent lookingAtTheOrigin() {
    Supersonic::CameraComponent cam;
    cam.aspect = 1920.0f / 1080.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 100.0f;
    cam.position = glm::vec3(0.0f, 0.0f, 5.0f);
    cam.yaw = -90.0f;
    cam.pitch = 0.0f;
    cam.updateCameraVectors();
    return cam;
}

// The centre of everything drawn in one colour, and how many vertices it took.
struct Blob {
    glm::vec2 centre{0.0f};
    // The extent as well, because "is it centred on the point" is a question
    // about the two edges and cannot be answered by a mean: glyph shapes pull
    // the mean around by several pixels, and a label hung off the point rather
    // than centred on it moves it by half the string. Those are the same size.
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
    int vertices{0};
};

Blob blobOf(entt::registry& registry, const glm::mat4& viewProj, ImU32 wanted) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(1920.0f, 1080.0f));
    ImGui::Begin("game", nullptr, ImGuiWindowFlags_NoDecoration);

    UICanvas::UIPointer pointer;
    pointer.position = glm::vec2(-4000.0f, -4000.0f);
    UICanvas::UIKeyboard keyboard;
    UISystem::Render(registry, UIRect{glm::vec2(0.0f, 0.0f), glm::vec2(1920.0f, 1080.0f)},
                     pointer, keyboard, viewProj);

    ImGui::End();
    ImGui::Render();

    Blob blob;
    glm::vec2 sum(0.0f);
    const ImDrawData* data = ImGui::GetDrawData();
    for (int list = 0; data != nullptr && list < data->CmdListsCount; ++list) {
        const ImDrawList* commands = data->CmdLists[list];
        for (int v = 0; v < commands->VtxBuffer.Size; ++v) {
            if (commands->VtxBuffer[v].col != wanted) continue;
            const glm::vec2 at(commands->VtxBuffer[v].pos.x, commands->VtxBuffer[v].pos.y);
            if (blob.vertices == 0) {
                blob.min = at;
                blob.max = at;
            } else {
                blob.min = glm::min(blob.min, at);
                blob.max = glm::max(blob.max, at);
            }
            sum += at;
            ++blob.vertices;
        }
    }
    if (blob.vertices > 0) blob.centre = sum / static_cast<float>(blob.vertices);
    return blob;
}

entt::entity addDiscAt(entt::registry& registry, const glm::vec3& world) {
    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = world;
    auto& shape = registry.emplace<UIShapeComponent>(entity);
    shape.kind = UIShapeComponent::Kind::Disc;
    shape.radius = 30.0f;
    shape.color = toVec4(kMarker);
    return entity;
}

void testAWorldSpaceMarkerIsDrawnWhereTheCameraPutsIt() {
    HeadlessImGui imgui;
    entt::registry registry;
    const Supersonic::CameraComponent cam = lookingAtTheOrigin();
    const glm::mat4 viewProj = cam.getProjectionMatrix() * cam.getViewMatrix();

    // A point up and to the right of the origin, so "the centre of the screen"
    // is not the answer by accident.
    const glm::vec3 world(1.5f, 0.75f, 0.0f);
    addDiscAt(registry, world);

    glm::vec3 expected(0.0f);
    CHECK_MSG(UICanvas::ProjectToScreen(viewProj, world,
                                        UIRect{glm::vec2(0.0f, 0.0f), glm::vec2(1920.0f, 1080.0f)},
                                        expected),
              "the fixture point must be in front of the camera");

    const Blob blob = blobOf(registry, viewProj, kMarker);
    CHECK_MSG(blob.vertices > 0, "the marker must be drawn at all");
    if (blob.vertices == 0) return;

    // A filled circle's vertices average to its centre. Loose by a pixel,
    // because ImGui's fan has a centre vertex that pulls the mean very
    // slightly - the question is whether it is at the projected point or at a
    // screen corner, and those are hundreds of pixels apart.
    CHECK_MSG(std::fabs(blob.centre.x - expected.x) < 2.0f,
              "the marker must sit where the point projects in x");
    CHECK_MSG(std::fabs(blob.centre.y - expected.y) < 2.0f,
              "and in y");
    CHECK_MSG(expected.x > 1000.0f && expected.y < 540.0f,
              "and the fixture must be off-centre, or this proves nothing");
}

void testTheSameMarkerInScreenSpaceIgnoresTheCameraEntirely() {
    // The control. Same component, same colour, same radius - only worldSpace
    // is different, and the answer must move to the anchor.
    HeadlessImGui imgui;
    entt::registry registry;
    const Supersonic::CameraComponent cam = lookingAtTheOrigin();
    const glm::mat4 viewProj = cam.getProjectionMatrix() * cam.getViewMatrix();

    const auto entity = addDiscAt(registry, glm::vec3(1.5f, 0.75f, 0.0f));
    auto& shape = registry.get<UIShapeComponent>(entity);
    shape.worldSpace = false;
    shape.anchor = UIAnchor::TopLeft;
    shape.offset = glm::vec2(100.0f, 60.0f);

    const Blob blob = blobOf(registry, viewProj, kMarker);
    CHECK_MSG(blob.vertices > 0, "a screen-space marker must still be drawn");
    if (blob.vertices == 0) return;

    CHECK_MSG(std::fabs(blob.centre.x - 100.0f) < 2.0f, "it must sit at its anchor in x");
    CHECK_MSG(std::fabs(blob.centre.y - 60.0f) < 2.0f, "and in y");
}

void testAMarkerBehindTheCameraIsNotDrawnAtAll() {
    // Without the cull it projects to a MIRRORED position in front of the
    // viewer, which reads as a selection nobody made, on a unit that is not
    // there. Absent is the right answer, not "somewhere harmless".
    HeadlessImGui imgui;
    entt::registry registry;
    const Supersonic::CameraComponent cam = lookingAtTheOrigin();
    const glm::mat4 viewProj = cam.getProjectionMatrix() * cam.getViewMatrix();

    addDiscAt(registry, glm::vec3(1.5f, 0.75f, 20.0f)); // behind the eye at z=5

    const Blob blob = blobOf(registry, viewProj, kMarker);
    CHECK_EQ(blob.vertices, 0);
}

void testAWorldSpaceLabelIsDrawnWhereTheCameraPutsIt() {
    // 2.4 of the port plan, in a running frame rather than as a projection
    // function with tests. A name plate is centred on the point rather than
    // hung off it, so its glyph vertices straddle the projected x.
    HeadlessImGui imgui;
    entt::registry registry;
    const Supersonic::CameraComponent cam = lookingAtTheOrigin();
    const glm::mat4 viewProj = cam.getProjectionMatrix() * cam.getViewMatrix();

    const glm::vec3 world(-1.5f, 0.5f, 0.0f);
    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = world;
    auto& text = registry.emplace<UITextComponent>(entity);
    text.text = "Wolf";
    text.worldSpace = true;
    // Zeroed, and it does not default to zero: UITextComponent::offset is
    // {24,24} for a screen-anchored label, and 24 pixels is a third of this
    // string. A name plate would set it deliberately to sit above a head.
    text.offset = glm::vec2(0.0f, 0.0f);
    text.shadow = false;  // one colour, so the blob is only the glyphs
    text.color = toVec4(kMarker);

    glm::vec3 expected(0.0f);
    CHECK(UICanvas::ProjectToScreen(viewProj, world,
                                    UIRect{glm::vec2(0.0f, 0.0f), glm::vec2(1920.0f, 1080.0f)},
                                    expected));

    const Blob blob = blobOf(registry, viewProj, kMarker);
    CHECK_MSG(blob.vertices > 0, "the label must actually reach the draw list");
    if (blob.vertices == 0) return;

    // The two edges, not the mean. A label hung off the point instead of
    // centred on it puts the point ON its left edge, so the left gap collapses
    // to nothing while the right one grows to the whole string - and a mean
    // moves by exactly as much as glyph shapes move it anyway.
    const float leftGap = expected.x - blob.min.x;
    const float rightGap = blob.max.x - expected.x;
    const float width = blob.max.x - blob.min.x;
    CHECK_MSG(width > 20.0f, "the label must have some width to be centred at all");
    CHECK_MSG(std::fabs(leftGap - rightGap) < width * 0.25f,
              "the projected point must fall in the middle of the label, not at its edge");

    const float topGap = expected.y - blob.min.y;
    const float bottomGap = blob.max.y - expected.y;
    const float height = blob.max.y - blob.min.y;
    CHECK_MSG(std::fabs(topGap - bottomGap) < height * 0.6f,
              "and near the middle vertically - glyphs sit high in their line box");

    CHECK_MSG(expected.x < 800.0f, "and the fixture must be left of centre");
}

// The box every glyph of one colour lands in, which for a paragraph is the
// paragraph's own extent.
struct Extent {
    float minX{1e9f}, minY{1e9f}, maxX{-1e9f}, maxY{-1e9f};
    int vertices{0};

    float width() const { return maxX - minX; }
    float height() const { return maxY - minY; }
};

// `screen` is the rect the UI is laid out against, and it is a parameter
// rather than a constant because the reference height is 1080: at exactly that
// size every authored unit is one pixel, the scale factor is one, and a
// mutation that dropped the scale entirely changed nothing any test could see.
Extent measureDrawn(entt::registry& registry, ImU32 colour,
                    const glm::vec2& screen = glm::vec2(1920.0f, 1080.0f)) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(screen.x, screen.y));
    ImGui::Begin("game", nullptr, ImGuiWindowFlags_NoDecoration);

    UICanvas::UIPointer pointer;
    pointer.position = glm::vec2(-4000.0f, -4000.0f);
    UICanvas::UIKeyboard keyboard;
    UISystem::Render(registry, UIRect{glm::vec2(0.0f), screen},
                     pointer, keyboard, glm::mat4(1.0f));

    ImGui::End();
    ImGui::Render();

    Extent out;
    const ImDrawData* data = ImGui::GetDrawData();
    for (int list = 0; data != nullptr && list < data->CmdListsCount; ++list) {
        const ImDrawList* commands = data->CmdLists[list];
        for (int v = 0; v < commands->VtxBuffer.Size; ++v) {
            if (commands->VtxBuffer[v].col != colour) continue;
            const ImVec2 p = commands->VtxBuffer[v].pos;
            out.minX = std::min(out.minX, p.x);
            out.minY = std::min(out.minY, p.y);
            out.maxX = std::max(out.maxX, p.x);
            out.maxY = std::max(out.maxY, p.y);
            ++out.vertices;
        }
    }
    return out;
}

constexpr ImU32 kParagraph = IM_COL32(11, 222, 33, 255);

entt::entity addParagraph(entt::registry& registry, float wrapWidth) {
    const auto entity = registry.create();
    auto& text = registry.emplace<UITextComponent>(entity);
    text.text = "The convoy will reach the ridge at first light and the escort "
                "is expected to break formation as soon as the shelling starts.";
    text.anchor = UIAnchor::TopLeft;
    text.offset = glm::vec2(40.0f, 40.0f);
    text.fontSize = 24.0f;
    text.color = toVec4(kParagraph);
    text.wrapWidth = wrapWidth;

    // Off, so every vertex of this colour is a glyph rather than a glyph and
    // its shadow at a different alpha.
    text.shadow = false;
    return entity;
}

static void testAParagraphWithNoWrapWidthRunsOffTheScreen() {
    // The behaviour every label had, and must keep: a score, a timer and a
    // unit name are one line by nature. This is also the control for the case
    // below - without it, a test that says a wrapped paragraph is narrow would
    // pass against a font that was simply small.
    HeadlessImGui imgui;
    entt::registry registry;
    addParagraph(registry, 0.0f);

    const Extent drawn = measureDrawn(registry, kParagraph);
    CHECK_MSG(drawn.vertices > 0, "the paragraph is drawn at all");
    CHECK_MSG(drawn.width() > 1200.0f,
              "one line, and a long one: it runs most of the way across a 1920 screen");
    CHECK_MSG(drawn.height() < 40.0f, "and it is one line high");
}

static void testAWrapWidthBreaksTheParagraphIntoLines() {
    HeadlessImGui imgui;
    entt::registry registry;
    addParagraph(registry, 400.0f);

    const Extent drawn = measureDrawn(registry, kParagraph);
    CHECK_MSG(drawn.vertices > 0, "still drawn");
    CHECK_MSG(drawn.width() <= 400.0f,
              "no line is wider than the width it was given");
    CHECK_MSG(drawn.height() > 60.0f,
              "and it is several lines tall, which is the whole point");
}

static void testTheSameWordsAreDrawnWhetherTheyWrapOrNot() {
    // Wrapping must break lines, not drop words. A wrap that silently
    // truncated would look exactly like a paragraph that happened to fit.
    HeadlessImGui imgui;

    entt::registry wide;
    addParagraph(wide, 0.0f);
    const int unwrapped = measureDrawn(wide, kParagraph).vertices;

    entt::registry narrow;
    addParagraph(narrow, 400.0f);
    const int wrapped = measureDrawn(narrow, kParagraph).vertices;

    CHECK_MSG(wrapped == unwrapped,
              "the same glyphs, in a different arrangement - not fewer of them");
}

static void testARightAnchoredParagraphIsPlacedByItsWrappedWidth() {
    // THE MEASUREMENT THAT PLACES, which the cases above cannot see: they all
    // hang off the top-left, where the anchor IS the left edge and the width
    // decides nothing. A right-anchored label subtracts its own width from the
    // right edge, so measuring it unwrapped puts it a full unwrapped line to
    // the left - most of the way off the other side of the screen.
    HeadlessImGui imgui;
    entt::registry registry;

    const auto entity = addParagraph(registry, 400.0f);
    auto& text = registry.get<UITextComponent>(entity);
    text.anchor = UIAnchor::TopRight;
    text.offset = glm::vec2(40.0f, 40.0f);

    const Extent drawn = measureDrawn(registry, kParagraph);
    CHECK_MSG(drawn.vertices > 0, "drawn");
    CHECK_MSG(drawn.maxX <= 1920.0f - 40.0f + 2.0f,
              "its right edge sits where the anchor put it");
    CHECK_MSG(drawn.minX > 1920.0f - 40.0f - 420.0f,
              "and its left edge is one WRAPPED width in, not one unwrapped line:" +
                  std::to_string(drawn.minX));
}

static void testTheWrapWidthIsAuthoredUnitsAndScalesWithTheScreen() {
    // Every size here is authored at the reference height and scaled, so a
    // paragraph breaks at the same WORD on every display. A wrap width passed
    // through in raw pixels would break at a different word on each one, which
    // is the bug the whole authored-units convention exists to prevent - and
    // it is invisible at 1080 exactly, where the scale is one.
    HeadlessImGui imgui;
    entt::registry registry;
    addParagraph(registry, 400.0f);

    const Extent atReference = measureDrawn(registry, kParagraph);
    const Extent atDouble =
        measureDrawn(registry, kParagraph, glm::vec2(3840.0f, 2160.0f));

    CHECK_MSG(atReference.vertices > 0 && atDouble.vertices > 0, "drawn at both sizes");
    CHECK_MSG(atDouble.width() > atReference.width() * 1.8f,
              "at twice the height the paragraph is twice as wide in pixels:" +
                  std::to_string(atReference.width()) + " then " +
                  std::to_string(atDouble.width()));
    CHECK_MSG(atDouble.vertices == atReference.vertices,
              "and it is the same words broken in the same places, not a different wrap");
}

static void testAWrappedLabelIsGivenTheHeightItsWrapNeeds() {
    // THE HALF A DRAW-ONLY CHANGE WOULD SILENTLY GET WRONG. A stack asks each
    // child how big it is and reserves that much room; measuring a paragraph
    // unwrapped there reserves one line, and every line after the first
    // prints over whatever comes next.
    HeadlessImGui imgui;
    entt::registry registry;

    const auto column = registry.create();
    auto& stack = registry.emplace<UIStackComponent>(column);
    stack.horizontal = false;
    stack.spacing = 8.0f;
    stack.anchor = UIAnchor::TopLeft;
    stack.offset = glm::vec2(40.0f, 40.0f);

    const auto paragraph = addParagraph(registry, 400.0f);
    registry.emplace<HierarchyComponent>(paragraph).parent = column;
    registry.emplace<UIOrderComponent>(paragraph).order = 0;

    const auto below = registry.create();
    auto& marker = registry.emplace<UIPanelComponent>(below);
    marker.size = glm::vec2(120.0f, 24.0f);
    marker.color = toVec4(kHudPanel);
    registry.emplace<HierarchyComponent>(below).parent = column;
    registry.emplace<UIOrderComponent>(below).order = 1;

    const Extent text = measureDrawn(registry, kParagraph);
    const Extent panel = measureDrawn(registry, kHudPanel);

    CHECK_MSG(text.vertices > 0 && panel.vertices > 0, "both are drawn");
    CHECK_MSG(panel.minY >= text.maxY,
              "the panel below the paragraph starts below ALL of it, not below its first line");
}

} // namespace

static void runTests() {
    testAnUnlayeredHudDrawsPanelsThenButtons();
    testALayeredOverlayDrawsOverEverythingBelowIt();
    testEveryElementIsStillDrawnExactlyOnce();

    testAWorldSpaceMarkerIsDrawnWhereTheCameraPutsIt();
    testTheSameMarkerInScreenSpaceIgnoresTheCameraEntirely();
    testAMarkerBehindTheCameraIsNotDrawnAtAll();
    testAWorldSpaceLabelIsDrawnWhereTheCameraPutsIt();

    testAParagraphWithNoWrapWidthRunsOffTheScreen();
    testAWrapWidthBreaksTheParagraphIntoLines();
    testTheSameWordsAreDrawnWhetherTheyWrapOrNot();
    testARightAnchoredParagraphIsPlacedByItsWrappedWidth();
    testTheWrapWidthIsAuthoredUnitsAndScalesWithTheScreen();
    testAWrappedLabelIsGivenTheHeightItsWrapNeeds();
}

TEST_MAIN("test_uilayer", 30)
