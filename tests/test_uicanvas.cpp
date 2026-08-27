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
#include "core/UISystem.hpp"

#include <entt/entt.hpp>

#include <vector>

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

// --- stacking ---------------------------------------------------------------
//
// Every element placed itself from an anchor and an offset, so an author
// computed every position by hand and re-computed them all whenever anything
// changed size. Wolf Brigade uses six VBoxContainers, five HBoxContainers and
// four CenterContainers - fifteen of its sixteen container uses are a stack
// with an anchor.

// A screen exactly the reference height, so scale is 1 and authored units are
// pixels. Every expected number below is then arithmetic anyone can redo.
static UIRect unitScaleScreen() {
    return UIRect{glm::vec2(0.0f, 0.0f), glm::vec2(1920.0f, 1080.0f)};
}

static void testAVerticalStackAdvancesByHeightPlusSpacing() {
    const std::vector<glm::vec2> sizes{{100.0f, 20.0f}, {100.0f, 30.0f}, {100.0f, 40.0f}};
    const std::vector<UIRect> rects =
        UICanvas::LayoutStack(sizes, false, 10.0f, UIAnchor::TopLeft, glm::vec2(0.0f),
                              unitScaleScreen());

    CHECK_EQ(rects.size(), size_t{3});
    CHECK_NEAR(rects[0].min.y, 0.0f);
    CHECK_NEAR(rects[1].min.y, 30.0f);          // 20 + 10
    CHECK_NEAR(rects[2].min.y, 70.0f);          // 20 + 10 + 30 + 10
    CHECK_NEAR(rects[2].max.y, 110.0f);

    // Spacing goes BETWEEN, not after: three items have two gaps. A stack that
    // trails a gap is half a gap off centre and looks like nothing is wrong.
    CHECK_MSG(rects[2].max.y - rects[0].min.y == 110.0f,
              "three items and two gaps, not three gaps");
}

static void testAHorizontalStackAdvancesAlongX() {
    const std::vector<glm::vec2> sizes{{50.0f, 20.0f}, {70.0f, 20.0f}};
    const std::vector<UIRect> rects =
        UICanvas::LayoutStack(sizes, true, 8.0f, UIAnchor::TopLeft, glm::vec2(0.0f),
                              unitScaleScreen());

    CHECK_NEAR(rects[0].min.x, 0.0f);
    CHECK_NEAR(rects[1].min.x, 58.0f);          // 50 + 8
    CHECK_NEAR(rects[0].min.y, rects[1].min.y);
}

static void testCentringCentresTheGroupNotEachChild() {
    // The mistake this is written against: anchoring each child independently
    // centres every child on the same point, so they land on top of one another
    // and it looks like the layout did nothing at all.
    // Spacing is NON-ZERO on purpose. With zero spacing this test cannot tell a
    // correct block size from one that trails a gap after the last child, and a
    // trailing gap is off-centre by half a gap - which is exactly the kind of
    // wrong that looks fine until someone measures it.
    const std::vector<glm::vec2> sizes{{200.0f, 40.0f}, {200.0f, 40.0f}, {200.0f, 40.0f}};
    const std::vector<UIRect> rects =
        UICanvas::LayoutStack(sizes, false, 10.0f, UIAnchor::Center, glm::vec2(0.0f),
                              unitScaleScreen());

    CHECK_MSG(rects[0].min.y != rects[1].min.y, "children must not share a row");

    // Three 40-tall children with two 10 gaps is 140. Centred in 1080 that
    // spans 470..610, and the middle of what is actually drawn must be 540.
    const float top = rects[0].min.y;
    const float bottom = rects[2].max.y;
    CHECK_NEAR(bottom - top, 140.0f);
    CHECK_NEAR((top + bottom) * 0.5f, 540.0f);
}

static void testTheCrossAxisIsCentredSoARowOfMixedHeightsLinesUp() {
    const std::vector<glm::vec2> sizes{{40.0f, 20.0f}, {40.0f, 60.0f}};
    const std::vector<UIRect> rects =
        UICanvas::LayoutStack(sizes, true, 0.0f, UIAnchor::TopLeft, glm::vec2(0.0f),
                              unitScaleScreen());

    const float shortMid = (rects[0].min.y + rects[0].max.y) * 0.5f;
    const float tallMid = (rects[1].min.y + rects[1].max.y) * 0.5f;
    CHECK_NEAR(shortMid, tallMid);
}

