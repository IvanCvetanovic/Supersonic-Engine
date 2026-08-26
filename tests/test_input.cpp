// Regression tests for the input layer.
//
// The engine previously had no way for gameplay to read input at all, so every
// failure mode here is new. They are also the quiet kind: an edge that fires
// twice makes a jump double-trigger once in a while, a missing deadzone walks
// the player across the level while the controller sits on the desk, and an
// action that reads a stale frame responds one frame late in a way nobody can
// reproduce on demand.
//
// Input is deliberately GLFW-free, which is what lets this run with no window:
// the test hands over a RawInputState instead of driving real hardware.

#include "TestHarness.hpp"
#include "core/Input.hpp"

#include <cmath>
#include <initializer_list>
#include <utility>
#include <string>

using namespace Supersonic;

// Feeds one frame of state.
static void frame(const RawInputState& state) {
    Input::Update(state);
}

// Back to a clean slate, bindings AND devices.
//
// ClearBindings deliberately no longer touches who owns the mouse and the
// keyboard - those are not bindings, and resetting them there would mean a
// settings screen reloading the defaults released a locked pointer mid-game.
// So a suite that leaves the pointer locked has to put it back itself, through
// the same public setters a game uses.
static void reset() {
    Input::ClearBindings();
    Input::SetCursorMode(CursorMode::Normal);
    Input::SuppressCursorCapture(false);
    Input::SetWindowFocused(true);
    Input::SetTextCaptureActive(false);

    // Two frames of nothing, so the cursor-mode rebase and the edge detection
    // both settle before the next case starts feeding real input.
    frame(RawInputState{});
    frame(RawInputState{});
}

static RawInputState withKey(int key) {
    RawInputState state{};
    state.keys[key] = true;
    return state;
}

static void testActionRespondsToItsKey() {
    reset();
    Input::BindActionKey("Jump", Key::Space);

    frame(RawInputState{});
    CHECK_MSG(!Input::IsDown("Jump"), "nothing is pressed yet");

    frame(withKey(Key::Space));
    CHECK_MSG(Input::IsDown("Jump"), "the bound key must satisfy the action");
    CHECK_MSG(Input::WasPressed("Jump"), "and register as an edge on the first frame");
}

static void testPressIsAnEdgeNotALevel() {
    // The failure this guards: a jump that fires every frame the button is held
    // rather than once when it goes down.
    reset();
    Input::BindActionKey("Jump", Key::Space);

    frame(RawInputState{});
    frame(withKey(Key::Space));
    CHECK_MSG(Input::WasPressed("Jump"), "first frame down is a press");

    frame(withKey(Key::Space));
    CHECK_MSG(Input::IsDown("Jump"), "still held");
    CHECK_MSG(!Input::WasPressed("Jump"), "but holding must NOT re-fire the press");

    frame(RawInputState{});
    CHECK_MSG(!Input::IsDown("Jump"), "released");
    CHECK_MSG(Input::WasReleased("Jump"), "and the release is its own edge");
    CHECK_MSG(!Input::WasPressed("Jump"), "a release is not a press");

    frame(RawInputState{});
    CHECK_MSG(!Input::WasReleased("Jump"), "and the release does not repeat either");
}

static void testKeyHeldAtStartupIsNotAPress() {
    // With no previous frame to compare against, every key already down when
    // the window opens would otherwise report as pressed on frame one - which
    // fires every bound action the moment the game starts.
    reset();
    Input::BindActionKey("Fire", Key::F);

    frame(withKey(Key::F));
    CHECK_MSG(Input::IsDown("Fire"), "the key is genuinely down");
    CHECK_MSG(!Input::WasPressed("Fire"), "but the first frame must not invent an edge");
}

static void testAnySourceSatisfiesAnAction() {
    reset();
    Input::BindActionKey("Fire", Key::F);
    Input::BindActionMouseButton("Fire", MouseButton::Left);
    Input::BindActionPadButton("Fire", Pad::RightBumper);

    frame(RawInputState{});

    RawInputState mouse{};
    mouse.mouseButtons[MouseButton::Left] = true;
    frame(mouse);
    CHECK_MSG(Input::IsDown("Fire"), "the mouse must satisfy it");

    RawInputState pad{};
    pad.padConnected = true;
    pad.padButtons[Pad::RightBumper] = true;
    frame(pad);
    CHECK_MSG(Input::IsDown("Fire"), "and so must the gamepad");

    frame(withKey(Key::F));
    CHECK_MSG(Input::IsDown("Fire"), "and the keyboard");
}

