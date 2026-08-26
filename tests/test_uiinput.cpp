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


// No containers in these cases: every element here places itself from its own
// anchor, which is the path this suite is about. Stack layout has its own tests
// in test_uicanvas.
static const UICanvas::StackedRects kNoStacks;

namespace {

// A 1920x1080 game area at the origin, so authored units are pixels and the
// expected positions can be worked out by hand.
UIRect screen() {
    return { glm::vec2(0.0f, 0.0f), glm::vec2(1920.0f, 1080.0f) };
}

// No typing, which is what almost every case here wants. The keyboard is a
// separate argument precisely so a caller cannot forget it and get a field that
// silently cannot be typed into.
UICanvas::UIKeyboard noKeyboard() { return UICanvas::UIKeyboard{}; }

// One frame of typing.
UICanvas::UIKeyboard typing(const unsigned int* characters, int count) {
    UICanvas::UIKeyboard keyboard;
    keyboard.characters = characters;
    keyboard.characterCount = count;
    return keyboard;
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
    UIInput::Update(registry, screen(), pointerAt(point, false, false), noKeyboard(), kNoStacks);
    UIInput::Update(registry, screen(), pointerAt(point, true, false), noKeyboard(), kNoStacks);
    UIInput::Update(registry, screen(), pointerAt(point, false, true), noKeyboard(), kNoStacks);
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
                                        pointerAt(glm::vec2(140.0f, 70.0f), true, false), noKeyboard(), kNoStacks);
    const int onRelease = UIInput::Update(registry, screen(),
                                          pointerAt(glm::vec2(140.0f, 70.0f), false, true), noKeyboard(), kNoStacks);
    const int afterwards = UIInput::Update(registry, screen(),
                                           pointerAt(glm::vec2(140.0f, 70.0f), false, false), noKeyboard(), kNoStacks);

    CHECK_MSG(onPress == 0, "the press is not the click");
    CHECK_MSG(onRelease == 1, "the release over the button is");
    CHECK_MSG(afterwards == 0, "and it is not reported again the frame after");
}

static void testClickingEmptySpaceReportsNothing() {
    entt::registry registry;
    addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    UIInput::Update(registry, screen(), pointerAt(glm::vec2(900.0f, 900.0f), true, false), noKeyboard(), kNoStacks);
    const int clicked = UIInput::Update(registry, screen(),
                                        pointerAt(glm::vec2(900.0f, 900.0f), false, true), noKeyboard(), kNoStacks);
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

    UIInput::Update(registry, small, pointerAt(centre, true, false), noKeyboard(), kNoStacks);
    UIInput::Update(registry, small, pointerAt(centre, false, true), noKeyboard(), kNoStacks);
    CHECK_MSG(registry.get<UIButtonComponent>(button).clicked,
              "the click target must follow the button when the window changes size");

    // And the place it would have been at the authored size must now miss.
    const glm::vec2 authored(1920.0f - 150.0f, 1080.0f - 80.0f);
    UIInput::Update(registry, small, pointerAt(authored, true, false), noKeyboard(), kNoStacks);
    UIInput::Update(registry, small, pointerAt(authored, false, true), noKeyboard(), kNoStacks);
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

    UIInput::Update(registry, panel, pointerAt(glm::vec2(440.0f, 170.0f), true, false), noKeyboard(), kNoStacks);
    UIInput::Update(registry, panel, pointerAt(glm::vec2(440.0f, 170.0f), false, true), noKeyboard(), kNoStacks);
    CHECK_MSG(registry.get<UIButtonComponent>(button).clicked,
              "the button sits at the panel's origin plus its offset");

    UIInput::Update(registry, panel, pointerAt(glm::vec2(140.0f, 70.0f), true, false), noKeyboard(), kNoStacks);
    UIInput::Update(registry, panel, pointerAt(glm::vec2(140.0f, 70.0f), false, true), noKeyboard(), kNoStacks);
    CHECK_MSG(!registry.get<UIButtonComponent>(button).clicked,
              "and not at the window origin plus its offset");
}

static void testADegenerateGameRectIsIgnored() {
    // A minimised window. Nothing should be hovered, and nothing should divide
    // by a zero-height screen on the way to finding out.
    entt::registry registry;
    const auto button = addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    const UIRect none{ glm::vec2(0.0f), glm::vec2(0.0f) };
    const int clicked = UIInput::Update(registry, none, pointerAt(glm::vec2(0.0f), true, false), noKeyboard(), kNoStacks);
    CHECK_MSG(clicked == 0, "a zero-size game area has nothing to click");
    CHECK(!registry.get<UIButtonComponent>(button).hovered);
}

// --- focus ------------------------------------------------------------------
//
// The first decision in this file that is about the SCENE rather than about one
// element. A button resolves entity by entity because the answer only depends
// on where the pointer is; focus cannot, because exactly one field may hold it.

