// Regression tests for the pointer running over a scene's UI.
//
// UICanvas tests the rule for one button in isolation. This tests the pass
// that applies it to a scene: the right button out of several, at the position
// its component actually asks for, with the flags landing on the component a
// script will read.
//
// Deliberately without a window. Whether a click lands on the right button is
// not a question about a renderer, and it was previously answerable only by
// driving the real UI with synthetic mouse events and looking at a screenshot
// - which fails for reasons that have nothing to do with the engine, as it did
// here when another application took the foreground.

#include "TestHarness.hpp"
#include "core/Components.hpp"
#include "core/UIInput.hpp"

#include <string>

using namespace Supersonic;

namespace {

// A 1920x1080 game area at the origin, so authored units are pixels and the
// expected positions can be worked out by hand.
UIRect screen() {
    return { glm::vec2(0.0f, 0.0f), glm::vec2(1920.0f, 1080.0f) };
}

UICanvas::UIPointer pointerAt(const glm::vec2& position, bool down, bool wasDown,
                              bool active = true) {
    UICanvas::UIPointer pointer;
    pointer.position = position;
    pointer.down = down;
    pointer.wasDown = wasDown;
    pointer.active = active;
    return pointer;
}

// A button of a known size at a known place, so the test can aim at it.
entt::entity addButton(entt::registry& registry, const std::string& label,
                       UIAnchor anchor, const glm::vec2& offset,
                       const glm::vec2& size = glm::vec2(200.0f, 60.0f)) {
    const auto entity = registry.create();
    auto& button = registry.emplace<UIButtonComponent>(entity);
    button.label = label;
    button.anchor = anchor;
    button.offset = offset;
    button.size = size;
    return entity;
}

// Presses and releases at a point, as three frames of pointer input.
void clickAt(entt::registry& registry, const glm::vec2& point) {
    UIInput::Update(registry, screen(), pointerAt(point, false, false));
    UIInput::Update(registry, screen(), pointerAt(point, true, false));
    UIInput::Update(registry, screen(), pointerAt(point, false, true));
}

} // namespace

static void testAClickReachesTheButtonUnderThePointer() {
    entt::registry registry;
    // Top-left, 40 in from each edge: occupies (40,40) to (240,100).
    const auto button = addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    clickAt(registry, glm::vec2(140.0f, 70.0f));

    CHECK_MSG(registry.get<UIButtonComponent>(button).clicked,
              "a press and release over the button must set its clicked flag");
}

static void testOnlyTheButtonUnderThePointerIsClicked() {
    // Two buttons in a menu. Clicking one must not fire the other, which is
    // the failure that turns a menu into a lottery.
    entt::registry registry;
    const auto play = addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const auto quit = addButton(registry, "Quit", UIAnchor::TopLeft, glm::vec2(40.0f, 200.0f));

    clickAt(registry, glm::vec2(140.0f, 70.0f));

    CHECK(registry.get<UIButtonComponent>(play).clicked);
    CHECK_MSG(!registry.get<UIButtonComponent>(quit).clicked,
              "clicking one menu item must not fire the one below it");
    CHECK_MSG(!registry.get<UIButtonComponent>(quit).hovered,
              "and it must not even be hovered");
}

static void testClickingBetweenTwoButtonsHitsNeither() {
    entt::registry registry;
    const auto play = addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const auto quit = addButton(registry, "Quit", UIAnchor::TopLeft, glm::vec2(40.0f, 200.0f));

    // The gap between them: below the first, above the second.
    clickAt(registry, glm::vec2(140.0f, 150.0f));

    CHECK(!registry.get<UIButtonComponent>(play).clicked);
    CHECK(!registry.get<UIButtonComponent>(quit).clicked);
}

static void testUpdateReportsHowManyWereClicked() {
    // The count is what lets the game ask "did the UI take this click?" before
    // passing it to the world - otherwise pressing a menu button also shoots.
    entt::registry registry;
    addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    // Held in locals: Update advances state, so calling it inside a check that
    // might evaluate its argument more than once would run the frame twice.
    const int onPress = UIInput::Update(registry, screen(),
                                        pointerAt(glm::vec2(140.0f, 70.0f), true, false));
    const int onRelease = UIInput::Update(registry, screen(),
                                          pointerAt(glm::vec2(140.0f, 70.0f), false, true));
    const int afterwards = UIInput::Update(registry, screen(),
                                           pointerAt(glm::vec2(140.0f, 70.0f), false, false));

    CHECK_MSG(onPress == 0, "the press is not the click");
    CHECK_MSG(onRelease == 1, "the release over the button is");
    CHECK_MSG(afterwards == 0, "and it is not reported again the frame after");
}

static void testClickingEmptySpaceReportsNothing() {
    entt::registry registry;
    addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    UIInput::Update(registry, screen(), pointerAt(glm::vec2(900.0f, 900.0f), true, false));
    const int clicked = UIInput::Update(registry, screen(),
                                        pointerAt(glm::vec2(900.0f, 900.0f), false, true));
    CHECK_MSG(clicked == 0, "clicking empty space must report nothing");
}