static void testDisconnectedPadIsIgnored() {
    // Stale button bytes in a disconnected pad's state must not drive anything.
    reset();
    Input::BindActionPadButton("Fire", Pad::A);

    RawInputState state{};
    state.padConnected = false;
    state.padButtons[Pad::A] = true;
    frame(state);

    CHECK_MSG(!Input::IsDown("Fire"), "a pad that is not connected cannot press anything");
}

static void testKeyAxisIsBipolar() {
    reset();
    Input::BindAxisKeys("MoveX", Key::D, Key::A);

    frame(withKey(Key::D));
    CHECK_NEAR(Input::GetAxis("MoveX"), 1.0f);

    frame(withKey(Key::A));
    CHECK_NEAR(Input::GetAxis("MoveX"), -1.0f);

    RawInputState both{};
    both.keys[Key::A] = true;
    both.keys[Key::D] = true;
    frame(both);
    CHECK_MSG(std::fabs(Input::GetAxis("MoveX")) < 1e-5f,
              "opposite keys held together must cancel, not pick one");
}

static void testStickDeadzone() {
    // A resting stick reads slightly off centre and drifts as it ages. Without
    // a deadzone the player walks across the level with nobody touching it.
    reset();
    Input::BindAxisPad("MoveX", Pad::LeftX);

    RawInputState resting{};
    resting.padConnected = true;
    resting.padAxes[Pad::LeftX] = 0.1f;   // inside the deadzone
    frame(resting);
    CHECK_MSG(std::fabs(Input::GetAxis("MoveX")) < 1e-5f, "a resting stick must read zero");

    RawInputState pushed{};
    pushed.padConnected = true;
    pushed.padAxes[Pad::LeftX] = 1.0f;
    frame(pushed);
    CHECK_NEAR(Input::GetAxis("MoveX"), 1.0f);
}

static void testDeadzoneRescalesRatherThanClipping() {
    // Just past the deadzone must start near zero, not jump to the deadzone
    // value - otherwise the control snaps into motion instead of easing in.
    reset();
    Input::BindAxisPad("MoveX", Pad::LeftX);

    RawInputState state{};
    state.padConnected = true;
    state.padAxes[Pad::LeftX] = Input::kStickDeadzone + 0.001f;
    frame(state);

    const float value = Input::GetAxis("MoveX");
    CHECK_MSG(value > 0.0f, "just past the threshold must be live");
    CHECK_MSG(value < 0.02f, "but must start from near zero, not from the deadzone value");
}

static void testPadAxisCanBeInverted() {
    // GLFW reports stick up as negative, so the default bindings invert Y. A
    // scale that did not apply would move the player the wrong way.
    reset();
    Input::BindAxisPad("MoveY", Pad::LeftY, -1.0f);

    RawInputState state{};
    state.padConnected = true;
    state.padAxes[Pad::LeftY] = -1.0f;   // stick pushed up
    frame(state);

    CHECK_NEAR(Input::GetAxis("MoveY"), 1.0f);
}

static void testStrongestSourceWinsRatherThanSumming() {
    reset();
    Input::BindAxisKeys("MoveX", Key::D, Key::A);
    Input::BindAxisPad("MoveX", Pad::LeftX);

    RawInputState both{};
    both.keys[Key::D] = true;        // full deflection from the keyboard
    both.padConnected = true;
    both.padAxes[Pad::LeftX] = 0.5f; // half from the stick
    frame(both);

    CHECK_MSG(Input::GetAxis("MoveX") <= 1.0f + 1e-5f,
              "a key and a stick together must not exceed full deflection");
    CHECK_NEAR(Input::GetAxis("MoveX"), 1.0f);
}

