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
#include "core/UISystem.hpp"

#include <string>

using namespace Supersonic;


// No containers in these cases: every element here places itself from its own
// anchor, which is the path this suite is about. Stack layout has its own tests
// in test_uicanvas.
static const UICanvas::StackedLayout kNoStacks;

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

static void testAClickIsTheReleaseAndOnlyTheReleaseFrame() {
    // What the deleted return value tried to say, asserted on the component
    // instead. This is also the fact that killed it: a click exists on the
    // RELEASE frame, and any guard asking "did the UI take this click?" runs on
    // the PRESS - so the number it read was zero every time it mattered.
    entt::registry registry;
    const auto play = addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const auto& button = registry.get<UIButtonComponent>(play);

    UIInput::Update(registry, screen(), pointerAt(glm::vec2(140.0f, 70.0f), true, false),
                    noKeyboard(), kNoStacks);
    CHECK_MSG(!button.clicked, "the press is not the click");

    UIInput::Update(registry, screen(), pointerAt(glm::vec2(140.0f, 70.0f), false, true),
                    noKeyboard(), kNoStacks);
    CHECK_MSG(button.clicked, "the release over the button is");

    UIInput::Update(registry, screen(), pointerAt(glm::vec2(140.0f, 70.0f), false, false),
                    noKeyboard(), kNoStacks);
    CHECK_MSG(!button.clicked, "and it is not reported again the frame after");
}

static void testAPanelSwallowsAClickAndNoButtonReportsOne() {
    // The other half of why a count of clicked BUTTONS could never have been the
    // guard. A full-screen backdrop over a menu is supported on purpose, so that
    // clicks meant for the game behind it are swallowed - and a backdrop is a
    // panel, not a button. The UI took the click and the count would have read
    // zero, in exactly the case the guard existed for.
    entt::registry registry;
    const auto play = addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    const auto backdrop = registry.create();
    auto& panel = registry.emplace<UIPanelComponent>(backdrop);
    panel.anchor = UIAnchor::TopLeft;
    panel.offset = glm::vec2(0.0f);
    panel.size = glm::vec2(4000.0f, 4000.0f);
    registry.emplace<UIOrderComponent>(backdrop).order = 1;   // over the button

    UIInput::Update(registry, screen(), pointerAt(glm::vec2(140.0f, 70.0f), true, false),
                    noKeyboard(), kNoStacks);
    UIInput::Update(registry, screen(), pointerAt(glm::vec2(140.0f, 70.0f), false, true),
                    noKeyboard(), kNoStacks);

    const auto& button = registry.get<UIButtonComponent>(play);
    CHECK_MSG(!button.clicked, "the panel took it, so the button did not");
    CHECK_MSG(!button.hovered, "and the button is not even hovered through it");
}

