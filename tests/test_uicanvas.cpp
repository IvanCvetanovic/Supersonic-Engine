// Regression tests for HUD layout.
//
// A HUD is authored at one window size and has to survive every other one.
// Anchor arithmetic is the part that goes wrong at resolutions nobody tested
// on - the score that sits neatly in the corner at 1280x720 and drifts into
// the middle of a 4K screen - so it lives apart from the renderer and is
// checked here rather than by eye.

#include "TestHarness.hpp"
#include "core/UICanvas.hpp"

#include <string>

using namespace Supersonic;

namespace {

// A 1920x1080 screen starting at the origin, which is the packaged-game case.
UIRect fullHd() {
    return { glm::vec2(0.0f, 0.0f), glm::vec2(1920.0f, 1080.0f) };
}

// The editor case: the viewport panel is an inset rectangle, not the window.
UIRect panel() {
    return { glm::vec2(300.0f, 100.0f), glm::vec2(1300.0f, 700.0f) };
}

} // namespace

static void testScaleIsOneAtTheReferenceHeight() {
    CHECK_NEAR(UICanvas::ScaleFor(glm::vec2(1920.0f, 1080.0f)), 1.0f);
    CHECK_NEAR(UICanvas::ScaleFor(glm::vec2(3840.0f, 2160.0f)), 2.0f);
    CHECK_NEAR(UICanvas::ScaleFor(glm::vec2(1280.0f, 720.0f)), 720.0f / 1080.0f);
}

static void testScaleSurvivesADegenerateScreen() {
    // A window minimised to zero height must not produce a scale of infinity
    // and place every element at NaN.
    CHECK_NEAR(UICanvas::ScaleFor(glm::vec2(0.0f, 0.0f)), 1.0f);
    CHECK_NEAR(UICanvas::ScaleFor(glm::vec2(100.0f, -5.0f)), 1.0f);
}

static void testTopLeftOffsetsInwardFromTheCorner() {
    const UIRect r = UICanvas::Place(UIAnchor::TopLeft, glm::vec2(24.0f, 16.0f),
                                     glm::vec2(200.0f, 40.0f), fullHd());
    CHECK_NEAR(r.min.x, 24.0f);
    CHECK_NEAR(r.min.y, 16.0f);
    CHECK_NEAR(r.max.x, 224.0f);
    CHECK_NEAR(r.max.y, 56.0f);
}

static void testBottomRightOffsetsInwardToo() {
    // The same offset must mean "in from the corner" at every anchor. Adding it
    // blindly pushes a bottom-right element off the bottom-right of the screen,
    // which is the classic anchor bug and is invisible until someone plays at
    // a different resolution.
    const UIRect r = UICanvas::Place(UIAnchor::BottomRight, glm::vec2(24.0f, 16.0f),
                                     glm::vec2(200.0f, 40.0f), fullHd());
    CHECK_NEAR(r.max.x, 1920.0f - 24.0f);
    CHECK_NEAR(r.max.y, 1080.0f - 16.0f);
    CHECK_NEAR(r.min.x, 1920.0f - 24.0f - 200.0f);
    CHECK_NEAR(r.min.y, 1080.0f - 16.0f - 40.0f);
}

static void testTopRightIsInwardHorizontallyAndDownwardVertically() {
    const UIRect r = UICanvas::Place(UIAnchor::TopRight, glm::vec2(30.0f, 20.0f),
                                     glm::vec2(100.0f, 50.0f), fullHd());
    CHECK_NEAR(r.max.x, 1920.0f - 30.0f);
    CHECK_NEAR(r.min.y, 20.0f);
}

static void testCentreIsCentredOnItsOwnMiddle() {
    // A centred element must be centred on the element, not have its top-left
    // corner at the middle of the screen.
    const UIRect r = UICanvas::Place(UIAnchor::Center, glm::vec2(0.0f, 0.0f),
                                     glm::vec2(200.0f, 100.0f), fullHd());
    CHECK_NEAR(r.min.x, 960.0f - 100.0f);
    CHECK_NEAR(r.min.y, 540.0f - 50.0f);
    CHECK_NEAR(r.max.x, 960.0f + 100.0f);
    CHECK_NEAR(r.max.y, 540.0f + 50.0f);
}