static void testUnknownNamesAreInertNotFatal() {
    reset();
    frame(RawInputState{});

    CHECK_MSG(!Input::IsDown("NoSuchAction"), "an unbound action is simply never down");
    CHECK_MSG(!Input::WasPressed("NoSuchAction"), "and never pressed");
    CHECK_NEAR(Input::GetAxis("NoSuchAxis"), 0.0f);
}

static void testMouseDeltaIsPerFrame() {
    reset();

    RawInputState first{};
    first.mousePosition = glm::vec2(100.0f, 100.0f);
    frame(first);

    RawInputState second{};
    second.mousePosition = glm::vec2(140.0f, 90.0f);
    frame(second);

    CHECK_NEAR(Input::MouseDelta().x, 40.0f);
    CHECK_NEAR(Input::MouseDelta().y, -10.0f);
    CHECK_NEAR(Input::MousePosition().x, 140.0f);

    // Standing still must report no movement, not the previous delta.
    frame(second);
    CHECK_NEAR(Input::MouseDelta().x, 0.0f);
    CHECK_NEAR(Input::MouseDelta().y, 0.0f);
}

static void testDefaultBindingsCoverTheBasics() {
    Input::LoadDefaultBindings();

    frame(RawInputState{});
    frame(withKey(Key::Space));
    CHECK_MSG(Input::WasPressed("Jump"), "Space must jump out of the box");

    frame(withKey(Key::W));
    CHECK_NEAR(Input::GetAxis("MoveY"), 1.0f);

    frame(withKey(Key::D));
    CHECK_NEAR(Input::GetAxis("MoveX"), 1.0f);

    CHECK_MSG(!Input::ActionNames().empty(), "the editor needs to be able to list them");
    CHECK_MSG(!Input::AxisNames().empty(), "and the axes too");
}

static void testRebindingAddsRatherThanReplaces() {
    reset();
    Input::BindActionKey("Fire", Key::F);
    Input::BindActionKey("Fire", Key::Enter);

    frame(RawInputState{});
    frame(withKey(Key::F));
    CHECK_MSG(Input::IsDown("Fire"), "the original binding still works");

    frame(withKey(Key::Enter));
    CHECK_MSG(Input::IsDown("Fire"), "and the added one does too");
}

static void testOutOfRangeCodesAreRejected() {
    reset();
    Input::BindActionKey("Bad", 99999);
    Input::BindActionKey("Bad", -5);
    Input::BindActionPadButton("Bad", 400);
    Input::BindActionMouseButton("Bad", 99);
    Input::BindAxisPad("BadAxis", 77);

    frame(RawInputState{});
    CHECK_MSG(!Input::IsDown("Bad"), "a nonsense binding must not index out of bounds");
    CHECK_NEAR(Input::GetAxis("BadAxis"), 0.0f);
}

// --- who owns the pointer ---------------------------------------------------

static void testTheCursorModeIsARequestUntilSomethingVetoesIt() {
    reset();
    CHECK_MSG(Input::EffectiveCursorMode() == CursorMode::Normal,
              "nothing has asked for anything yet");

    Input::SetCursorMode(CursorMode::Locked);
    CHECK_MSG(Input::EffectiveCursorMode() == CursorMode::Locked,
              "a game that asks for a locked pointer and is not vetoed gets one");

    // The editor's veto: between plays, or with the pointer somewhere other
    // than the viewport, the editor needs a real pointer back.
    Input::SuppressCursorCapture(true);
    CHECK_MSG(Input::EffectiveCursorMode() == CursorMode::Normal,
              "a suppressed capture is a normal pointer whatever the game asked for");
    CHECK_MSG(Input::RequestedCursorMode() == CursorMode::Locked,
              "and the request survives the veto, or resuming play could not restore it");

    Input::SuppressCursorCapture(false);
    CHECK_MSG(Input::EffectiveCursorMode() == CursorMode::Locked,
              "lifting the veto restores what was asked for rather than losing it");
}

