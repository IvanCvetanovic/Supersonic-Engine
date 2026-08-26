// Regression tests for HUD layout.
//
// A HUD is authored at one window size and has to survive every other one.
// Anchor arithmetic is the part that goes wrong at resolutions nobody tested
// on - the score that sits neatly in the corner at 1280x720 and drifts into
// the middle of a 4K screen - so it lives apart from the renderer and is
// checked here rather than by eye.

#include "TestHarness.hpp"
#include "core/Components.hpp"
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

// --- typing -----------------------------------------------------------------
//
// A name box looks like the simplest widget there is and is the one place in
// the UI where UTF-8 has to be got right: every edit is a splice into a
// std::string, and a splice made in the middle of a character produces a value
// the font will not draw and the script ABI must not hand to a plugin.

namespace {

UICanvas::UIKeyboard typed(const unsigned int* characters, int count) {
    UICanvas::UIKeyboard keyboard;
    keyboard.characters = characters;
    keyboard.characterCount = count;
    return keyboard;
}

// Applies one frame and returns the new state, so a case reads as a sequence of
// keystrokes rather than as bookkeeping.
UICanvas::UITextEditState apply(UICanvas::UITextEditState state, std::string& value,
                                int maxLength, const UICanvas::UIKeyboard& keyboard) {
    return UICanvas::EditText(state, value, maxLength, keyboard);
}

} // namespace

static void testTypingInsertsAtTheCaret() {
    std::string value = "Ivan";
    UICanvas::UITextEditState state{};
    state.caret = 2;

    const unsigned int characters[] = { 'X' };
    state = apply(state, value, 0, typed(characters, 1));

    CHECK_MSG(value == "IvXan", value);
    CHECK_EQ(state.caret, 3);
}

static void testBackspaceRemovesAWholeCharacterNotAByte() {
    // The failure this exists for: erasing one byte of a two-byte letter leaves
    // a lone continuation byte, which is not valid UTF-8. The box then draws a
    // replacement glyph nobody typed, and getText hands the plugin a broken
    // string with no way to know.
    std::string value = "caf\xC3\xA9";        // "café", six bytes, four characters
    CHECK_EQ(value.size(), size_t{5});

    UICanvas::UITextEditState state{};
    state.caret = static_cast<int>(value.size());

    UICanvas::UIKeyboard keyboard;
    keyboard.backspace = true;
    state = apply(state, value, 0, keyboard);

    CHECK_MSG(value == "caf", value);
    CHECK_MSG(state.caret == 3, "and the caret follows it, still on a boundary");
}

static void testMaxLengthCountsCharactersNotBytes() {
    // A field authored to hold three that took three plain letters but only one
    // accented one would be a bug report, not a design: the author counted what
    // they can see.
    std::string value;
    UICanvas::UITextEditState state{};

    const unsigned int accented[] = { 0x00E9u, 0x00E9u, 0x00E9u, 0x00E9u };  // é é é é
    state = apply(state, value, 3, typed(accented, 4));

    CHECK_MSG(value == "\xC3\xA9\xC3\xA9\xC3\xA9",
              "three characters, even though they are six bytes");
    CHECK_MSG(value.size() == size_t{6}, "and the fourth was refused, not truncated");
    CHECK_EQ(state.caret, 6);
}

static void testTheCaretMovesByCharacters() {
    std::string value = "\xC3\xA9""x";        // "éx": two characters, three bytes
    UICanvas::UITextEditState state{};
    state.caret = 0;

    UICanvas::UIKeyboard right;
    right.caretRight = true;
    state = apply(state, value, 0, right);
    CHECK_MSG(state.caret == 2, "one arrow press steps over the WHOLE two-byte letter");

    UICanvas::UIKeyboard left;
    left.caretLeft = true;
    state = apply(state, value, 0, left);
    CHECK_EQ(state.caret, 0);

    UICanvas::UIKeyboard end;
    end.caretEnd = true;
    state = apply(state, value, 0, end);
    CHECK_EQ(state.caret, 3);
}

