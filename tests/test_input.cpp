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
#include <string>

using namespace Supersonic;

// Feeds one frame of state.
static void frame(const RawInputState& state) {
    Input::Update(state);
}

static RawInputState withKey(int key) {
    RawInputState state{};
    state.keys[key] = true;
    return state;
}

static void testActionRespondsToItsKey() {
    Input::ClearBindings();
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
    Input::ClearBindings();
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
    Input::ClearBindings();
    Input::BindActionKey("Fire", Key::F);

    frame(withKey(Key::F));
    CHECK_MSG(Input::IsDown("Fire"), "the key is genuinely down");
    CHECK_MSG(!Input::WasPressed("Fire"), "but the first frame must not invent an edge");
}

static void testAnySourceSatisfiesAnAction() {
    Input::ClearBindings();
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
    Input::ClearBindings();
    Input::BindActionPadButton("Fire", Pad::A);

    RawInputState state{};
    state.padConnected = false;
    state.padButtons[Pad::A] = true;
    frame(state);

    CHECK_MSG(!Input::IsDown("Fire"), "a pad that is not connected cannot press anything");
}

static void testKeyAxisIsBipolar() {
    Input::ClearBindings();
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
    Input::ClearBindings();
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
    Input::ClearBindings();
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
    Input::ClearBindings();
    Input::BindAxisPad("MoveY", Pad::LeftY, -1.0f);

    RawInputState state{};
    state.padConnected = true;
    state.padAxes[Pad::LeftY] = -1.0f;   // stick pushed up
    frame(state);

    CHECK_NEAR(Input::GetAxis("MoveY"), 1.0f);
}

static void testStrongestSourceWinsRatherThanSumming() {
    Input::ClearBindings();
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
    Input::ClearBindings();
    frame(RawInputState{});

    CHECK_MSG(!Input::IsDown("NoSuchAction"), "an unbound action is simply never down");
    CHECK_MSG(!Input::WasPressed("NoSuchAction"), "and never pressed");
    CHECK_NEAR(Input::GetAxis("NoSuchAxis"), 0.0f);
}

static void testMouseDeltaIsPerFrame() {
    Input::ClearBindings();

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
    Input::ClearBindings();
    Input::BindActionKey("Fire", Key::F);
    Input::BindActionKey("Fire", Key::Enter);

    frame(RawInputState{});
    frame(withKey(Key::F));
    CHECK_MSG(Input::IsDown("Fire"), "the original binding still works");

    frame(withKey(Key::Enter));
    CHECK_MSG(Input::IsDown("Fire"), "and the added one does too");
}

static void testOutOfRangeCodesAreRejected() {
    Input::ClearBindings();
    Input::BindActionKey("Bad", 99999);
    Input::BindActionKey("Bad", -5);
    Input::BindActionPadButton("Bad", 400);
    Input::BindActionMouseButton("Bad", 99);
    Input::BindAxisPad("BadAxis", 77);

    frame(RawInputState{});
    CHECK_MSG(!Input::IsDown("Bad"), "a nonsense binding must not index out of bounds");
    CHECK_NEAR(Input::GetAxis("BadAxis"), 0.0f);
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
}

TEST_MAIN("test_input", 30)