static void testLosingTheWindowIsAlwaysAWayOut() {
    // The safety mechanism, not a nicety: a locked pointer is invisible and
    // cannot leave the window, so a game that locks it and offers no release
    // would trap whoever ran it. Alt-tab has to work.
    reset();
    Input::SetCursorMode(CursorMode::Locked);

    Input::SetWindowFocused(false);
    CHECK_MSG(Input::EffectiveCursorMode() == CursorMode::Normal,
              "an unfocused window gives the pointer back");

    Input::SetWindowFocused(true);
    CHECK_MSG(Input::EffectiveCursorMode() == CursorMode::Locked,
              "and coming back re-captures, because the request was never dropped");

    // Focus defaults to true, so a headless run - which reports focus to
    // nobody - is not silently unable to capture.
    reset();
    CHECK_MSG(Input::WindowFocused(), "focus defaults to present");
}

static void testCapturingTheMouseDoesNotSnapTheView() {
    // The failure this exists for: locking the pointer teleports it - GLFW
    // swaps screen coordinates for unbounded virtual ones - and a teleport
    // differenced against last frame is a delta of several hundred pixels. The
    // camera snaps to face somewhere else entirely on the frame you capture,
    // once, which is exactly the kind of thing that gets blamed on the mouse.
    reset();

    RawInputState at{};
    at.mousePosition = glm::vec2(640.0f, 360.0f);
    frame(at);
    frame(at);
    CHECK_NEAR(Input::MouseDelta().x, 0.0f);

    Input::SetCursorMode(CursorMode::Locked);

    // The polling layer applies the mode and THEN reads the position, so the
    // first frame under the new mode already reports the new coordinate space.
    RawInputState teleported{};
    teleported.mousePosition = glm::vec2(-4211.0f, 90210.0f);
    frame(teleported);
    CHECK_MSG(Input::MouseDelta() == glm::vec2(0.0f),
              "the frame the pointer is captured must report no movement at all");

    // And the very next frame is ordinary again - the baseline moved, it was
    // not suppressed.
    RawInputState moved = teleported;
    moved.mousePosition += glm::vec2(12.0f, -7.0f);
    frame(moved);
    CHECK_NEAR(Input::MouseDelta().x, 12.0f);
    CHECK_NEAR(Input::MouseDelta().y, -7.0f);
}

static void testReleasingTheMouseDoesNotSnapEither() {
    // The same teleport in the other direction: GLFW puts the pointer back
    // where it was before the capture, which is just as far to jump.
    reset();
    Input::SetCursorMode(CursorMode::Locked);

    RawInputState virt{};
    virt.mousePosition = glm::vec2(-4211.0f, 90210.0f);
    frame(virt);
    frame(virt);

    Input::SetCursorMode(CursorMode::Normal);
    RawInputState restored{};
    restored.mousePosition = glm::vec2(640.0f, 360.0f);
    frame(restored);
    CHECK_MSG(Input::MouseDelta() == glm::vec2(0.0f),
              "releasing the pointer must not report the trip back as a look");
}

static void testAVetoedCaptureAlsoRebasesTheDelta() {
    // The veto changes the EFFECTIVE mode, which is what moves the pointer -
    // so it has to rebase too. Keying the correction off the request alone
    // would snap the view every time the editor took the mouse back.
    reset();
    Input::SetCursorMode(CursorMode::Locked);

    RawInputState virt{};
    virt.mousePosition = glm::vec2(-900.0f, 4000.0f);
    frame(virt);
    frame(virt);

    Input::SuppressCursorCapture(true);
    RawInputState back{};
    back.mousePosition = glm::vec2(200.0f, 200.0f);
    frame(back);
    CHECK_MSG(Input::MouseDelta() == glm::vec2(0.0f),
              "the editor taking the pointer back must not read as a look");
}

// --- typing, and who the keyboard belongs to --------------------------------

static void testTypedCharactersArriveAndDoNotLinger() {
    reset();

    RawInputState typed{};
    typed.textCharacters[0] = 'H';
    typed.textCharacters[1] = 0x00E9;   // e-acute, to prove this is not ASCII
    typed.textCharacterCount = 2;
    frame(typed);

    CHECK_EQ(Input::TypedCharacterCount(), 2);
    CHECK_MSG(Input::TypedCharacters()[0] == 'H', "in the order they were typed");
    CHECK_MSG(Input::TypedCharacters()[1] == 0x00E9u,
              "and as codepoints, not bytes - GLFW hands over UTF-32");

    // A frame with no typing must not repeat the last one, or a held key would
    // spell its letter forever.
    frame(RawInputState{});
    CHECK_EQ(Input::TypedCharacterCount(), 0);
}