static void testTheStackScalesWithTheScreenLikeEverythingElse() {
    // Authored units, not pixels. A menu authored at 1080 has to be the same
    // fraction of a 4K screen, or the HUD and the containers disagree about
    // what a unit means - which is worse than either convention alone.
    const std::vector<glm::vec2> sizes{{100.0f, 50.0f}, {100.0f, 50.0f}};

    const UIRect small = unitScaleScreen();
    const UIRect large{glm::vec2(0.0f, 0.0f), glm::vec2(3840.0f, 2160.0f)};

    const auto a = UICanvas::LayoutStack(sizes, false, 10.0f, UIAnchor::TopLeft,
                                         glm::vec2(0.0f), small);
    const auto b = UICanvas::LayoutStack(sizes, false, 10.0f, UIAnchor::TopLeft,
                                         glm::vec2(0.0f), large);

    CHECK_NEAR(b[1].min.y, a[1].min.y * 2.0f);
    CHECK_NEAR(b[0].max.y - b[0].min.y, (a[0].max.y - a[0].min.y) * 2.0f);
}

static void testAnEmptyStackIsEmptyRatherThanAPoint() {
    const std::vector<UIRect> rects =
        UICanvas::LayoutStack({}, false, 8.0f, UIAnchor::Center, glm::vec2(0.0f),
                              unitScaleScreen());
    CHECK_EQ(rects.size(), size_t{0});
}

// --- Nested stacks -------------------------------------------------------
//
// A stack inside a stack used to be skipped entirely: the measurement chain
// tested text, panel, button and field and stopped, so a nested stack failed
// every branch, was left out of its parent's block, and then placed itself
// against the SCREEN - landing on top of whatever the parent had put there.
// Nothing reported it, because the input pass and the draw pass agreed with
// each other. Godot nests containers freely and any dock deeper than one level
// needs this.

void testMeasureStackCountsTheGapsBetweenAndNotAfter() {
    // n children have n-1 gaps. A stack that trails one is half a gap off
    // centre and looks like nothing is wrong.
    const std::vector<glm::vec2> three{{100.0f, 20.0f}, {60.0f, 30.0f}, {80.0f, 20.0f}};

    const glm::vec2 column = UICanvas::MeasureStack(three, false, 10.0f);
    CHECK_NEAR(column.y, 90.0f);    // 20 + 30 + 20 and TWO gaps
    CHECK_NEAR(column.x, 100.0f);   // the widest child

    const glm::vec2 row = UICanvas::MeasureStack(three, true, 10.0f);
    CHECK_NEAR(row.x, 260.0f);
    CHECK_NEAR(row.y, 30.0f);       // the tallest child

    CHECK_NEAR(UICanvas::MeasureStack({}, false, 10.0f).x, 0.0f);

    const std::vector<glm::vec2> one{{50.0f, 10.0f}};
    CHECK_NEAR(UICanvas::MeasureStack(one, false, 10.0f).y, 10.0f);   // no trailing gap
}

void testAnAreaPlacesAndAScaleSizesAndTheyAreDifferentQuestions() {
    // The bug this prevents: deriving the scale from the area would shrink a
    // nested row's contents in proportion to the row. A 200-tall slot inside a
    // 1080-tall screen would draw its buttons at a fifth size.
    const UIRect slot{{100.0f, 100.0f}, {400.0f, 300.0f}};
    const std::vector<glm::vec2> sizes{{100.0f, 20.0f}, {100.0f, 20.0f}};

    const std::vector<UIRect> inSlot =
        UICanvas::LayoutStack(sizes, false, 8.0f, UIAnchor::TopLeft, glm::vec2(0.0f), slot, 1.0f);
    CHECK_EQ(static_cast<int>(inSlot.size()), 2);
    if (inSlot.size() != 2) return;

    // At scale 1 a child is its authored size, wherever it was put.
    CHECK_NEAR(inSlot[0].size().x, 100.0f);
    CHECK_NEAR(inSlot[0].size().y, 20.0f);
    CHECK_MSG(inSlot[0].min.x >= slot.min.x && inSlot[0].min.y >= slot.min.y,
              "the block was placed into the area it was given");

    // The screen overload derives its scale from what it is handed, which is
    // right for a screen and is exactly what must not happen for a slot.
    const std::vector<UIRect> derived =
        UICanvas::LayoutStack(sizes, false, 8.0f, UIAnchor::TopLeft, glm::vec2(0.0f), slot);
    CHECK_MSG(derived[0].size().x < inSlot[0].size().x,
              "deriving the scale from a small area shrinks the contents - the bug");
}