static void testControlCodesAreNotCharacters() {
    // GLFW's character callback does not deliver these, but the ABI's setText
    // and a future paste path can, and a tab or a newline inside a one-line box
    // draws as a hole.
    std::string value;
    UICanvas::UITextEditState state{};

    const unsigned int characters[] = { '\n', '\t', 0x7Fu, 0xD800u, 0x110000u, 'A' };
    state = apply(state, value, 0, typed(characters, 6));

    CHECK_MSG(value == "A", "only the one that is a character survives");
}

static void testAnInactiveKeyboardChangesNothing() {
    // Something else has the keyboard - an ImGui box in the inspector. The
    // field keeps its focus and its caret and simply hears nothing, because
    // losing what was typed so far would be worse than not accepting more.
    std::string value = "Iva";
    UICanvas::UITextEditState state{};
    state.caret = 3;

    const unsigned int characters[] = { 'n' };
    UICanvas::UIKeyboard keyboard = typed(characters, 1);
    keyboard.backspace = true;
    keyboard.submit = true;
    keyboard.active = false;

    state = apply(state, value, 0, keyboard);
    CHECK_MSG(value == "Iva", "nothing typed and nothing erased");
    CHECK_EQ(state.caret, 3);
    CHECK_MSG(!state.submitted, "and no submit, or a menu would answer itself");
}

static void testEnterAndEscapeAreOneFrameFlags() {
    std::string value = "Ivan";
    UICanvas::UITextEditState state{};
    state.caret = 4;

    UICanvas::UIKeyboard enter;
    enter.submit = true;
    state = apply(state, value, 0, enter);
    CHECK_MSG(state.submitted, "Enter reports a submit");
    CHECK_MSG(value == "Ivan", "and does not touch the value");

    // Carried into a frame where nothing was pressed, a submit would be
    // answered again every frame until the next keystroke.
    state = apply(state, value, 0, UICanvas::UIKeyboard{});
    CHECK_MSG(!state.submitted, "exactly one frame, like a button's click");

    UICanvas::UIKeyboard escape;
    escape.cancel = true;
    state = apply(state, value, 0, escape);
    CHECK_MSG(state.cancelled, "Escape reports a cancel");
    CHECK_MSG(value == "Ivan",
              "and leaves the text alone - a mistyped key must not be unrecoverable");
}

// --- projecting a world point onto the screen -----------------------------
//
// World-space labels - a name plate over a unit, a floating damage number -
// had no expression at all, and three of a Wolf Brigade unit's five drawables
// are world-space UI. The arithmetic is small and every part of it has a sign
// that can be wrong in a way that still produces a plausible picture, so it is
// checked against a camera whose answers are known by construction.

static CameraComponent projectionCamera(bool orthographic) {
    CameraComponent cam;
    cam.aspect = 1.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 100.0f;
    cam.position = glm::vec3(0.0f, 0.0f, 5.0f);
    cam.yaw = -90.0f;
    cam.pitch = 0.0f;
    if (orthographic) {
        cam.projection = CameraComponent::Projection::Orthographic;
        cam.orthoHeight = 10.0f;
    }
    cam.updateCameraVectors();
    return cam;
}

static glm::mat4 viewProjOf(const CameraComponent& cam) {
    return cam.getProjectionMatrix() * cam.getViewMatrix();
}

static void testTheCentreOfTheWorldLandsInTheCentreOfTheScreen() {
    const CameraComponent cam = projectionCamera(false);
    const UIRect screen{glm::vec2(0.0f, 0.0f), glm::vec2(800.0f, 600.0f)};

    glm::vec3 out(0.0f);
    CHECK_MSG(UICanvas::ProjectToScreen(viewProjOf(cam), glm::vec3(0.0f), screen, out),
              "a point straight ahead must project");
    CHECK_NEAR(out.x, 400.0f);
    CHECK_NEAR(out.y, 300.0f);
}

static void testUpInTheWorldIsUpOnTheScreen() {
    // The one that is easy to get backwards and impossible to unsee once it is.
    // Vulkan clip space has +Y DOWN and the projection already negates its Y
    // row for that, so ProjectToScreen must NOT flip again - which is the same
    // double-negation that mirrored picking about the horizontal centreline.
    const CameraComponent cam = projectionCamera(false);
    const UIRect screen{glm::vec2(0.0f, 0.0f), glm::vec2(800.0f, 600.0f)};

    glm::vec3 above(0.0f);
    glm::vec3 below(0.0f);
    CHECK(UICanvas::ProjectToScreen(viewProjOf(cam), glm::vec3(0.0f, 1.0f, 0.0f), screen, above));
    CHECK(UICanvas::ProjectToScreen(viewProjOf(cam), glm::vec3(0.0f, -1.0f, 0.0f), screen, below));

    CHECK_MSG(above.y < below.y,
              "a point higher in the world must land higher on the screen, meaning a SMALLER row");
    CHECK_NEAR(above.x, 400.0f);
}