static void testTypingDoesNotAlsoWalkThePlayer() {
    // The failure this exists for: ImGui's io.WantTextInput only knows about
    // ImGui's own widgets, so a field drawn by the engine's UI canvas is
    // invisible to it. Without a veto of our own, typing a name would spell it
    // and walk the character across the level at the same time.
    reset();
    Input::BindActionKey("Fire", Key::F);
    Input::BindAxisKeys("MoveX", Key::D, Key::A);

    RawInputState pressing{};
    pressing.keys[Key::F] = true;
    pressing.keys[Key::D] = true;
    frame(pressing);
    CHECK_MSG(Input::IsDown("Fire"), "the game has the keyboard to begin with");
    CHECK_NEAR(Input::GetAxis("MoveX"), 1.0f);

    Input::SetTextCaptureActive(true);
    frame(pressing);
    CHECK_MSG(!Input::IsDown("Fire"), "a bound key is a letter while something is being typed into");
    CHECK_NEAR(Input::GetAxis("MoveX"), 0.0f);

    // But the raw queries stay truthful: the field itself has to read Backspace
    // through something, and so does the host's Escape hatch.
    CHECK_MSG(Input::IsKeyDown(Key::D), "the key really is down and must still say so");

    Input::SetTextCaptureActive(false);
    frame(pressing);
    CHECK_MSG(Input::IsDown("Fire"), "and it comes back when the field is done with it");
}

static void testAPadStillDrivesTheGameWhileANameIsTyped() {
    // Only KEY sources are taken away. Nobody types with a thumbstick, and a
    // controller player should not have their game stop because a name box on
    // screen took focus.
    reset();
    Input::BindActionKey("Fire", Key::F);
    Input::BindActionPadButton("Fire", Pad::A);
    Input::BindActionMouseButton("Fire", MouseButton::Left);

    Input::SetTextCaptureActive(true);

    RawInputState pad{};
    pad.padConnected = true;
    pad.padButtons[Pad::A] = true;
    frame(pad);
    CHECK_MSG(Input::IsDown("Fire"), "the gamepad keeps working while a name is typed");

    RawInputState mouse{};
    mouse.mouseButtons[MouseButton::Left] = true;
    frame(mouse);
    CHECK_MSG(Input::IsDown("Fire"), "and so does the mouse");
}

static void testTakingTheKeyboardIsAnEdgeLikeAnyOther() {
    // The first draft of this suppressed both edges, reasoning that a release
    // nobody performed is not a release. That is the wrong invariant to keep.
    // The LEVEL changes - the action reads false the moment the veto is up - so
    // an edge that did not fire would leave a game tracking movement by edges
    // convinced the player was still walking, with the run animation stuck
    // mid-stride for as long as the name took to type.
    reset();
    Input::BindActionKey("Walk", Key::W);

    RawInputState held{};
    held.keys[Key::W] = true;
    frame(held);
    frame(held);
    CHECK_MSG(Input::IsDown("Walk") && !Input::WasPressed("Walk"), "held, and past its press edge");

    Input::SetTextCaptureActive(true);
    frame(held);
    CHECK_MSG(!Input::IsDown("Walk"), "the action goes quiet, because W is now a letter");
    CHECK_MSG(Input::WasReleased("Walk"),
              "and says so exactly once, so a walk animation can stop");

    frame(held);
    CHECK_MSG(!Input::WasReleased("Walk"), "once, not every frame the name is being typed");

    Input::SetTextCaptureActive(false);
    frame(held);
    CHECK_MSG(Input::IsDown("Walk"), "the action comes back with the keyboard");
    CHECK_MSG(Input::WasPressed("Walk"),
              "and reports the press, because the key really is still down");
}

// --- contacts ---------------------------------------------------------------
//
// The engine had ONE mouse position, so a two-finger gesture arrived as a
// single point jittering somewhere between the fingers and a machine keyed on
// finger index had no index to key on. These are the tests for the part that
// cannot come from a snapshot: which finger is new, which is still down, and
// which has just lifted - the last of which is not in the current frame at all
// and would otherwise vanish without ever reporting an end.

