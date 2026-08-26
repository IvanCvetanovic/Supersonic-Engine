// The ported gesture machine, against the original's own harness.
//
// Every case below is one the Godot project already asserts, in
// `tools/verify_touch.gd` section A, and the expected answers are what that
// harness prints. To re-derive them:
//
//   cd /d/The-Wolf-Brigade
//   "D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe" \
//       --headless --path . res://tools/verify_touch.tscn
//
// It printed, on 26 August 2026, before a line of this was written:
//
//   ok  : tap (down+up, no drag) -> context_tap ONLY
//   ok  : sub-threshold jitter drag -> still a tap (not a pan)
//   ok  : short drag -> pan_drag ONLY
//   ok  : pan direction = -drag (camera follows finger)
//   ok  : a pan gesture never box-selects on release
//   ok  : hold-then-drag -> marquee_updated (not a tap)
//   ok  : marquee release -> box_select (world rect)
//   ok  : 2nd finger ignored; finger 0 still resolves its tap
//   ok  : state resets: a tap after a drag still taps
//
// Nothing here shells out to Godot. That binary is Steam-installed on a volume
// a build machine will not have, and a test that silently skips when a tool is
// missing is a test that reports green having asserted nothing.
//
// The exact coordinates are the harness's too - 500,400 for the tap, 5.4px of
// jitter, a 70px drag, a 300+120ms hold - because a case that discriminates in
// GDScript at those numbers is only known to discriminate at those numbers.

#include "TestHarness.hpp"

#include "sim/GestureMachine.hpp"

#include <vector>

using namespace Supersonic;
using WolfBrigade::GestureMachine;

