// The control scheme and hero-mode routing, against the original's harness.
//
// `tools/verify_hero.gd` section A resolves the scheme and section D routes
// the pointer in hero mode; section G adds the Q and E keys. To re-derive:
//
//   cd %USERPROFILE%\Desktop\The-Wolf-Brigade
//   tools\godot.bat --headless --path . res://tools/verify_hero.tscn
//
// It printed, at the game's 50741d1 on 10 September 2026 (72 passed, 0
// failed), among the rest:
//
//   ok  : explicit desktop pref resolves desktop
//   ok  : explicit touch pref resolves touch
//   ok  : auto resolves desktop on a desktop OS
//   ok  : auto label spells out detection
//   ok  : hero mode desktop: left click -> strike, not select
//   ok  : hero mode: right click / touch tap are inert (permanent possession)
//   ok  : hero mode: Esc -> menu-back (deselect signal)
//   ok  : hero mode touch: taps and clicks stay inert
//   ok  : after hero mode, a tap is a context tap again
//   ok  : placement mode outranks hero mode
//   ok  : Q outside hero mode is inert
//   ok  : hero mode: Q -> slot 0, E -> slot 1
//
// A's other two - the hero is controllable and the worker is not - are data,
// and test_wb_hero has them. The coordinates are the harness's own: a click at
// (600, 400), a tap at (700, 500), a tap at (800, 500) after hero mode and a
// placement click at (900, 500).
//
// The harness drives Godot's InputController with synthetic events. Here the
// router answers the same events directly, and the one claim that needs more
// than the router - a tap outside hero mode is a context tap - is followed
// through the real GestureMachine rather than asserted of a pass-through flag.

#include "TestHarness.hpp"

#include "core/Input.hpp"
#include "sim/ControlScheme.hpp"
#include "sim/GestureMachine.hpp"
#include "sim/HeroInput.hpp"
#include "sim/Progression.hpp"

#include <cmath>
#include <string>

using namespace WolfBrigade;
using Event = HeroInput::Event;