// A frame with the given fingers down, and nothing else.
static RawInputState touching(std::initializer_list<std::pair<int, glm::vec2>> fingers) {
    RawInputState state;
    int index = 0;
    for (const auto& [id, position] : fingers) {
        if (index >= Touch::kMaxContacts) break;
        state.contacts[index].id = id;
        state.contacts[index].position = position;
        ++index;
    }
    state.contactCount = index;
    return state;
}

static void testAFingerThatJustLandedBegins() {
    reset();
    frame(touching({}));
    frame(touching({{0, glm::vec2(100.0f, 200.0f)}}));

    CHECK_EQ(Input::ContactCount(), 1);
    Contact contact;
    CHECK_MSG(Input::TryGetContact(0, contact), "finger 0 must be findable by its id");
    CHECK(contact.phase == ContactPhase::Began);
    CHECK_NEAR(contact.position.x, 100.0f);

    // Zero, not the distance from wherever the last finger lifted. A gesture
    // that measures movement from the first frame of a touch measures it from
    // here, and a threshold crossed on frame one is a gesture decided before
    // anybody moved.
    CHECK_NEAR(contact.delta.x, 0.0f);
    CHECK_NEAR(contact.delta.y, 0.0f);
}

static void testAFingerThatStaysDownMovesAndCarriesItsDelta() {
    reset();
    frame(touching({{0, glm::vec2(100.0f, 200.0f)}}));
    frame(touching({{0, glm::vec2(112.0f, 195.0f)}}));

    Contact contact;
    CHECK(Input::TryGetContact(0, contact));
    CHECK(contact.phase == ContactPhase::Moved);
    CHECK_NEAR(contact.delta.x, 12.0f);
    CHECK_NEAR(contact.delta.y, -5.0f);
}

static void testAFingerThatHasNotMovedIsStillMovedRatherThanStationary() {
    // "Stationary" is a threshold question and the threshold belongs to
    // whoever is asking: a 12px hold test and a 2px one disagree about the same
    // frame, and neither answer belongs in the engine. Moved with a zero delta
    // says what happened and lets the asker decide what it means.
    reset();
    frame(touching({{0, glm::vec2(100.0f, 200.0f)}}));
    frame(touching({{0, glm::vec2(100.0f, 200.0f)}}));

    Contact contact;
    CHECK(Input::TryGetContact(0, contact));
    CHECK(contact.phase == ContactPhase::Moved);
    CHECK_NEAR(contact.delta.x, 0.0f);
}

static void testALiftedFingerIsReportedOnceAndThenGone() {
    // The case a snapshot cannot express. The finger is not in this frame's
    // state at all, so without synthesising it here a gesture that ends on
    // release never ends: the machine sits in PAN forever with nothing moving.
    reset();
    frame(touching({{0, glm::vec2(100.0f, 200.0f)}}));
    frame(touching({{0, glm::vec2(150.0f, 200.0f)}}));
    frame(touching({}));

    CHECK_EQ(Input::ContactCount(), 1);
    Contact contact;
    CHECK_MSG(Input::TryGetContact(0, contact), "the lift must be reported");
    CHECK(contact.phase == ContactPhase::Ended);

    // At the last place it was seen, not at the origin.
    CHECK_NEAR(contact.position.x, 150.0f);

    // And exactly once. An end that repeats fires whatever the release does -
    // a unit order, a shot - every frame until the next touch.
    frame(touching({}));
    CHECK_EQ(Input::ContactCount(), 0);
}

static void testTwoFingersAreTwoContactsWithTheirOwnDeltas() {
    // The whole reason this exists. One mousePosition cannot say that finger 0
    // went left while finger 1 went right, which is the difference between a
    // pan and a pinch.
    reset();
    frame(touching({{0, glm::vec2(100.0f, 100.0f)}, {1, glm::vec2(300.0f, 100.0f)}}));
    frame(touching({{0, glm::vec2(80.0f, 100.0f)}, {1, glm::vec2(320.0f, 100.0f)}}));

    CHECK_EQ(Input::ContactCount(), 2);

    Contact first, second;
    CHECK(Input::TryGetContact(0, first));
    CHECK(Input::TryGetContact(1, second));
    CHECK_NEAR(first.delta.x, -20.0f);
    CHECK_NEAR(second.delta.x, 20.0f);
}

