// Tests for the seam between the ported simulation and the picture.
//
// Every other port suite checks the simulation: that a worker gathers the same
// wood the original's harness printed, that a wave spawns the same enemies. The
// LAYER is what makes any of that visible, and nothing checked it - it lived
// inside an executable behind an option that is off by default, so it was the
// one piece of the port that could not be linked, let alone run.
//
// The screenshot tool does not cover it either, and it is worth saying exactly
// why rather than leaving the next person to rediscover it: --screenshot writes
// `offscreen.GetPresentedImage()`, the offscreen 3D colour target, while a game
// composites that image into ImGui with ImGui::Image and draws the HUD OVER it
// in the window's draw list (EditorLayer.cpp). The UI is therefore in the
// swapchain and never in the captured image. A picture of a running game shows
// the lane and cannot show a single label, in either direction - so "the HUD
// did not appear in the screenshot" is not evidence, and this file is the
// instrument that reaches the question instead.
//
// No Vulkan, no window: a layer is handed a registry and called, which is the
// whole of the seam EngineLayer.hpp argues for.

#include "TestHarness.hpp"

#include "WolfBrigadeLayer.hpp"

#include "core/Components.hpp"

#include "sim/GameState.hpp"
#include "sim/Match.hpp"
#include "sim/WaveDirector.hpp"

#include <string>

using namespace Supersonic;
using WolfBrigade::WolfBrigadeLayer;
using WolfBrigade::Match;

namespace {

// The tick the scene authors for this game.
constexpr float kTick = 1.0f / 30.0f;

// Finds the one entity carrying `tag`, or null.
entt::entity byTag(const entt::registry& registry, const std::string& tag) {
    for (auto [entity, name] : registry.view<const TagComponent>().each()) {
        if (name.tag == tag) return entity;
    }
    return entt::null;
}

std::string textOf(const entt::registry& registry, const std::string& tag) {
    const entt::entity entity = byTag(registry, tag);
    if (entity == entt::null) return {};
    const auto* text = registry.try_get<UITextComponent>(entity);
    return text ? text->text : std::string{};
}

// True when `haystack` contains `needle`.
bool has(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

// The wiring, end to end: attaching the layer puts a HUD in the registry.
static void testAttachingTheLayerBuildsTheHud() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);

    CHECK_MSG(byTag(registry, "HUD Wood") != entt::null, "the wood counter exists");
    CHECK_MSG(byTag(registry, "HUD Food") != entt::null, "the food counter exists");
    CHECK_MSG(byTag(registry, "HUD Wave") != entt::null, "the wave counter exists");

    const entt::entity pause = byTag(registry, "HUD Pause");
    CHECK_MSG(pause != entt::null, "the pause button exists");
    if (pause != entt::null) {
        CHECK_MSG(registry.all_of<UIButtonComponent>(pause),
                  "and it is a button rather than a label, or nothing can click it");
    }

    layer.OnDetach(registry);
}

// The numbers on screen are the SIMULATION's numbers, not placeholders.
//
// This is the check the whole file exists for. A HUD that renders beautifully
// and shows a constant is the failure mode a screenshot would pass.
static void testTheHudReadsTheSimulation() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);

    layer.OnFixedUpdate(registry, kTick);

    const Match* match = layer.CurrentMatch();
    CHECK_MSG(match != nullptr, "the layer booted a match at all");
    if (match == nullptr) { layer.OnDetach(registry); return; }

    const int wood = match->Run().Amount("wood");
    const int food = match->Run().Amount("food");

    CHECK_MSG(has(textOf(registry, "HUD Wood"), "Wood: " + std::to_string(wood)),
              "the wood label shows what the run actually holds, got \"" +
                  textOf(registry, "HUD Wood") + "\"");
    CHECK_MSG(has(textOf(registry, "HUD Food"), "Food: " + std::to_string(food)),
              "the food label shows what the run actually holds, got \"" +
                  textOf(registry, "HUD Food") + "\"");
    CHECK_MSG(has(textOf(registry, "HUD Wave"), "Wave " +
                                                    std::to_string(match->Run().CurrentWave())),
              "the wave label shows the run's wave, got \"" +
                  textOf(registry, "HUD Wave") + "\"");

    layer.OnDetach(registry);
}