namespace {

Event press(int button, const glm::vec2& at) {
    Event event;
    event.kind = Event::Kind::MouseButton;
    event.code = button;
    event.position = at;
    event.pressed = true;
    return event;
}

Event release(int button, const glm::vec2& at) {
    Event event = press(button, at);
    event.pressed = false;
    return event;
}

Event touch(const glm::vec2& at, bool down) {
    Event event;
    event.kind = Event::Kind::Touch;
    event.position = at;
    event.pressed = down;
    return event;
}

Event key(int code) {
    Event event;
    event.kind = Event::Kind::Key;
    event.code = code;
    return event;
}

// Nothing happened: the harness's "inert", which it checks as every signal
// list staying empty.
bool inert(const HeroInput::Intents& intents) {
    return !intents.attackAt && intents.ability < 0 && !intents.deselect &&
           !intents.placementConfirm && !intents.placementCancel && !intents.passThrough;
}

HeroInput heroMode(const char* scheme) {
    HeroInput input;
    input.SetHeroMode(true, scheme);
    return input;
}

// --- A. The scheme ----------------------------------------------------------

void testAnExplicitPreferenceResolvesToItself() {
    CHECK_MSG(ControlScheme::Resolve(ControlScheme::kDesktop, false) == ControlScheme::kDesktop,
              "explicit desktop pref resolves desktop");
    CHECK_MSG(ControlScheme::Resolve(ControlScheme::kTouch, false) == ControlScheme::kTouch,
              "explicit touch pref resolves touch");

    // And on a phone, which the harness cannot reach: an explicit choice is
    // never overridden by detection.
    CHECK(ControlScheme::Resolve(ControlScheme::kDesktop, true) == ControlScheme::kDesktop);
    CHECK(ControlScheme::Resolve(ControlScheme::kTouch, true) == ControlScheme::kTouch);
}

void testAutoDetectsThePlatform() {
    CHECK_MSG(ControlScheme::Resolve(ControlScheme::kAuto, false) == ControlScheme::kDesktop,
              "auto resolves desktop on a desktop OS");
    CHECK_MSG(ControlScheme::Resolve(ControlScheme::kAuto, true) == ControlScheme::kTouch,
              "and touch on a mobile one");

    // Anything that is not an explicit choice is detected: a never-chosen
    // preference, and a value a hand-edited save invented.
    CHECK(ControlScheme::Resolve("", false) == ControlScheme::kDesktop);
    CHECK(ControlScheme::Resolve("gamepad", true) == ControlScheme::kTouch);
}

void testTheAutoLabelSpellsOutWhatItDetected() {
    const std::string desktopAuto = ControlScheme::Label(ControlScheme::kAuto, false);
    CHECK_MSG(desktopAuto.rfind("Auto", 0) == 0, "auto label spells out detection");
    CHECK(desktopAuto == "Auto (PC)");
    CHECK(ControlScheme::Label(ControlScheme::kAuto, true) == "Auto (Touch)");
    CHECK(ControlScheme::Label(ControlScheme::kDesktop, true) == "PC");
    CHECK(ControlScheme::Label(ControlScheme::kTouch, false) == "Touch");
}

void testTheProfilesPreferenceIsWhatResolves() {
    // Every machine this suite runs on is a desktop one, which is what makes
    // the harness's "auto resolves desktop" deterministic there too.
    CHECK_MSG(!ControlScheme::kMobilePlatform, "this suite runs on a desktop OS");

    Profile chosen;
    chosen.SetControlScheme(ControlScheme::kTouch);
    CHECK(ControlScheme::Resolved(chosen) == ControlScheme::kTouch);

    Profile never;
    CHECK_MSG(ControlScheme::Resolved(never) == ControlScheme::kDesktop,
              "a profile that never chose is auto, and auto is desktop here");
}

// --- D. Routing in hero mode ------------------------------------------------

void testALeftClickStrikesRatherThanSelects() {
    const HeroInput input = heroMode(ControlScheme::kDesktop);

    const HeroInput::Intents clicked = input.Route(press(Supersonic::MouseButton::Left, {600.0f, 400.0f}));
    CHECK_MSG(clicked.attackAt && !clicked.passThrough,
              "hero mode desktop: left click -> strike, not select");
    CHECK(clicked.attackPosition == glm::vec2(600.0f, 400.0f));

    // One strike per click: the release that follows is nothing.
    CHECK(inert(input.Route(release(Supersonic::MouseButton::Left, {600.0f, 400.0f}))));
}

void testARightClickAndATapAreInert() {
    const HeroInput input = heroMode(ControlScheme::kDesktop);

    const bool rightInert =
        inert(input.Route(press(Supersonic::MouseButton::Right, {600.0f, 400.0f}))) &&
        inert(input.Route(release(Supersonic::MouseButton::Right, {600.0f, 400.0f})));
    const bool tapInert = inert(input.Route(touch({700.0f, 500.0f}, true))) &&
                          inert(input.Route(touch({700.0f, 500.0f}, false)));
    CHECK_MSG(rightInert && tapInert,
              "hero mode: right click / touch tap are inert (permanent possession)");
}

void testEscapeIsMenuBack() {
    const HeroInput::Intents escaped = heroMode(ControlScheme::kDesktop).Route(key(Supersonic::Key::Escape));
    CHECK_MSG(escaped.deselect, "hero mode: Esc -> menu-back (deselect signal)");
    CHECK(escaped.ability < 0 && !escaped.passThrough);
}

void testOnTheTouchSchemeTapsAndClicksStayInert() {
    const HeroInput input = heroMode(ControlScheme::kTouch);
    CHECK(input.TouchScheme());

    const bool tapInert = inert(input.Route(touch({700.0f, 500.0f}, true))) &&
                          inert(input.Route(touch({700.0f, 500.0f}, false)));
    const bool clickInert =
        inert(input.Route(press(Supersonic::MouseButton::Left, {600.0f, 400.0f})));
    CHECK_MSG(tapInert && clickInert, "hero mode touch: taps and clicks stay inert");
}

void testLeavingHeroModeATapIsAContextTapAgain() {
    HeroInput input = heroMode(ControlScheme::kTouch);
    input.SetHeroMode(false, ControlScheme::kTouch);

    // Followed through the machine it passes to, so the claim is the one the
    // harness makes - a context tap - rather than a flag.
    GestureMachine gestures;
    const glm::vec2 at(800.0f, 500.0f);
    CHECK(input.Route(touch(at, true)).passThrough);
    CHECK(input.Route(touch(at, false)).passThrough);

    Supersonic::Contact down;
    down.id = 0;
    down.position = at;
    down.phase = Supersonic::ContactPhase::Began;
    gestures.Step(&down, 1, 0.0f);

    Supersonic::Contact up = down;
    up.phase = Supersonic::ContactPhase::Ended;
    const GestureMachine::Intents tapped = gestures.Step(&up, 1, 0.05f);

    CHECK_MSG(tapped.contextTap, "after hero mode, a tap is a context tap again");
    CHECK(tapped.tapPosition == at);
}

void testPlacementOutranksHeroMode() {
    HeroInput input = heroMode(ControlScheme::kDesktop);
    input.SetPlacementMode(true);

    const HeroInput::Intents clicked = input.Route(press(Supersonic::MouseButton::Left, {900.0f, 500.0f}));
    CHECK_MSG(clicked.placementConfirm && !clicked.attackAt, "placement mode outranks hero mode");
    CHECK(clicked.placementPosition == glm::vec2(900.0f, 500.0f));
}

// --- G. The ability keys ----------------------------------------------------

void testQAndECastOnlyInHeroMode() {
    HeroInput outside;
    const HeroInput::Intents q = outside.Route(key(Supersonic::Key::Q));
    CHECK_MSG(q.ability < 0, "Q outside hero mode is inert");
    CHECK_MSG(q.passThrough, "and is left to the ordinary pointer, which binds no Q");

    const HeroInput input = heroMode(ControlScheme::kDesktop);
    CHECK_MSG(input.Route(key(Supersonic::Key::Q)).ability == 0 &&
                  input.Route(key(Supersonic::Key::E)).ability == 1,
              "hero mode: Q -> slot 0, E -> slot 1");
}

// --- Added by the port --------------------------------------------------------

// Placement's own three, which the harness reaches only through its one click:
// a secondary click and Escape both cancel, and a touch is the gesture path's.
void testPlacementCancelsOnASecondaryClickOrEscape() {
    HeroInput input = heroMode(ControlScheme::kDesktop);
    input.SetPlacementMode(true);

    CHECK(input.Route(press(Supersonic::MouseButton::Right, {900.0f, 500.0f})).placementCancel);
    const HeroInput::Intents escaped = input.Route(key(Supersonic::Key::Escape));
    CHECK_MSG(escaped.placementCancel && !escaped.deselect,
              "Escape cancels the placement rather than backing out of a menu");
    CHECK(input.Route(touch({900.0f, 500.0f}, true)).passThrough);
    CHECK(inert(input.Route(release(Supersonic::MouseButton::Right, {900.0f, 500.0f}))));
}

void testOutsideHeroModeOnlyEscapeIsAnswered() {
    HeroInput input;
    CHECK_MSG(input.Route(key(Supersonic::Key::Escape)).deselect,
              "the ordinary pointer's one key: Escape deselects");
    CHECK(input.Route(press(Supersonic::MouseButton::Left, {600.0f, 400.0f})).passThrough);
}

// The scheme is cached at entry, which is why the harness re-enters hero mode
// to switch schemes. Leaving turns the mode off and keeps nothing else live.
void testTheSchemeIsTakenWhenHeroModeIsEntered() {
    HeroInput input;
    input.SetHeroMode(true, ControlScheme::kTouch);
    CHECK(input.TouchScheme());
    input.SetHeroMode(true, ControlScheme::kDesktop);
    CHECK(!input.TouchScheme());
    input.SetHeroMode(false, ControlScheme::kTouch);
    CHECK(!input.HeroMode());
}

// `_keyboard_dir` and the shaping `_process` gives it for the hero.
void testKeysSteerAndADiagonalIsNoFaster() {
    CHECK(HeroInput::SteerFromKeys(false, false, false, false) == glm::vec2(0.0f));
    CHECK_MSG(HeroInput::SteerFromKeys(false, true, false, false) == glm::vec2(1.0f, 0.0f),
              "a single key stays exactly one");
    CHECK_MSG(HeroInput::SteerFromKeys(true, true, false, false) == glm::vec2(0.0f),
              "opposite keys cancel");
    CHECK_MSG(HeroInput::SteerFromKeys(false, false, true, false) == glm::vec2(0.0f, -1.0f),
              "up is toward the back of the band, y down the screen");

    // Godot's normalise divides by the length in single precision, so each
    // component is exactly 1/sqrt(2) as a float divides it.
    const glm::vec2 diagonal = HeroInput::SteerFromKeys(false, true, false, true);
    const float expected = 1.0f / std::sqrt(2.0f);
    CHECK_EQ(diagonal.x, expected);
    CHECK_EQ(diagonal.y, expected);
}

void runTests() {
    testAnExplicitPreferenceResolvesToItself();
    testAutoDetectsThePlatform();
    testTheAutoLabelSpellsOutWhatItDetected();
    testTheProfilesPreferenceIsWhatResolves();

    testALeftClickStrikesRatherThanSelects();
    testARightClickAndATapAreInert();
    testEscapeIsMenuBack();
    testOnTheTouchSchemeTapsAndClicksStayInert();
    testLeavingHeroModeATapIsAContextTapAgain();
    testPlacementOutranksHeroMode();

    testQAndECastOnlyInHeroMode();

    testPlacementCancelsOnASecondaryClickOrEscape();
    testOutsideHeroModeOnlyEscapeIsAnswered();
    testTheSchemeIsTakenWhenHeroModeIsEntered();
    testKeysSteerAndADiagonalIsNoFaster();
}

} // namespace

// Every check here runs on every machine - none needs a device, a window or a
// file - so the floor is the whole count, measured under GCC on 10 September.
TEST_MAIN("test_wb_controls", 48)