static void testCentreStaysCentredAtEveryScreenSize() {
    // A crosshair is the case that matters: it has to be on the middle pixel
    // whatever the window is.
    for (const glm::vec2 size : { glm::vec2(1280.0f, 720.0f),
                                  glm::vec2(1920.0f, 1080.0f),
                                  glm::vec2(3840.0f, 2160.0f),
                                  glm::vec2(800.0f, 1200.0f) }) {
        const UIRect screen{ glm::vec2(0.0f), size };
        const UIRect r = UICanvas::Place(UIAnchor::Center, glm::vec2(0.0f),
                                         glm::vec2(20.0f, 20.0f), screen);
        const glm::vec2 middle = (r.min + r.max) * 0.5f;
        CHECK_MSG(test::nearly(middle.x, size.x * 0.5f, 0.01f),
                  "centre drifted horizontally at " + std::to_string(size.x));
        CHECK_MSG(test::nearly(middle.y, size.y * 0.5f, 0.01f),
                  "centre drifted vertically at " + std::to_string(size.y));
    }
}

static void testTopCentreCentresHorizontallyAndHangsFromTheTop() {
    const UIRect r = UICanvas::Place(UIAnchor::TopCenter, glm::vec2(0.0f, 40.0f),
                                     glm::vec2(400.0f, 60.0f), fullHd());
    CHECK_NEAR((r.min.x + r.max.x) * 0.5f, 960.0f);
    CHECK_NEAR(r.min.y, 40.0f);
}

static void testBottomCentreHangsFromTheBottom() {
    const UIRect r = UICanvas::Place(UIAnchor::BottomCenter, glm::vec2(0.0f, 40.0f),
                                     glm::vec2(400.0f, 60.0f), fullHd());
    CHECK_NEAR((r.min.x + r.max.x) * 0.5f, 960.0f);
    CHECK_NEAR(r.max.y, 1080.0f - 40.0f);
}

static void testLayoutIsRelativeToTheGivenRectNotTheWindow() {
    // In the editor the game occupies a panel, not the window. A HUD that
    // anchored to the window would sit outside the viewport it belongs to, and
    // would move when a panel was resized.
    const UIRect screen = panel();
    const UIRect r = UICanvas::Place(UIAnchor::TopLeft, glm::vec2(10.0f, 10.0f),
                                     glm::vec2(50.0f, 50.0f), screen);
    CHECK_NEAR(r.min.x, 310.0f);
    CHECK_NEAR(r.min.y, 110.0f);

    const UIRect corner = UICanvas::Place(UIAnchor::BottomRight, glm::vec2(10.0f, 10.0f),
                                          glm::vec2(50.0f, 50.0f), screen);
    CHECK_NEAR(corner.max.x, 1290.0f);
    CHECK_NEAR(corner.max.y, 690.0f);
}

static void testEveryAnchorStaysInsideTheScreen() {
    // With a sane offset and a small element, nothing should land outside the
    // rectangle it is anchored to - at any anchor.
    const UIRect screen = fullHd();
    for (uint8_t i = 0; i <= static_cast<uint8_t>(UIAnchor::BottomRight); ++i) {
        const auto anchor = static_cast<UIAnchor>(i);
        const UIRect r = UICanvas::Place(anchor, glm::vec2(20.0f, 20.0f),
                                         glm::vec2(100.0f, 40.0f), screen);
        const std::string which = "anchor " + std::to_string(i);
        CHECK_MSG(r.min.x >= screen.min.x - 0.01f, which + " ran off the left");
        CHECK_MSG(r.min.y >= screen.min.y - 0.01f, which + " ran off the top");
        CHECK_MSG(r.max.x <= screen.max.x + 0.01f, which + " ran off the right");
        CHECK_MSG(r.max.y <= screen.max.y + 0.01f, which + " ran off the bottom");
    }
}