namespace {

entt::entity addField(entt::registry& registry, UIAnchor anchor, const glm::vec2& offset,
                      const glm::vec2& size = glm::vec2(300.0f, 60.0f)) {
    const auto entity = registry.create();
    auto& field = registry.emplace<UITextFieldComponent>(entity);
    field.anchor = anchor;
    field.offset = offset;
    field.size = size;
    field.maxLength = 0;
    return entity;
}

// A press EDGE at a point, which is what decides focus.
void pressAt(entt::registry& registry, const glm::vec2& point) {
    UIInput::Update(registry, screen(), pointerAt(point, false, false), noKeyboard(), kNoStacks);
    UIInput::Update(registry, screen(), pointerAt(point, true, false), noKeyboard(), kNoStacks);
}

} // namespace

static void testClickingAFieldFocusesExactlyOne() {
    entt::registry registry;
    const auto first = addField(registry, UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const auto second = addField(registry, UIAnchor::TopLeft, glm::vec2(40.0f, 200.0f));

    pressAt(registry, glm::vec2(100.0f, 60.0f));
    CHECK_MSG(registry.get<UITextFieldComponent>(first).focused, "the one under the pointer");
    CHECK_MSG(!registry.get<UITextFieldComponent>(second).focused, "and only that one");

    pressAt(registry, glm::vec2(100.0f, 220.0f));
    CHECK_MSG(!registry.get<UITextFieldComponent>(first).focused, "focus moves rather than spreading");
    CHECK(registry.get<UITextFieldComponent>(second).focused);
}

static void testClickingAnythingElseGivesUpFocus() {
    // Including a button, which is the case a per-entity design gets wrong: a
    // loop that only ever looked at fields would never hear about the click.
    entt::registry registry;
    const auto field = addField(registry, UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 400.0f));

    pressAt(registry, glm::vec2(100.0f, 60.0f));
    CHECK(registry.get<UITextFieldComponent>(field).focused);

    pressAt(registry, glm::vec2(100.0f, 420.0f));
    CHECK_MSG(!registry.get<UITextFieldComponent>(field).focused,
              "pressing a button takes the keyboard back from the field");

    pressAt(registry, glm::vec2(100.0f, 60.0f));
    pressAt(registry, glm::vec2(1500.0f, 900.0f));
    CHECK_MSG(!registry.get<UITextFieldComponent>(field).focused,
              "and so does pressing empty screen");
}