static void testASecondFingerBeginsWhileTheFirstKeepsMoving() {
    // The phases are per contact, not per frame. A machine that reads "this
    // frame is a Began frame" and applies it to everything restarts the pan the
    // moment a second finger lands.
    reset();
    frame(touching({{0, glm::vec2(100.0f, 100.0f)}}));
    frame(touching({{0, glm::vec2(110.0f, 100.0f)}, {1, glm::vec2(300.0f, 100.0f)}}));

    Contact first, second;
    CHECK(Input::TryGetContact(0, first));
    CHECK(Input::TryGetContact(1, second));
    CHECK(first.phase == ContactPhase::Moved);
    CHECK(second.phase == ContactPhase::Began);
    CHECK_NEAR(first.delta.x, 10.0f);
    CHECK_NEAR(second.delta.x, 0.0f);
}

static void testAnIdIsFollowedRatherThanAPositionInTheList() {
    // Devices do not promise an order, and a finger that lifts closes a gap.
    // A machine that decided PAN on "the second contact" would be panning with
    // a different finger the moment the first one lifted; one that decided on
    // finger 1 still has finger 1.
    reset();
    frame(touching({{0, glm::vec2(100.0f, 100.0f)}, {1, glm::vec2(300.0f, 100.0f)}}));
    // Same two fingers, reported the other way round, and one has moved.
    frame(touching({{1, glm::vec2(340.0f, 100.0f)}, {0, glm::vec2(100.0f, 100.0f)}}));

    Contact tracked;
    CHECK(Input::TryGetContact(1, tracked));
    CHECK_NEAR(tracked.delta.x, 40.0f);
    CHECK(tracked.phase == ContactPhase::Moved);
}

static void testAnUnknownIdIsAbsentRatherThanWrong() {
    reset();
    frame(touching({{0, glm::vec2(100.0f, 100.0f)}}));

    Contact contact;
    CHECK_MSG(!Input::TryGetContact(7, contact), "a finger nobody is touching with must not be found");

    // And an index past the end is a contact that is not there, not a crash:
    // a loop that runs one too far is an ordinary bug and should behave like
    // one.
    CHECK_EQ(Input::GetContact(5).id, -1);
    CHECK_EQ(Input::GetContact(-1).id, -1);
}

static void testAFrameWithNoContactsChangesNothingElse() {
    // The control, and the promise every existing caller depends on: a snapshot
    // that never fills these in is the engine that existed before they did.
    reset();
    RawInputState state;
    state.mousePosition = glm::vec2(42.0f, 43.0f);
    state.mouseButtons[MouseButton::Left] = true;
    frame(state);

    CHECK_EQ(Input::ContactCount(), 0);
    CHECK_MSG(Input::IsMouseButtonDown(MouseButton::Left), "the mouse must be unaffected");
    CHECK_NEAR(Input::MousePosition().x, 42.0f);
}

static void testACountLargerThanTheFilledSlotsProducesNoPhantomFinger() {
    // A platform layer that sets the count before filling the array leaves
    // empty slots behind it. Those hold an id of -1, and a phantom finger at
    // the origin would begin a gesture nobody started.
    reset();
    RawInputState state;
    state.contacts[0].id = 0;
    state.contacts[0].position = glm::vec2(100.0f, 100.0f);
    state.contactCount = 4;
    frame(state);

    CHECK_EQ(Input::ContactCount(), 1);
    CHECK_EQ(Input::GetContact(0).id, 0);
}

static void testACountBeyondTheArrayIsClampedRatherThanReadPast() {
    reset();
    RawInputState state;
    for (int i = 0; i < Touch::kMaxContacts; ++i) {
        state.contacts[i].id = i;
        state.contacts[i].position = glm::vec2(static_cast<float>(i), 0.0f);
    }
    state.contactCount = Touch::kMaxContacts + 40;
    frame(state);

    CHECK_EQ(Input::ContactCount(), Touch::kMaxContacts);
}