static void testAHiddenButtonIsNotAClickTarget() {
    // Hiding a menu is how a game closes it. A hidden menu that still ate
    // clicks would block the game underneath it.
    entt::registry registry;
    const auto button = addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    registry.get<UIButtonComponent>(button).visible = false;

    clickAt(registry, glm::vec2(140.0f, 70.0f));

    const auto& state = registry.get<UIButtonComponent>(button);
    CHECK_MSG(!state.clicked && !state.hovered, "a hidden button must not react at all");
}

static void testADisabledButtonDrawsButDoesNotReact() {
    entt::registry registry;
    const auto button = addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    registry.get<UIButtonComponent>(button).enabled = false;

    clickAt(registry, glm::vec2(140.0f, 70.0f));

    const auto& state = registry.get<UIButtonComponent>(button);
    CHECK_MSG(!state.clicked, "a disabled button must not click");
    CHECK_MSG(state.visible, "but it stays visible, so the menu does not reflow");
}

static void testAButtonFollowsItsAnchorAcrossResolutions() {
    // The click target has to move with the button. A bottom-right menu button
    // authored at 1080p and played at 720p is exactly where this breaks.
    entt::registry registry;
    const auto button = addButton(registry, "Quit", UIAnchor::BottomRight,
                                  glm::vec2(50.0f, 50.0f), glm::vec2(200.0f, 60.0f));

    const UIRect small{ glm::vec2(0.0f), glm::vec2(1280.0f, 720.0f) };
    const float scale = 720.0f / 1080.0f;

    // Where the button now is: in from the bottom-right by the scaled offset.
    const glm::vec2 centre(1280.0f - (50.0f + 100.0f) * scale,
                            720.0f - (50.0f + 30.0f) * scale);

    UIInput::Update(registry, small, pointerAt(centre, true, false));
    UIInput::Update(registry, small, pointerAt(centre, false, true));
    CHECK_MSG(registry.get<UIButtonComponent>(button).clicked,
              "the click target must follow the button when the window changes size");

    // And the place it would have been at the authored size must now miss.
    const glm::vec2 authored(1920.0f - 150.0f, 1080.0f - 80.0f);
    UIInput::Update(registry, small, pointerAt(authored, true, false));
    UIInput::Update(registry, small, pointerAt(authored, false, true));
    CHECK_MSG(!registry.get<UIButtonComponent>(button).clicked,
              "and must not still be where the button was authored");
}

static void testTheGameRectOffsetIsHonoured() {
    // In the editor the game occupies a panel, so the pointer arrives in
    // window coordinates and the button lives inside an inset rectangle. A
    // pass that ignored the offset would put every click target in the wrong
    // place by exactly the panel's position.
    entt::registry registry;
    const auto button = addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    const UIRect panel{ glm::vec2(300.0f, 100.0f), glm::vec2(2220.0f, 1180.0f) };

    UIInput::Update(registry, panel, pointerAt(glm::vec2(440.0f, 170.0f), true, false));
    UIInput::Update(registry, panel, pointerAt(glm::vec2(440.0f, 170.0f), false, true));
    CHECK_MSG(registry.get<UIButtonComponent>(button).clicked,
              "the button sits at the panel's origin plus its offset");

    UIInput::Update(registry, panel, pointerAt(glm::vec2(140.0f, 70.0f), true, false));
    UIInput::Update(registry, panel, pointerAt(glm::vec2(140.0f, 70.0f), false, true));
    CHECK_MSG(!registry.get<UIButtonComponent>(button).clicked,
              "and not at the window origin plus its offset");
}

static void testADegenerateGameRectIsIgnored() {
    // A minimised window. Nothing should be hovered, and nothing should divide
    // by a zero-height screen on the way to finding out.
    entt::registry registry;
    const auto button = addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    const UIRect none{ glm::vec2(0.0f), glm::vec2(0.0f) };
    const int clicked = UIInput::Update(registry, none, pointerAt(glm::vec2(0.0f), true, false));
    CHECK_MSG(clicked == 0, "a zero-size game area has nothing to click");
    CHECK(!registry.get<UIButtonComponent>(button).hovered);
}

static void runTests() {
    testAClickReachesTheButtonUnderThePointer();
    testOnlyTheButtonUnderThePointerIsClicked();
    testClickingBetweenTwoButtonsHitsNeither();
    testUpdateReportsHowManyWereClicked();
    testClickingEmptySpaceReportsNothing();
    testAHiddenButtonIsNotAClickTarget();
    testADisabledButtonDrawsButDoesNotReact();
    testAButtonFollowsItsAnchorAcrossResolutions();
    testTheGameRectOffsetIsHonoured();
    testADegenerateGameRectIsIgnored();
}

TEST_MAIN("test_uiinput")