static void testAPointBehindTheCameraIsRefused() {
    // It still HAS coordinates, and they are the mirrored ones in front. A
    // caller that ignores the result draws a name plate for a unit that is off
    // behind its shoulder.
    const CameraComponent cam = projectionCamera(false);
    const UIRect screen{glm::vec2(0.0f, 0.0f), glm::vec2(800.0f, 600.0f)};

    glm::vec3 out(0.0f);
    CHECK_MSG(!UICanvas::ProjectToScreen(viewProjOf(cam), glm::vec3(0.0f, 0.0f, 20.0f), screen, out),
              "a point behind the camera must be refused, not placed");

    // And the control: the same point in FRONT is accepted, so the refusal is
    // about direction rather than about the function never working.
    CHECK_MSG(UICanvas::ProjectToScreen(viewProjOf(cam), glm::vec3(0.0f, 0.0f, -20.0f), screen, out),
              "a point in front must still project");
}

static void testOrthographicProjectsTooAndSpansTheAuthoredHeight() {
    // Orthographic keeps w at 1, so the behind-the-camera test cannot be a
    // w check alone - it has to look at depth as well, or a label behind an
    // orthographic camera is placed happily.
    const CameraComponent cam = projectionCamera(true);
    const UIRect screen{glm::vec2(0.0f, 0.0f), glm::vec2(800.0f, 600.0f)};
    const glm::mat4 vp = viewProjOf(cam);

    glm::vec3 top(0.0f);
    glm::vec3 bottom(0.0f);
    CHECK(UICanvas::ProjectToScreen(vp, glm::vec3(0.0f, 5.0f, 0.0f), screen, top));
    CHECK(UICanvas::ProjectToScreen(vp, glm::vec3(0.0f, -5.0f, 0.0f), screen, bottom));

    // orthoHeight is 10, so plus and minus five are exactly the top and bottom
    // edges of an 800x600 screen.
    CHECK_NEAR(top.y, 0.0f);
    CHECK_NEAR(bottom.y, 600.0f);

    glm::vec3 behind(0.0f);
    CHECK_MSG(!UICanvas::ProjectToScreen(vp, glm::vec3(0.0f, 0.0f, 20.0f), screen, behind),
              "an orthographic camera must refuse what is behind it too, where w cannot say so");
}

static void testTheScreenRectIsAnOffsetNotAnAssumption() {
    // The HUD is drawn against the viewport panel in the editor, which does not
    // start at the window origin. A projection that assumed (0,0) would put
    // every label up and left by the dock width.
    const CameraComponent cam = projectionCamera(false);
    const UIRect offset{glm::vec2(100.0f, 50.0f), glm::vec2(900.0f, 650.0f)};

    glm::vec3 out(0.0f);
    CHECK(UICanvas::ProjectToScreen(viewProjOf(cam), glm::vec3(0.0f), offset, out));
    CHECK_NEAR(out.x, 500.0f);
    CHECK_NEAR(out.y, 350.0f);
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

    testTypingInsertsAtTheCaret();
    testBackspaceRemovesAWholeCharacterNotAByte();
    testMaxLengthCountsCharactersNotBytes();
    testTheCaretMovesByCharacters();
    testControlCodesAreNotCharacters();
    testAnInactiveKeyboardChangesNothing();
    testEnterAndEscapeAreOneFrameFlags();
    testTheCentreOfTheWorldLandsInTheCentreOfTheScreen();
    testUpInTheWorldIsUpOnTheScreen();
    testAPointBehindTheCameraIsRefused();
    testOrthographicProjectsTooAndSpansTheAuthoredHeight();
    testTheScreenRectIsAnOffsetNotAnAssumption();
}

TEST_MAIN("test_uicanvas", 88)