static void testFillTakesTheLeftPortion() {
    const UIRect bar{ glm::vec2(100.0f, 50.0f), glm::vec2(300.0f, 70.0f) };

    const UIRect half = UICanvas::FillHorizontal(bar, 0.5f);
    CHECK_NEAR(half.min.x, 100.0f);
    CHECK_NEAR(half.max.x, 200.0f);
    CHECK_MSG(test::nearly(half.max.y, 70.0f), "a bar must not change height as it empties");

    const UIRect full = UICanvas::FillHorizontal(bar, 1.0f);
    CHECK_NEAR(full.max.x, 300.0f);
}

static void testFillClampsOutOfRangeValues() {
    // Health goes negative in the same frame the death handler runs, and a
    // negative width draws a rectangle that extends to the left of the bar.
    const UIRect bar{ glm::vec2(100.0f, 50.0f), glm::vec2(300.0f, 70.0f) };

    const UIRect empty = UICanvas::FillHorizontal(bar, -3.0f);
    CHECK_NEAR(empty.max.x, 100.0f);
    CHECK_MSG(empty.max.x >= empty.min.x, "an empty bar must not have negative width");

    const UIRect over = UICanvas::FillHorizontal(bar, 4.0f);
    CHECK_MSG(test::nearly(over.max.x, 300.0f),
              "an overfull bar must stop at its own right edge, not run past it");
}

// --- interaction ------------------------------------------------------------
//
// Whether a press counts as a click is the whole of a button, and every way of
// getting it wrong still produces something that looks like it works: a menu
// that fires whatever you happen to release over, a button that arms itself
// when you drag a held pointer across it, one that cannot be cancelled once
// pressed. None of that shows up in a screenshot.

namespace {

// The rectangle used by every interaction test.
UIRect testButton() {
    return { glm::vec2(100.0f, 100.0f), glm::vec2(300.0f, 160.0f) };
}

const glm::vec2 kInside(200.0f, 130.0f);
const glm::vec2 kOutside(600.0f, 400.0f);

// One frame of pointer input. `wasDown` is threaded by the caller, exactly as
// UISystem threads it from the previous frame.
UICanvas::UIPointer pointerAt(const glm::vec2& position, bool down, bool wasDown,
                              bool active = true) {
    UICanvas::UIPointer pointer;
    pointer.position = position;
    pointer.down = down;
    pointer.wasDown = wasDown;
    pointer.active = active;
    return pointer;
}

} // namespace

static void testHoverWithoutPressing() {
    const UICanvas::UIButtonState state =
        UICanvas::UpdateButton({}, testButton(), pointerAt(kInside, false, false));

    CHECK(state.hovered);
    CHECK(!state.pressed);
    CHECK(!state.clicked);
}

static void testPointerOutsideIsNotHovered() {
    const UICanvas::UIButtonState state =
        UICanvas::UpdateButton({}, testButton(), pointerAt(kOutside, false, false));
    CHECK(!state.hovered);
}

static void testPressAndReleaseInsideClicksOnce() {
    const UIRect rect = testButton();

    const auto pressed = UICanvas::UpdateButton({}, rect, pointerAt(kInside, true, false));
    CHECK_MSG(pressed.pressed, "pressing on the button must arm it");
    CHECK_MSG(!pressed.clicked, "the click belongs to the release, not the press");

    const auto held = UICanvas::UpdateButton(pressed, rect, pointerAt(kInside, true, true));
    CHECK(held.pressed);
    CHECK(!held.clicked);

    const auto released = UICanvas::UpdateButton(held, rect, pointerAt(kInside, false, true));
    CHECK_MSG(released.clicked, "releasing over the button must click it");
    CHECK(!released.pressed);

    // Exactly one frame. A click that stays true is a menu button that fires
    // every frame the player leaves the pointer where it is.
    const auto after = UICanvas::UpdateButton(released, rect, pointerAt(kInside, false, false));
    CHECK_MSG(!after.clicked, "a click must last exactly one frame");
}

static void testReleasingAwayFromTheButtonDoesNotClick() {
    const UIRect rect = testButton();

    const auto pressed = UICanvas::UpdateButton({}, rect, pointerAt(kInside, true, false));
    const auto draggedOff = UICanvas::UpdateButton(pressed, rect, pointerAt(kOutside, true, true));

    CHECK_MSG(draggedOff.pressed,
              "dragging off a held button keeps the press, so sliding back on works");
    CHECK(!draggedOff.hovered);

    const auto released = UICanvas::UpdateButton(draggedOff, rect, pointerAt(kOutside, false, true));
    CHECK_MSG(!released.clicked,
              "letting go away from the button is how a player cancels; it must not click");
}