namespace {

// The frame a finger lands, at a position.
Contact began(int id, const glm::vec2& at) {
    Contact contact;
    contact.id = id;
    contact.position = at;
    contact.phase = ContactPhase::Began;
    return contact;
}

// A frame it has moved. `relative` is Godot's InputEventScreenDrag.relative,
// which is this engine's Contact::delta - the same number under two names.
Contact moved(int id, const glm::vec2& at, const glm::vec2& relative) {
    Contact contact;
    contact.id = id;
    contact.position = at;
    contact.delta = relative;
    contact.phase = ContactPhase::Moved;
    return contact;
}

Contact ended(int id, const glm::vec2& at) {
    Contact contact;
    contact.id = id;
    contact.position = at;
    contact.phase = ContactPhase::Ended;
    return contact;
}

// One frame with one contact, which is every frame in this suite except the
// multi-touch case.
GestureMachine::Intents step(GestureMachine& machine, const Contact& contact, float now) {
    return machine.Step(&contact, 1, now);
}

// --- 1. TAP --------------------------------------------------------------

void testATapIsAContextTapAndNothingElse() {
    GestureMachine machine;

    GestureMachine::Intents down = step(machine, began(0, glm::vec2(500.0f, 400.0f)), 0.0f);
    CHECK_MSG(!down.contextTap, "landing is not yet a tap - it might become anything");

    const GestureMachine::Intents up = step(machine, ended(0, glm::vec2(500.0f, 400.0f)), 0.05f);
    CHECK_MSG(up.contextTap, "down and up with no drag must be a context tap");
    CHECK_NEAR(up.tapPosition.x, 500.0f);
    CHECK_NEAR(up.tapPosition.y, 400.0f);

    // And NOTHING else, which is half of what the original harness asserts: a
    // tap that also pans moves the camera every time the player selects a unit.
    CHECK_MSG(!up.panned, "a tap must not pan");
    CHECK_MSG(!up.marqueeVisible, "a tap must not draw a marquee");
    CHECK_MSG(!up.boxSelect, "a tap must not band-select");
}

// --- 1b. SUB-THRESHOLD JITTER --------------------------------------------

void testJitterUnderTheThresholdIsStillATap() {
    // The single most realistic touch path, and the reason the threshold exists
    // at all: a finger tapping a screen moves a few pixels on the way down and
    // up, and every one of those arrives as a drag.
    GestureMachine machine;

    step(machine, began(0, glm::vec2(500.0f, 400.0f)), 0.0f);

    // 5.4px, which is the harness's number and comfortably under 12.
    const GestureMachine::Intents jitter =
        step(machine, moved(0, glm::vec2(505.0f, 402.0f), glm::vec2(5.0f, 2.0f)), 0.02f);
    CHECK_MSG(!jitter.panned, "jitter under the threshold must not commit to a pan");
    CHECK_MSG(!jitter.marqueeVisible, "nor to a marquee");
    CHECK(machine.CurrentMode() == GestureMachine::Mode::Undecided);

    const GestureMachine::Intents up = step(machine, ended(0, glm::vec2(505.0f, 402.0f)), 0.05f);
    CHECK_MSG(up.contextTap, "and it must still resolve as a tap");
}

// --- 2. SHORT DRAG -> PAN ------------------------------------------------

void testAQuickDragPansAndOnlyPans() {
    GestureMachine machine;

    step(machine, began(0, glm::vec2(300.0f, 400.0f)), 0.0f);

    // 70px, immediately - past the threshold and nowhere near the hold time.
    const GestureMachine::Intents drag =
        step(machine, moved(0, glm::vec2(370.0f, 400.0f), glm::vec2(70.0f, 0.0f)), 0.05f);
    CHECK_MSG(drag.panned, "a quick drag past the threshold must pan");
    CHECK_MSG(!drag.marqueeVisible, "and must not draw a marquee");

    // Negated. The camera follows the finger, so dragging right shows what is
    // to the left - and getting this backwards is a game that feels wrong in a
    // way nobody can name.
    CHECK_MSG(drag.panDelta.x < 0.0f, "pan is the negative of the drag");
    CHECK_NEAR(drag.panDelta.x, -70.0f);

    const GestureMachine::Intents up = step(machine, ended(0, glm::vec2(370.0f, 400.0f)), 0.1f);
    CHECK_MSG(!up.boxSelect, "a pan gesture never band-selects on release");
    CHECK_MSG(!up.contextTap, "nor taps");
}

// --- 3. HOLD THEN DRAG -> MARQUEE ----------------------------------------

void testHoldingFirstThenDraggingBandSelects() {
    // The same movement as the case above, distinguished ONLY by time. A
    // threshold on distance cannot express this, which is why the machine
    // carries a press timestamp at all.
    GestureMachine machine;

    step(machine, began(0, glm::vec2(200.0f, 300.0f)), 0.0f);

    // The harness delays touch_hold_select_ms + 120, so 0.42s.
    const float afterHold = 0.420f;
    const GestureMachine::Intents drag =
        step(machine, moved(0, glm::vec2(280.0f, 360.0f), glm::vec2(80.0f, 60.0f)), afterHold);
    CHECK_MSG(drag.marqueeVisible, "held first, then dragged, must draw a marquee");
    CHECK_MSG(!drag.panned, "and must not pan");

    // From the PRESS point to the finger, in either direction: a box drawn up
    // and left has to be the same box as one drawn down and right.
    CHECK_NEAR(drag.marquee.min.x, 200.0f);
    CHECK_NEAR(drag.marquee.max.x, 280.0f);
    CHECK_NEAR(drag.marquee.size().y, 60.0f);

    const GestureMachine::Intents up = step(machine, ended(0, glm::vec2(280.0f, 360.0f)), 0.5f);
    CHECK_MSG(up.boxSelect, "releasing a marquee commits the selection");
    CHECK_MSG(up.marqueeHidden, "and stops drawing it");
    CHECK_MSG(!up.contextTap, "a committed marquee is not also a tap");
    CHECK_NEAR(up.box.min.x, 200.0f);
    CHECK_NEAR(up.box.max.y, 360.0f);
}

void testABoxDrawnBackwardsIsTheSameBox() {
    // Not in the original harness, and it is the first thing a player does:
    // drag up and to the left. Rect2(top_left, (a-b).abs()) handles it and a
    // naive max-minus-min does not.
    GestureMachine machine;
    step(machine, began(0, glm::vec2(280.0f, 360.0f)), 0.0f);
    const GestureMachine::Intents drag =
        step(machine, moved(0, glm::vec2(200.0f, 300.0f), glm::vec2(-80.0f, -60.0f)), 0.42f);

    CHECK(drag.marqueeVisible);
    CHECK_NEAR(drag.marquee.min.x, 200.0f);
    CHECK_NEAR(drag.marquee.min.y, 300.0f);
    CHECK_NEAR(drag.marquee.size().x, 80.0f);
    CHECK_NEAR(drag.marquee.size().y, 60.0f);
}

// --- 4. MULTI-TOUCH ------------------------------------------------------

void testASecondFingerIsIgnoredAndTheFirstStillResolves() {
    // A player resting a thumb on the screen must not cancel the drag they are
    // in the middle of. The original latches an index; this latches an id.
    GestureMachine machine;

    step(machine, began(0, glm::vec2(400.0f, 400.0f)), 0.0f);
    CHECK_EQ(machine.TrackedFinger(), 0);

    const Contact second[] = {
        moved(0, glm::vec2(400.0f, 400.0f), glm::vec2(0.0f, 0.0f)),
        began(1, glm::vec2(700.0f, 200.0f)),
    };
    machine.Step(second, 2, 0.02f);
    CHECK_MSG(machine.TrackedFinger() == 0, "the second finger must not steal the gesture");

    const Contact lift[] = {
        moved(0, glm::vec2(400.0f, 400.0f), glm::vec2(0.0f, 0.0f)),
        ended(1, glm::vec2(700.0f, 200.0f)),
    };
    const GestureMachine::Intents ignored = machine.Step(lift, 2, 0.04f);
    CHECK_MSG(!ignored.contextTap, "the second finger lifting must not fire a tap either");

    const GestureMachine::Intents up = step(machine, ended(0, glm::vec2(400.0f, 400.0f)), 0.06f);
    CHECK_MSG(up.contextTap, "finger 0 must still resolve its own tap");
}

// --- 5. STATE RESETS -----------------------------------------------------

void testATapAfterADragStillTaps() {
    // Every state this machine enters has to be left. A mode that latches past
    // the finger that set it turns the next tap into whatever the last gesture
    // was.
    GestureMachine machine;

    step(machine, began(0, glm::vec2(200.0f, 300.0f)), 0.0f);
    step(machine, moved(0, glm::vec2(280.0f, 360.0f), glm::vec2(80.0f, 60.0f)), 0.42f);
    step(machine, ended(0, glm::vec2(280.0f, 360.0f)), 0.5f);
    CHECK(machine.CurrentMode() == GestureMachine::Mode::None);
    CHECK_EQ(machine.TrackedFinger(), -1);

    step(machine, began(0, glm::vec2(600.0f, 500.0f)), 1.0f);
    const GestureMachine::Intents up = step(machine, ended(0, glm::vec2(600.0f, 500.0f)), 1.05f);
    CHECK_MSG(up.contextTap, "a tap after a marquee must still be a tap");
}

// --- placement mode ------------------------------------------------------

void testPlacementModeTakesThePointerAndUnlatchesWhatWasInFlight() {
    // set_placement_mode resets the gesture in the original, and the comment
    // there says why: a mode change mid-drag would otherwise leave the marquee
    // latched and resume it when placement ended - the game band-selecting on
    // its own after the player put a building down.
    GestureMachine machine;

    step(machine, began(0, glm::vec2(200.0f, 300.0f)), 0.0f);
    step(machine, moved(0, glm::vec2(280.0f, 360.0f), glm::vec2(80.0f, 60.0f)), 0.42f);
    CHECK(machine.CurrentMode() == GestureMachine::Mode::Marquee);

    machine.SetPlacementMode(true);
    CHECK(machine.CurrentMode() == GestureMachine::Mode::None);
    CHECK_EQ(machine.TrackedFinger(), -1);

    // And nothing fires while it is on: those intents belong to the placement
    // system, which is a different one.
    const GestureMachine::Intents during =
        step(machine, moved(0, glm::vec2(300.0f, 380.0f), glm::vec2(20.0f, 20.0f)), 0.5f);
    CHECK_MSG(!during.marqueeVisible && !during.panned, "placement mode owns the pointer");

    machine.SetPlacementMode(false);
    step(machine, began(0, glm::vec2(600.0f, 500.0f)), 1.0f);
    const GestureMachine::Intents up = step(machine, ended(0, glm::vec2(600.0f, 500.0f)), 1.05f);
    CHECK_MSG(up.contextTap, "and gives it back cleanly");
}

// --- the threshold itself ------------------------------------------------

void testTheThresholdIsStrictlyPastAndTheHoldIsAtLeast() {
    // Two comparisons the GDScript makes with different operators - `>` on
    // distance and `>=` on time - and a port that swaps either one changes
    // which gesture a borderline drag becomes.
    {
        GestureMachine machine;
        step(machine, began(0, glm::vec2(0.0f, 0.0f)), 0.0f);
        // Exactly 12: NOT past it.
        step(machine, moved(0, glm::vec2(12.0f, 0.0f), glm::vec2(12.0f, 0.0f)), 0.01f);
        CHECK_MSG(machine.CurrentMode() == GestureMachine::Mode::Undecided,
                  "exactly the threshold is not past it");
    }
    {
        GestureMachine machine;
        step(machine, began(0, glm::vec2(0.0f, 0.0f)), 0.0f);
        step(machine, moved(0, glm::vec2(13.0f, 0.0f), glm::vec2(13.0f, 0.0f)), 0.01f);
        CHECK_MSG(machine.CurrentMode() == GestureMachine::Mode::Pan, "one pixel past it is");
    }
    {
        // Exactly the hold time counts as held, so a deliberate 300ms press
        // band-selects rather than panning.
        GestureMachine machine;
        step(machine, began(0, glm::vec2(0.0f, 0.0f)), 0.0f);
        step(machine, moved(0, glm::vec2(40.0f, 0.0f), glm::vec2(40.0f, 0.0f)), 0.300f);
        CHECK_MSG(machine.CurrentMode() == GestureMachine::Mode::Marquee,
                  "exactly the hold time is held");
    }
}

} // namespace

static void runTests() {
    testATapIsAContextTapAndNothingElse();
    testJitterUnderTheThresholdIsStillATap();
    testAQuickDragPansAndOnlyPans();
    testHoldingFirstThenDraggingBandSelects();
    testABoxDrawnBackwardsIsTheSameBox();
    testASecondFingerIsIgnoredAndTheFirstStillResolves();
    testATapAfterADragStillTaps();
    testPlacementModeTakesThePointerAndUnlatchesWhatWasInFlight();
    testTheThresholdIsStrictlyPastAndTheHoldIsAtLeast();
}

TEST_MAIN("test_wb_gestures", 40)