// A counter that never moves is the same bug as one that reads nothing.
//
// The workers start gathering immediately, so wood climbs within a few seconds
// of simulated time. Driving the tick rather than asserting a literal keeps
// this a test of the WIRING - if the economy is retuned the number changes and
// this still passes, but if the label stops tracking the run it fails.
static void testTheHudFollowsTheRunAsItGoes() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);

    layer.OnFixedUpdate(registry, kTick);
    const std::string first = textOf(registry, "HUD Wood");

    const Match* match = layer.CurrentMatch();
    if (match == nullptr) { CHECK_MSG(false, "no match"); layer.OnDetach(registry); return; }
    const int startingWood = match->Run().Amount("wood");

    // Sixty seconds of game time. Long enough for a worker to make several
    // round trips at any plausible tuning.
    for (int i = 0; i < 30 * 60; ++i) layer.OnFixedUpdate(registry, kTick);

    const int laterWood = match->Run().Amount("wood");
    CHECK_MSG(laterWood != startingWood,
              "the simulation itself moved, or this test proves nothing about the label");

    const std::string later = textOf(registry, "HUD Wood");
    CHECK_MSG(later != first, "and the label moved with it, from \"" + first + "\" to \"" +
                                  later + "\"");
    CHECK_MSG(has(later, "Wood: " + std::to_string(laterWood)),
              "to the value the run now holds");

    layer.OnDetach(registry);
}

// Pause stops the game and not the HUD.
//
// Clicked through the TICK-latched flag, which is the one a game reads: a
// click is held from the frame that produced it until exactly one tick takes
// it. Setting `clicked` instead would test the frame's answer and pass while
// the thing a game actually reads stayed broken.
static void testThePauseButtonStopsTheSimulation() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);

    layer.OnFixedUpdate(registry, kTick);

    const Match* match = layer.CurrentMatch();
    if (match == nullptr) { CHECK_MSG(false, "no match"); layer.OnDetach(registry); return; }

    const entt::entity pause = byTag(registry, "HUD Pause");
    if (pause == entt::null) { CHECK_MSG(false, "no pause button"); layer.OnDetach(registry); return; }

    registry.get<UIButtonComponent>(pause).clickedThisTick = true;
    layer.OnFixedUpdate(registry, kTick);
    registry.get<UIButtonComponent>(pause).clickedThisTick = false;

    CHECK_MSG(registry.get<UIButtonComponent>(pause).label == "Resume",
              "the button says how to undo what it just did");

    // Elapsed time is the cleanest black-box witness that the match is not
    // stepping: it advances on every step and on nothing else.
    const double stopped = match->Director().Elapsed();
    for (int i = 0; i < 30 * 5; ++i) layer.OnFixedUpdate(registry, kTick);
    CHECK_MSG(match->Director().Elapsed() == stopped,
              "five seconds of ticks moved the match while it was paused");

    // And back again, or pause is a trap rather than a pause.
    registry.get<UIButtonComponent>(pause).clickedThisTick = true;
    layer.OnFixedUpdate(registry, kTick);
    registry.get<UIButtonComponent>(pause).clickedThisTick = false;

    CHECK_MSG(registry.get<UIButtonComponent>(pause).label == "Pause",
              "the label went back");

    for (int i = 0; i < 30 * 5; ++i) layer.OnFixedUpdate(registry, kTick);
    CHECK_MSG(match->Director().Elapsed() > stopped, "and the match is running again");

    layer.OnDetach(registry);
}

// The HUD is built ONCE and afterwards only written to.
//
// Not a style check. A button's `pressed` flag lives on its component and a
// click is the transition off it, so a screen rebuilt from the simulation every
// tick hands the release to a component that was never pressed and no button
// ever fires - see testAButtonRecreatedMidGestureDoesNotFire in test_uiinput,
// which pins the engine half of this. This is the caller half: if someone
// later moves buildHud into the tick, the entity identity changes and this
// fails, instead of the game quietly losing every click.
static void testTheHudIsNotRebuiltEveryTick() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);

    layer.OnFixedUpdate(registry, kTick);
    const entt::entity pauseFirst = byTag(registry, "HUD Pause");
    const entt::entity woodFirst = byTag(registry, "HUD Wood");

    for (int i = 0; i < 120; ++i) layer.OnFixedUpdate(registry, kTick);

    CHECK_MSG(byTag(registry, "HUD Pause") == pauseFirst,
              "the pause button is the same entity 120 ticks later, or every click "
              "spanning a tick boundary is lost");
    CHECK_MSG(byTag(registry, "HUD Wood") == woodFirst,
              "and so is the wood label");

    layer.OnDetach(registry);
}

static void runTests() {
    testAttachingTheLayerBuildsTheHud();
    testTheHudReadsTheSimulation();
    testTheHudFollowsTheRunAsItGoes();
    testThePauseButtonStopsTheSimulation();
    testTheHudIsNotRebuiltEveryTick();
}

TEST_MAIN("test_wb_hud", 18)