void testAStackInsideAStackIsPlacedInsideItsParent() {
    // Panels only, so no glyph is measured and the font is never dereferenced -
    // which is what lets this run with no ImGui context.
    entt::registry registry;
    const UIRect screen{{0.0f, 0.0f}, {1920.0f, 1080.0f}};

    const entt::entity outer = registry.create();
    auto& outerStack = registry.emplace<UIStackComponent>(outer);
    outerStack.horizontal = false;
    outerStack.spacing = 0.0f;
    outerStack.anchor = UIAnchor::TopLeft;

    const entt::entity top = registry.create();
    registry.emplace<UIPanelComponent>(top).size = glm::vec2(100.0f, 40.0f);
    registry.emplace<HierarchyComponent>(top).parent = outer;
    registry.emplace<UIOrderComponent>(top).order = 0;

    const entt::entity row = registry.create();
    auto& rowStack = registry.emplace<UIStackComponent>(row);
    rowStack.horizontal = true;
    rowStack.spacing = 0.0f;
    registry.emplace<HierarchyComponent>(row).parent = outer;
    registry.emplace<UIOrderComponent>(row).order = 1;

    const entt::entity left = registry.create();
    registry.emplace<UIPanelComponent>(left).size = glm::vec2(30.0f, 20.0f);
    registry.emplace<HierarchyComponent>(left).parent = row;
    registry.emplace<UIOrderComponent>(left).order = 0;

    const entt::entity right = registry.create();
    registry.emplace<UIPanelComponent>(right).size = glm::vec2(30.0f, 20.0f);
    registry.emplace<HierarchyComponent>(right).parent = row;
    registry.emplace<UIOrderComponent>(right).order = 1;

    const UICanvas::StackedRects placed =
        UISystem::LayoutStacks(registry, screen, nullptr, 1.0f);

    // Every leaf placed, INCLUDING the two inside the nested row. Before this
    // they were absent from the parent's block and laid out against the screen.
    CHECK_MSG(placed.count(top) == 1, "the plain panel was placed");
    CHECK_MSG(placed.count(left) == 1, "and the left child of the nested row");
    CHECK_MSG(placed.count(right) == 1, "and the right one");
    if (placed.count(top) == 0 || placed.count(left) == 0 || placed.count(right) == 0) return;

    const UIRect topRect = placed.at(top);
    const UIRect leftRect = placed.at(left);
    const UIRect rightRect = placed.at(right);

    // The row sits BELOW the panel above it, because the parent reserved twenty
    // units of height for it. Skip the nested stack and the parent block is 40
    // tall rather than 60, and the row lands at the top of the screen.
    CHECK_MSG(leftRect.min.y >= topRect.max.y - 0.001f,
              "the nested row got its own slot under the panel above it");

    CHECK_NEAR(leftRect.size().x, 30.0f);
    CHECK_NEAR(leftRect.size().y, 20.0f);
    CHECK_MSG(leftRect.min.x < rightRect.min.x, "and ran along the row in order");
    CHECK_NEAR(rightRect.min.x - leftRect.min.x, 30.0f);
}