static void testDraggingBackOnStillClicks() {
    const UIRect rect = testButton();

    const auto pressed = UICanvas::UpdateButton({}, rect, pointerAt(kInside, true, false));
    const auto off = UICanvas::UpdateButton(pressed, rect, pointerAt(kOutside, true, true));
    const auto back = UICanvas::UpdateButton(off, rect, pointerAt(kInside, true, true));
    const auto released = UICanvas::UpdateButton(back, rect, pointerAt(kInside, false, true));

    CHECK_MSG(released.clicked,
              "sliding off a button and back on before releasing must still click it");
}

static void testDraggingOntoAButtonWhileHeldDoesNotPressIt() {
    // The pointer went down somewhere else entirely. Without a press edge this
    // button would arm itself as the pointer crossed it, so dragging across a
    // menu would trigger every button on the way.
    const UIRect rect = testButton();

    const auto crossing = UICanvas::UpdateButton({}, rect, pointerAt(kInside, true, true));
    CHECK(crossing.hovered);
    CHECK_MSG(!crossing.pressed, "a press that began elsewhere must not arm this button");

    const auto released = UICanvas::UpdateButton(crossing, rect, pointerAt(kInside, false, true));
    CHECK_MSG(!released.clicked, "and it must not click when the pointer is let go over it");
}

static void testAnInactivePointerDoesNothing() {
    // The editor has a panel over the viewport, or the game has released the
    // mouse. Everything stops, including a press already in flight.
    const UIRect rect = testButton();

    const auto pressed = UICanvas::UpdateButton({}, rect, pointerAt(kInside, true, false));
    CHECK(pressed.pressed);

    const auto blocked =
        UICanvas::UpdateButton(pressed, rect, pointerAt(kInside, true, true, /*active=*/false));
    CHECK_MSG(!blocked.hovered && !blocked.pressed && !blocked.clicked,
              "an inactive pointer must not hover, press or click");

    const auto released =
        UICanvas::UpdateButton(blocked, rect, pointerAt(kInside, false, true));
    CHECK_MSG(!released.clicked,
              "a press interrupted by something taking the pointer must not fire later");
}

static void testContainsIncludesTheEdges() {
    const UIRect rect = testButton();

    CHECK(UICanvas::Contains(rect, rect.min));
    CHECK(UICanvas::Contains(rect, rect.max));
    CHECK(UICanvas::Contains(rect, glm::vec2(rect.min.x, rect.max.y)));
    CHECK(!UICanvas::Contains(rect, glm::vec2(rect.min.x - 0.5f, rect.min.y)));
    CHECK(!UICanvas::Contains(rect, glm::vec2(rect.max.x + 0.5f, rect.max.y)));
}

static void runTests() {
    testScaleIsOneAtTheReferenceHeight();
    testScaleSurvivesADegenerateScreen();
    testTopLeftOffsetsInwardFromTheCorner();
    testBottomRightOffsetsInwardToo();
    testTopRightIsInwardHorizontallyAndDownwardVertically();
    testCentreIsCentredOnItsOwnMiddle();
    testCentreStaysCentredAtEveryScreenSize();
    testTopCentreCentresHorizontallyAndHangsFromTheTop();
    testBottomCentreHangsFromTheBottom();
    testLayoutIsRelativeToTheGivenRectNotTheWindow();
    testEveryAnchorStaysInsideTheScreen();
    testFillTakesTheLeftPortion();
    testFillClampsOutOfRangeValues();

    testContainsIncludesTheEdges();
    testHoverWithoutPressing();
    testPointerOutsideIsNotHovered();
    testPressAndReleaseInsideClicksOnce();
    testReleasingAwayFromTheButtonDoesNotClick();
    testDraggingBackOnStillClicks();
    testDraggingOntoAButtonWhileHeldDoesNotPressIt();
    testAnInactivePointerDoesNothing();
}

TEST_MAIN("test_uicanvas", 72)