static void testTheMouseIsContactZeroForAsLongAsItIsHeld() {
    // The desktop path, end to end. Without it a gesture machine written
    // against contacts is dead code until Android exists, which is the same as
    // untested - and the day it stops being dead code is the worst possible day
    // to find out it was wrong.
    reset();

    RawInputState hovering;
    hovering.mousePosition = glm::vec2(50.0f, 50.0f);
    Input::SynthesiseMouseContact(hovering);
    frame(hovering);
    CHECK_MSG(Input::ContactCount() == 0,
              "hovering is not touching; a contact here begins every gesture on entry");

    RawInputState pressed;
    pressed.mousePosition = glm::vec2(50.0f, 50.0f);
    pressed.mouseButtons[MouseButton::Left] = true;
    Input::SynthesiseMouseContact(pressed);
    frame(pressed);
    Contact contact;
    CHECK(Input::TryGetContact(0, contact));
    CHECK(contact.phase == ContactPhase::Began);

    RawInputState dragged;
    dragged.mousePosition = glm::vec2(62.0f, 50.0f);
    dragged.mouseButtons[MouseButton::Left] = true;
    Input::SynthesiseMouseContact(dragged);
    frame(dragged);
    CHECK(Input::TryGetContact(0, contact));
    CHECK(contact.phase == ContactPhase::Moved);

    // Twelve pixels, which is the threshold Wolf Brigade's gesture machine
    // arbitrates a marquee against a pan on. It reads that from here.
    CHECK_NEAR(contact.delta.x, 12.0f);

    RawInputState released;
    released.mousePosition = glm::vec2(62.0f, 50.0f);
    Input::SynthesiseMouseContact(released);
    frame(released);
    CHECK(Input::TryGetContact(0, contact));
    CHECK(contact.phase == ContactPhase::Ended);
    CHECK_NEAR(contact.position.x, 62.0f);
}

static void runTests() {
    testActionRespondsToItsKey();
    testPressIsAnEdgeNotALevel();
    testKeyHeldAtStartupIsNotAPress();
    testAnySourceSatisfiesAnAction();
    testDisconnectedPadIsIgnored();
    testKeyAxisIsBipolar();
    testStickDeadzone();
    testDeadzoneRescalesRatherThanClipping();
    testPadAxisCanBeInverted();
    testStrongestSourceWinsRatherThanSumming();
    testUnknownNamesAreInertNotFatal();
    testMouseDeltaIsPerFrame();
    testDefaultBindingsCoverTheBasics();
    testRebindingAddsRatherThanReplaces();
    testOutOfRangeCodesAreRejected();

    testTheCursorModeIsARequestUntilSomethingVetoesIt();
    testLosingTheWindowIsAlwaysAWayOut();
    testCapturingTheMouseDoesNotSnapTheView();
    testReleasingTheMouseDoesNotSnapEither();
    testAVetoedCaptureAlsoRebasesTheDelta();

    testTypedCharactersArriveAndDoNotLinger();
    testTypingDoesNotAlsoWalkThePlayer();
    testAPadStillDrivesTheGameWhileANameIsTyped();
    testTakingTheKeyboardIsAnEdgeLikeAnyOther();
    testAFingerThatJustLandedBegins();
    testAFingerThatStaysDownMovesAndCarriesItsDelta();
    testAFingerThatHasNotMovedIsStillMovedRatherThanStationary();
    testALiftedFingerIsReportedOnceAndThenGone();
    testTwoFingersAreTwoContactsWithTheirOwnDeltas();
    testASecondFingerBeginsWhileTheFirstKeepsMoving();
    testAnIdIsFollowedRatherThanAPositionInTheList();
    testAnUnknownIdIsAbsentRatherThanWrong();
    testAFrameWithNoContactsChangesNothingElse();
    testACountLargerThanTheFilledSlotsProducesNoPhantomFinger();
    testACountBeyondTheArrayIsClampedRatherThanReadPast();
    testTheMouseIsContactZeroForAsLongAsItIsHeld();
}

TEST_MAIN("test_input", 88)