static void testClickingEmptySpaceReportsNothing() {
    entt::registry registry;
    const auto play = addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    UIInput::Update(registry, screen(), pointerAt(glm::vec2(900.0f, 900.0f), true, false), noKeyboard(), kNoStacks);
    UIInput::Update(registry, screen(), pointerAt(glm::vec2(900.0f, 900.0f), false, true),
                    noKeyboard(), kNoStacks);
    CHECK_MSG(!registry.get<UIButtonComponent>(play).clicked,
              "clicking empty space must click nothing");
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
    UIInput::Update(registry, none, pointerAt(glm::vec2(0.0f), true, false), noKeyboard(), kNoStacks);
    CHECK_MSG(!registry.get<UIButtonComponent>(button).clicked,
              "a zero-size game area has nothing to click");
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

// --- a click belongs to one tick, not to one frame ------------------------

namespace {

// The two frames that make a click: press over the button, then release.
void clickOnce(entt::registry& registry, const glm::vec2& at) {
    UIInput::Update(registry, screen(), pointerAt(at, true, false), noKeyboard(), kNoStacks);
    UIInput::Update(registry, screen(), pointerAt(at, false, true), noKeyboard(), kNoStacks);
}

// A frame where nothing happens, which is most of them.
void idleFrame(entt::registry& registry, const glm::vec2& at) {
    UIInput::Update(registry, screen(), pointerAt(at, false, false), noKeyboard(), kNoStacks);
}

bool tickSawClick(entt::registry& registry) {
    UIInput::BeginTickClicks(registry);
    for (auto [entity, button] : registry.view<UIButtonComponent>().each()) {
        if (button.clickedThisTick) return true;
    }
    return false;
}

} // namespace

static void testAClickSurvivesTheFramesBeforeTheNextTick() {
    // `clicked` is true for exactly one FRAME and a script runs on the TICK.
    // A game simulating at 20 Hz on a 144 Hz display runs no tick at all on six
    // frames out of seven, so a click landing on one of those was gone before
    // anything could act on it - a menu button that does nothing, sometimes,
    // on fast machines.
    entt::registry registry;
    addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const glm::vec2 on(140.0f, 70.0f);

    clickOnce(registry, on);

    // Several frames pass with no tick in them.
    idleFrame(registry, on);
    idleFrame(registry, on);

    CHECK_MSG(tickSawClick(registry), "the click waited for a tick to ask for it");
}

static void testOnlyOneTickSeesAGivenClick() {
    // The other direction, and the more expensive one: a frame running late
    // runs several ticks, and a click reported to all of them makes one press
    // of Buy buy three times.
    entt::registry registry;
    addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const glm::vec2 on(140.0f, 70.0f);

    clickOnce(registry, on);

    CHECK_MSG(tickSawClick(registry), "the first tick of the frame sees it");
    CHECK_MSG(!tickSawClick(registry), "and the second does not");
    CHECK_MSG(!tickSawClick(registry), "and neither does the third");
}

static void testATickNeverSeesAClickNobodyMade() {
    entt::registry registry;
    addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const glm::vec2 on(140.0f, 70.0f);

    idleFrame(registry, on);
    CHECK_MSG(!tickSawClick(registry), "nothing was clicked, nothing is reported");

    // Holding the button down is not clicking it again, which the frame-scoped
    // flag already got right and the latch must not undo.
    UIInput::Update(registry, screen(), pointerAt(on, true, false), noKeyboard(), kNoStacks);
    CHECK_MSG(!tickSawClick(registry), "a press that has not been released is not a click");
}

static void testClicksNoTickIsComingForAreDropped() {
    // Pressing buttons in the editor, or on a pause menu that stops the
    // simulation, and then hitting Play. Without this the latch holds every one
    // of them and the first tick after Play does all their actions at once.
    entt::registry registry;
    addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const glm::vec2 on(140.0f, 70.0f);

    clickOnce(registry, on);
    UIInput::DiscardPendingClicks(registry);

    CHECK_MSG(!tickSawClick(registry), "a click made while nothing ticked does not arrive later");

    // And the latch still works afterwards.
    clickOnce(registry, on);
    CHECK_MSG(tickSawClick(registry), "a click made while ticking still arrives");
}

static void testDiscardingLeavesTheRunningTicksOwnClickAlone() {
    // The pending flag and the one a tick is currently reading are different
    // things. Dropping the second would take a click away from a tick that had
    // already been given it, half way through its own update.
    entt::registry registry;
    addButton(registry, "Play", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    const glm::vec2 on(140.0f, 70.0f);

    clickOnce(registry, on);
    UIInput::BeginTickClicks(registry);

    UIInput::DiscardPendingClicks(registry);

    bool stillHeld = false;
    for (auto [entity, button] : registry.view<UIButtonComponent>().each()) {
        if (button.clickedThisTick) stillHeld = true;
    }
    CHECK_MSG(stillHeld, "the tick that owns the click keeps it");
}

// HIDING A CONTAINER MUST HIDE WHAT IS INSIDE IT.
//
// This is the worse half of the same rule, and it does not leave a hole - it
// piles the whole menu on one point.
//
// The layout pass skips a hidden stack before it measures anything, so none of
// its children get a rectangle. Both the draw pass and this one then fall back
// to "place the element from its own anchor" - and UIButtonComponent::anchor
// defaults to Center with a zero offset. So every button in a hidden column
// lands on the middle of the screen, on top of one another, and the topmost of
// them is clickable over whatever is really there.
//
// Wolf Brigade hides a container to open a modal in three places (main_menu's
// confirm box, the pause menu, the game-over overlay), so the first modal in
// the game would have put four invisible buttons across the middle of a live
// match.
static void testAButtonInsideAHiddenStackIsNotClickable() {
    entt::registry registry;

    const entt::entity column = registry.create();
    auto& stack = registry.emplace<UIStackComponent>(column);
    stack.anchor = UIAnchor::Center;
    stack.visible = false;

    const entt::entity hidden = registry.create();
    auto& button = registry.emplace<UIButtonComponent>(hidden);
    button.label = "Quit";
    button.size = glm::vec2(440.0f, 104.0f);
    registry.emplace<HierarchyComponent>(hidden).parent = column;

    // The layout pass is given the same registry the draw pass would be, so
    // the fallback under test is the real one rather than kNoStacks.
    const UICanvas::StackedLayout placed =
        UISystem::LayoutStacks(registry, screen(), nullptr, 1.0f);

    // Dead centre of the screen, which is exactly where the fallback anchor
    // would put it.
    const glm::vec2 middle(960.0f, 540.0f);
    UIInput::Update(registry, screen(), pointerAt(middle, false, false), noKeyboard(), placed);
    UIInput::Update(registry, screen(), pointerAt(middle, true, false), noKeyboard(), placed);
    UIInput::Update(registry, screen(), pointerAt(middle, false, true), noKeyboard(), placed);

    CHECK_MSG(!registry.get<UIButtonComponent>(hidden).clicked,
              "a button inside a hidden container must not be clickable, and must "
              "not fall back to its own anchor in the middle of the screen");
    CHECK_MSG(!registry.get<UIButtonComponent>(hidden).hovered, "nor hovered");
}

// The control that gives the case above its meaning: the SAME column, shown.
// Without this, hiding everything unconditionally would also pass.
static void testAButtonInsideAShownStackStillIs() {
    entt::registry registry;

    const entt::entity column = registry.create();
    auto& stack = registry.emplace<UIStackComponent>(column);
    stack.anchor = UIAnchor::Center;
    stack.visible = true;

    const entt::entity shown = registry.create();
    auto& button = registry.emplace<UIButtonComponent>(shown);
    button.label = "Quit";
    button.size = glm::vec2(440.0f, 104.0f);
    registry.emplace<HierarchyComponent>(shown).parent = column;

    const UICanvas::StackedLayout placed =
        UISystem::LayoutStacks(registry, screen(), nullptr, 1.0f);

    const glm::vec2 middle(960.0f, 540.0f);
    UIInput::Update(registry, screen(), pointerAt(middle, false, false), noKeyboard(), placed);
    UIInput::Update(registry, screen(), pointerAt(middle, true, false), noKeyboard(), placed);
    UIInput::Update(registry, screen(), pointerAt(middle, false, true), noKeyboard(), placed);

    CHECK_MSG(registry.get<UIButtonComponent>(shown).clicked,
              "the same button in a visible column is still clickable, or the fix "
              "above is just switching the UI off");
}

// A BUTTON REBUILT MID-GESTURE NEVER FIRES, AND THAT DECIDES HOW A MENU IS
// WRITTEN.
//
// `pressed` lives on the component, and a click is the transition from pressed
// to released: UpdateButton reads the PREVIOUS frame's flag off the same
// component to decide the release meant something. So the state that makes a
// click a click is stored on the entity, between frames.
//
// The engine's other view code pools its entities for unrelated reasons - the
// lane's quads are reused rather than recreated so the state hash does not move
// for reasons about drawing - and a HUD written the obvious way, rebuilt from
// the simulation every tick, would be destroying and recreating its buttons
// under the player's finger. The press lands on one component and the release
// on a fresh one, which has never been pressed, so nothing fires. No button in
// the game would ever work, and it would look like the click was going to the
// wrong place rather than like the button was a different button.
//
// Pinned as a characterisation test, not reported as a bug: the alternative is
// click state living outside the entity, keyed on something that survives the
// rebuild, and there is nothing to key it on. The constraint on the caller is
// the cheaper half - rebuild a screen when it CHANGES, not every tick - and
// this is where that constraint is written down.
static void testAButtonRecreatedMidGestureDoesNotFire() {
    entt::registry registry;
    const glm::vec2 at(140.0f, 70.0f);

    const auto first = addButton(registry, "Buy", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    UIInput::Update(registry, screen(), pointerAt(at, false, false), noKeyboard(), kNoStacks);
    UIInput::Update(registry, screen(), pointerAt(at, true, false), noKeyboard(), kNoStacks);
    CHECK_MSG(registry.get<UIButtonComponent>(first).pressed,
              "the press landed, so the gesture really is in flight");

    // The rebuild: same label, same place, new entity.
    registry.destroy(first);
    const auto rebuilt = addButton(registry, "Buy", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));

    UIInput::Update(registry, screen(), pointerAt(at, false, true), noKeyboard(), kNoStacks);

    CHECK_MSG(!registry.get<UIButtonComponent>(rebuilt).clicked,
              "the release fell on a component that was never pressed, so the click "
              "is lost - a screen rebuilt every tick has no working buttons");
    CHECK_MSG(!registry.get<UIButtonComponent>(rebuilt).clickPending,
              "and nothing is latched for a later tick to find either");
}

// The control: the SAME gesture, on an entity that survives it.
static void testAButtonThatSurvivesTheGestureDoesFire() {
    entt::registry registry;
    const glm::vec2 at(140.0f, 70.0f);

    const auto button = addButton(registry, "Buy", UIAnchor::TopLeft, glm::vec2(40.0f, 40.0f));
    UIInput::Update(registry, screen(), pointerAt(at, false, false), noKeyboard(), kNoStacks);
    UIInput::Update(registry, screen(), pointerAt(at, true, false), noKeyboard(), kNoStacks);
    UIInput::Update(registry, screen(), pointerAt(at, false, true), noKeyboard(), kNoStacks);

    CHECK_MSG(registry.get<UIButtonComponent>(button).clicked,
              "the identical gesture fires when the button is not rebuilt under it, "
              "which is what makes the case above about the rebuild");
}

static void runTests() {
    testAButtonRecreatedMidGestureDoesNotFire();
    testAButtonThatSurvivesTheGestureDoesFire();
    testAButtonInsideAHiddenStackIsNotClickable();
    testAButtonInsideAShownStackStillIs();
    testAClickSurvivesTheFramesBeforeTheNextTick();
    testOnlyOneTickSeesAGivenClick();
    testATickNeverSeesAClickNobodyMade();
    testClicksNoTickIsComingForAreDropped();
    testDiscardingLeavesTheRunningTicksOwnClickAlone();
    testAClickReachesTheButtonUnderThePointer();
    testOnlyTheButtonUnderThePointerIsClicked();
    testClickingBetweenTwoButtonsHitsNeither();
    testAClickIsTheReleaseAndOnlyTheReleaseFrame();
    testAPanelSwallowsAClickAndNoButtonReportsOne();
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

TEST_MAIN("test_uiinput", 68)