static void testMovingThePointerAwayDoesNotLoseTheTypedName() {
    // Focus is decided by a press and by nothing else. Losing it when the mouse
    // drifted off the box would make the field unusable, because typing does
    // not involve the mouse at all.
    entt::registry registry;
    const auto field = addField(registry, UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    pressAt(registry, glm::vec2(100.0f, 60.0f));

    const unsigned int characters[] = { 'I', 'v' };
    UIInput::Update(registry, screen(), pointerAt(glm::vec2(1500.0f, 900.0f), false, false),
                    typing(characters, 2), kNoStacks);

    const auto& state = registry.get<UITextFieldComponent>(field);
    CHECK_MSG(state.focused, "the pointer left and the keyboard stayed");
    CHECK_MSG(state.text == "Iv", state.text);
}

static void testAPointerWithNoPositionDecidesNothing() {
    // A locked pointer is invisible and unbounded, so its "position" is
    // wherever the virtual coordinates have wandered to. A player firing under
    // a captured cursor must not blur the focus of a field they cannot see.
    entt::registry registry;
    const auto field = addField(registry, UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    pressAt(registry, glm::vec2(100.0f, 60.0f));
    CHECK(registry.get<UITextFieldComponent>(field).focused);

    UIInput::Update(registry, screen(),
                    pointerAt(glm::vec2(-9000.0f, 40000.0f), true, false, /*active=*/false),
                    noKeyboard(), kNoStacks);
    CHECK_MSG(registry.get<UITextFieldComponent>(field).focused,
              "a press from a pointer that is not anywhere decides nothing");
}

static void testOnlyTheFocusedFieldReceivesCharacters() {
    entt::registry registry;
    const auto first = addField(registry, UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const auto second = addField(registry, UIAnchor::TopLeft, glm::vec2(40.0f, 200.0f));

    pressAt(registry, glm::vec2(100.0f, 60.0f));

    const unsigned int characters[] = { 'A' };
    UIInput::Update(registry, screen(), pointerAt(glm::vec2(100.0f, 60.0f), true, true),
                    typing(characters, 1), kNoStacks);

    CHECK_MSG(registry.get<UITextFieldComponent>(first).text == "A", "the focused one hears it");
    CHECK_MSG(registry.get<UITextFieldComponent>(second).text.empty(), "and nothing else does");
}

static void testHidingAFocusedFieldGivesTheKeyboardBack() {
    // A menu closes by hiding, and a hidden field that kept the keyboard would
    // leave the game unable to move with nothing on screen to explain why.
    entt::registry registry;
    const auto field = addField(registry, UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    pressAt(registry, glm::vec2(100.0f, 60.0f));
    CHECK(UIInput::AnyTextFieldFocused(registry));

    registry.get<UITextFieldComponent>(field).visible = false;
    UIInput::Update(registry, screen(), pointerAt(glm::vec2(100.0f, 60.0f), false, false),
                    noKeyboard(), kNoStacks);

    CHECK_MSG(!registry.get<UITextFieldComponent>(field).focused, "hidden means not focused");
    CHECK_MSG(!UIInput::AnyTextFieldFocused(registry),
              "which is what hands the keyboard back to the game");
}

static void testSubmitIsTrueForExactlyOneFrame() {
    entt::registry registry;
    const auto field = addField(registry, UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    pressAt(registry, glm::vec2(100.0f, 60.0f));

    UICanvas::UIKeyboard enter = noKeyboard();
    enter.submit = true;
    UIInput::Update(registry, screen(), pointerAt(glm::vec2(100.0f, 60.0f), true, true), enter, kNoStacks);
    CHECK(registry.get<UITextFieldComponent>(field).submitted);

    UIInput::Update(registry, screen(), pointerAt(glm::vec2(100.0f, 60.0f), true, true),
                    noKeyboard(), kNoStacks);
    CHECK_MSG(!registry.get<UITextFieldComponent>(field).submitted,
              "or a menu would answer itself every frame until the next keystroke");
}

static void testFocusPutsTheCaretAtTheEnd() {
    // Where someone clicking into a box that already has a name expects to
    // carry on from.
    entt::registry registry;
    const auto field = addField(registry, UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    registry.get<UITextFieldComponent>(field).text = "Ivan";

    pressAt(registry, glm::vec2(100.0f, 60.0f));
    CHECK_EQ(registry.get<UITextFieldComponent>(field).caret, 4);

    const unsigned int characters[] = { '!' };
    UIInput::Update(registry, screen(), pointerAt(glm::vec2(100.0f, 60.0f), true, true),
                    typing(characters, 1), kNoStacks);
    CHECK_MSG(registry.get<UITextFieldComponent>(field).text == "Ivan!",
              registry.get<UITextFieldComponent>(field).text);
}

// --- overlap ----------------------------------------------------------------
//
// Every case above has elements that do not touch, so each one could answer
// "am I under the pointer" for itself and be right. Once a pause menu can be
// drawn over a HUD, overlap is the ordinary case rather than an authoring
// mistake, and the question changes to "am I THE one under the pointer" -
// which no element can answer alone.

// A panel of a known size at a known place, on a given layer.
static entt::entity addPanel(entt::registry& registry, UIAnchor anchor,
                             const glm::vec2& offset, const glm::vec2& size, int32_t layer) {
    const auto entity = registry.create();
    auto& panel = registry.emplace<UIPanelComponent>(entity);
    panel.anchor = anchor;
    panel.offset = offset;
    panel.size = size;
    if (layer != 0) registry.emplace<UIOrderComponent>(entity).order = layer;
    return entity;
}

static void testTwoOverlappingButtonsDoNotBothFire() {
    // Both used to. They highlighted together and one click ran two actions,
    // which in a menu is "Resume" and "Quit to desktop" on the same press.
    entt::registry registry;
    const auto lower = addButton(registry, "Lower", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const auto upper = addButton(registry, "Upper", UIAnchor::TopLeft, glm::vec2(60.0f, 50.0f));

    clickAt(registry, glm::vec2(140.0f, 70.0f)); // inside both

    const bool lowerFired = registry.get<UIButtonComponent>(lower).clicked;
    const bool upperFired = registry.get<UIButtonComponent>(upper).clicked;
    CHECK_MSG(!(lowerFired && upperFired), "one click must not fire two buttons");
    CHECK_MSG(lowerFired || upperFired, "and it must still fire one of them");
}

static void testAButtonOnAHigherLayerTakesTheClick() {
    entt::registry registry;
    const auto hud = addButton(registry, "Fire", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const auto menu = addButton(registry, "Resume", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    registry.emplace<UIOrderComponent>(menu).order = 9;

    clickAt(registry, glm::vec2(140.0f, 70.0f));

    CHECK_MSG(registry.get<UIButtonComponent>(menu).clicked,
              "the layer 9 button is the one on screen and must take the click");
    CHECK_MSG(!registry.get<UIButtonComponent>(hud).clicked,
              "the layer 0 button underneath must not also fire");
}

static void testAnOverlayPanelSwallowsClicksMeantForTheGameBehindIt() {
    // What makes a pause menu modal, with no flag saying so. A panel is drawn
    // under every button on its own layer, so it can only ever block one on a
    // LOWER layer - which is the backdrop of a menu, and nothing else.
    entt::registry registry;
    const auto hud = addButton(registry, "Fire", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    addPanel(registry, UIAnchor::TopLeft, glm::vec2(0.0f, 0.0f), glm::vec2(1920.0f, 1080.0f), 9);

    clickAt(registry, glm::vec2(140.0f, 70.0f));

    CHECK_MSG(!registry.get<UIButtonComponent>(hud).clicked,
              "a button under a higher overlay must not be clickable");
    CHECK_MSG(!registry.get<UIButtonComponent>(hud).hovered,
              "and must not highlight either, or it reads as pressable");
}

static void testAPanelDoesNotBlockAButtonOnItsOwnLayer() {
    // The other half of the same rule, and the one that would break every HUD
    // built so far: a button sitting on its own backdrop is the normal way to
    // draw one, and the backdrop is drawn first precisely so the button is on
    // top of it.
    entt::registry registry;
    addPanel(registry, UIAnchor::TopLeft, glm::vec2(0.0f, 0.0f), glm::vec2(600.0f, 400.0f), 0);
    const auto button = addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    clickAt(registry, glm::vec2(140.0f, 70.0f));

    CHECK_MSG(registry.get<UIButtonComponent>(button).clicked,
              "a button drawn on top of a panel on the same layer must still work");
}

static void testADisabledButtonStillCoversWhatIsBeneathIt() {
    // Disabled is drawn; hidden is not. A greyed-out item that let clicks
    // through would fire whatever happened to be behind it, which is worse
    // than doing nothing and looks identical until it happens.
    entt::registry registry;
    const auto behind = addButton(registry, "Behind", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const auto greyed = addButton(registry, "Greyed", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    registry.get<UIButtonComponent>(greyed).enabled = false;
    registry.emplace<UIOrderComponent>(greyed).order = 9;

    clickAt(registry, glm::vec2(140.0f, 70.0f));

    CHECK_MSG(!registry.get<UIButtonComponent>(greyed).clicked, "a disabled button takes nothing");
    CHECK_MSG(!registry.get<UIButtonComponent>(behind).clicked,
              "and passes nothing on to what it covers");
}

static void testAHiddenOverlayStopsBlocking() {
    // Hiding is how a menu closes. If its backdrop kept swallowing clicks the
    // game would be unplayable after one pause, with nothing on screen to show
    // why - the exact failure the visible check on buttons already prevents.
    entt::registry registry;
    const auto hud = addButton(registry, "Fire", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const auto cover = addPanel(registry, UIAnchor::TopLeft, glm::vec2(0.0f, 0.0f),
                                glm::vec2(1920.0f, 1080.0f), 9);
    registry.get<UIPanelComponent>(cover).visible = false;

    clickAt(registry, glm::vec2(140.0f, 70.0f));

    CHECK_MSG(registry.get<UIButtonComponent>(hud).clicked,
              "closing the menu must give the game its clicks back");
}

static void testAFieldUnderAnOverlayCannotBeFocused() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& field = registry.emplace<UITextFieldComponent>(entity);
    field.anchor = UIAnchor::TopLeft;
    field.offset = glm::vec2(40.0f, 40.0f);
    field.size = glm::vec2(300.0f, 48.0f);

    addPanel(registry, UIAnchor::TopLeft, glm::vec2(0.0f, 0.0f), glm::vec2(1920.0f, 1080.0f), 9);

    UIInput::Update(registry, screen(), pointerAt(glm::vec2(140.0f, 60.0f), true, false),
                    noKeyboard(), kNoStacks);

    CHECK_MSG(!registry.get<UITextFieldComponent>(entity).focused,
              "a press on an overlay must not focus the field behind it");
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

    testClickingAFieldFocusesExactlyOne();
    testClickingAnythingElseGivesUpFocus();
    testMovingThePointerAwayDoesNotLoseTheTypedName();
    testAPointerWithNoPositionDecidesNothing();
    testOnlyTheFocusedFieldReceivesCharacters();
    testHidingAFocusedFieldGivesTheKeyboardBack();
    testSubmitIsTrueForExactlyOneFrame();
    testFocusPutsTheCaretAtTheEnd();

    testTwoOverlappingButtonsDoNotBothFire();
    testAButtonOnAHigherLayerTakesTheClick();
    testAnOverlayPanelSwallowsClicksMeantForTheGameBehindIt();
    testAPanelDoesNotBlockAButtonOnItsOwnLayer();
    testADisabledButtonStillCoversWhatIsBeneathIt();
    testAHiddenOverlayStopsBlocking();
    testAFieldUnderAnOverlayCannotBeFocused();
}

TEST_MAIN("test_uiinput", 41)