void testANestedStackIsNotAlsoPlacedAgainstTheScreen() {
    // A stack whose parent is a stack must be reached ONLY by recursion. Run it
    // from the top level as well and it is placed twice with the second answer
    // silently winning - which is how a nested row ends up at the screen anchor
    // whatever its parent decided.
    entt::registry registry;
    const UIRect screen{{0.0f, 0.0f}, {1920.0f, 1080.0f}};

    const entt::entity outer = registry.create();
    auto& outerStack = registry.emplace<UIStackComponent>(outer);
    outerStack.anchor = UIAnchor::BottomRight;   // far from the nested default
    outerStack.spacing = 0.0f;

    const entt::entity row = registry.create();
    registry.emplace<UIStackComponent>(row).horizontal = true;
    registry.emplace<HierarchyComponent>(row).parent = outer;

    const entt::entity leaf = registry.create();
    registry.emplace<UIPanelComponent>(leaf).size = glm::vec2(40.0f, 20.0f);
    registry.emplace<HierarchyComponent>(leaf).parent = row;

    const UICanvas::StackedRects placed =
        UISystem::LayoutStacks(registry, screen, nullptr, 1.0f);

    CHECK_MSG(placed.count(leaf) == 1, "the leaf was placed");
    if (placed.count(leaf) == 0) return;

    // The nested stack's own anchor is the TopLeft default. Its parent anchors
    // bottom-right, so a leaf placed through the parent lands bottom-right and
    // one placed against the screen lands top-left.
    CHECK_MSG(placed.at(leaf).min.x > screen.size().x * 0.5f,
              "the leaf followed its PARENT's anchor, not the screen's");
    CHECK_MSG(placed.at(leaf).min.y > screen.size().y * 0.5f, "on both axes");

    // And the leaf is inside the row that owns it, which is the invariant the
    // anchor check above is really about.
    CHECK_MSG(placed.count(row) == 1, "the nested row was itself placed");
    if (placed.count(row) == 0) return;
    const UIRect rowRect = placed.at(row);
    const UIRect leafRect = placed.at(leaf);
    CHECK_MSG(leafRect.min.x >= rowRect.min.x - 0.001f &&
                  leafRect.max.x <= rowRect.max.x + 0.001f,
              "a child never leaves the slot its parent gave it");
}

void testACycleIsUnreachableRatherThanGuardedAgainst() {
    // A mutation removing the depth cap SURVIVED this suite, and chasing that
    // down is the useful part: a cycle is not caught by the cap, it is
    // unreachable before the cap is ever consulted.
    //
    // The reason is structural. An entity has ONE parent, so the only cycles
    // expressible are A->B->A and A->A - and in every one of them each member
    // has a stack for a parent, which is exactly the test the root loop uses to
    // skip it. Nothing in a cycle is ever a root, so nothing in a cycle is ever
    // laid out.
    //
    // The cap therefore guards legitimate DEPTH, not cycles. It is asserted
    // here as what it is rather than left reading as cycle protection.
    entt::registry registry;
    const UIRect screen{{0.0f, 0.0f}, {1920.0f, 1080.0f}};

    const entt::entity a = registry.create();
    const entt::entity b = registry.create();
    registry.emplace<UIStackComponent>(a);
    registry.emplace<UIStackComponent>(b);
    registry.emplace<HierarchyComponent>(a).parent = b;
    registry.emplace<HierarchyComponent>(b).parent = a;

    const entt::entity leaf = registry.create();
    registry.emplace<UIPanelComponent>(leaf).size = glm::vec2(10.0f, 10.0f);
    registry.emplace<HierarchyComponent>(leaf).parent = a;

    const UICanvas::StackedRects placed = UISystem::LayoutStacks(registry, screen, nullptr, 1.0f);

    CHECK_MSG(placed.empty(),
              "nothing in a cycle is a root, so nothing in a cycle is placed at all");
    CHECK_MSG(placed.count(leaf) == 0, "including a leaf hanging off one");
}

static void runTests() {
    testMeasureStackCountsTheGapsBetweenAndNotAfter();
    testAnAreaPlacesAndAScaleSizesAndTheyAreDifferentQuestions();
    testAStackInsideAStackIsPlacedInsideItsParent();
    testANestedStackIsNotAlsoPlacedAgainstTheScreen();
    testACycleIsUnreachableRatherThanGuardedAgainst();
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
    testAVerticalStackAdvancesByHeightPlusSpacing();
    testAHorizontalStackAdvancesAlongX();
    testCentringCentresTheGroupNotEachChild();
    testTheCrossAxisIsCentredSoARowOfMixedHeightsLinesUp();
    testTheStackScalesWithTheScreenLikeEverythingElse();
    testAnEmptyStackIsEmptyRatherThanAPoint();
}

TEST_MAIN("test_uicanvas", 125)
