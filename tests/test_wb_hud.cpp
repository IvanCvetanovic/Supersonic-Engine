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
#include "core/Application.hpp"
#include "core/UISystem.hpp"

#include <imgui.h>

#include "sim/GameState.hpp"
#include "sim/Building.hpp"
#include "sim/BuildPlacement.hpp"
#include "sim/Selection.hpp"
#include "sim/Match.hpp"
#include "sim/WaveDirector.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace Supersonic;
using WolfBrigade::WolfBrigadeLayer;
using WolfBrigade::Match;
using WolfBrigade::Building;
using WolfBrigade::Cost;

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

// AND IT ACTUALLY REACHES A DRAW CALL, ON SCREEN.
//
// Everything above proves the HUD holds the right numbers. It does not prove a
// single pixel of it is drawn, and those are different failures: a label
// anchored off the edge, or one the draw pass skips, holds exactly the right
// text and is invisible.
//
// The screenshot cannot answer this (see the note at the top of this file), so
// the measurement is the one test_uilayer established - run ImGui with no
// graphics backend at all and read the vertex buffer it produced. That is the
// real draw call rather than a proxy for it.
//
// The elements are recoloured first so each can be told from the others by its
// vertices. Colour is not what is under test; identity is.
namespace {

constexpr ImU32 kWoodInk = IM_COL32(255, 0, 0, 255);
constexpr ImU32 kFoodInk = IM_COL32(0, 255, 0, 255);
constexpr ImU32 kWaveInk = IM_COL32(0, 0, 255, 255);
constexpr ImU32 kPauseInk = IM_COL32(255, 0, 255, 255);

glm::vec4 toVec4(ImU32 color) {
    return glm::vec4(static_cast<float>((color >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
                     static_cast<float>((color >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
                     static_cast<float>((color >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
                     static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
}

struct Bounds {
    int vertices{0};
    float minX{1e9f}, minY{1e9f}, maxX{-1e9f}, maxY{-1e9f};
    void add(float x, float y) {
        ++vertices;
        minX = std::min(minX, x); minY = std::min(minY, y);
        maxX = std::max(maxX, x); maxY = std::max(maxY, y);
    }
};

} // namespace

static void testTheHudIsActuallyDrawnAndOnScreen() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    layer.OnFixedUpdate(registry, kTick);

    registry.get<UITextComponent>(byTag(registry, "HUD Wood")).color = toVec4(kWoodInk);
    registry.get<UITextComponent>(byTag(registry, "HUD Food")).color = toVec4(kFoodInk);
    registry.get<UITextComponent>(byTag(registry, "HUD Wave")).color = toVec4(kWaveInk);
    auto& pause = registry.get<UIButtonComponent>(byTag(registry, "HUD Pause"));
    pause.color = toVec4(kPauseInk);
    pause.cornerRadius = 0.0f;

    // The drop shadow would draw the same glyphs a second time in another
    // colour, which is a second bounding box for the same label.
    for (const char* tag : { "HUD Wood", "HUD Food", "HUD Wave" }) {
        registry.get<UITextComponent>(byTag(registry, tag)).shadow = false;
    }

    const float width = 1920.0f;
    const float height = 1080.0f;

    ImGui::CreateContext();
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(width, height);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.IniFilename = nullptr;
    }

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("game", nullptr, ImGuiWindowFlags_NoDecoration);

    UICanvas::UIPointer pointer;
    // Off screen: a hovered button recolours itself, and this identifies it by
    // colour.
    pointer.position = glm::vec2(-4000.0f, -4000.0f);
    UICanvas::UIKeyboard keyboard;
    UISystem::Render(registry, UIRect{glm::vec2(0.0f), glm::vec2(width, height)},
                     pointer, keyboard, glm::mat4(1.0f));

    ImGui::End();
    ImGui::Render();

    Bounds wood, food, wave, button;
    const ImDrawData* data = ImGui::GetDrawData();
    for (int list = 0; data != nullptr && list < data->CmdListsCount; ++list) {
        const ImDrawList* commands = data->CmdLists[list];
        for (int v = 0; v < commands->VtxBuffer.Size; ++v) {
            const ImDrawVert& vertex = commands->VtxBuffer[v];
            Bounds* target = nullptr;
            if (vertex.col == kWoodInk) target = &wood;
            else if (vertex.col == kFoodInk) target = &food;
            else if (vertex.col == kWaveInk) target = &wave;
            else if (vertex.col == kPauseInk) target = &button;
            if (target != nullptr) target->add(vertex.pos.x, vertex.pos.y);
        }
    }
    ImGui::DestroyContext();

    const struct { const char* name; const Bounds* bounds; } drawn[] = {
        { "the wood counter", &wood }, { "the food counter", &food },
        { "the wave counter", &wave }, { "the pause button", &button },
    };

    for (const auto& element : drawn) {
        CHECK_MSG(element.bounds->vertices > 0,
                  std::string(element.name) + " produced no geometry at all - it holds "
                  "the right text and nothing draws it");
        if (element.bounds->vertices == 0) continue;

        CHECK_MSG(element.bounds->minX >= -0.5f && element.bounds->maxX <= width + 0.5f &&
                      element.bounds->minY >= -0.5f && element.bounds->maxY <= height + 0.5f,
                  std::string(element.name) + " drew outside the screen: x " +
                      std::to_string(element.bounds->minX) + ".." +
                      std::to_string(element.bounds->maxX) + ", y " +
                      std::to_string(element.bounds->minY) + ".." +
                      std::to_string(element.bounds->maxY));
    }

    // The three corners the original puts them in, so a label that drifted to
    // the wrong side of the screen is caught rather than merely being on it.
    CHECK_MSG(wood.maxX < width * 0.5f, "the wood counter is on the left");
    CHECK_MSG(food.minY > wood.minY, "the food counter sits below the wood one");
    CHECK_MSG(wave.minX > width * 0.5f, "the wave counter is on the right");
    CHECK_MSG(button.minX > wood.maxX && button.maxX < wave.minX,
              "and the pause button is between them");

    layer.OnDetach(registry);
}

// ---- The contextual bottom bar ----------------------------------------
//
// The bar is where the rebuild constraint actually bites, so these check the
// SPLIT rather than the buttons: which changes recreate entities and which do
// not. A bar that rebuilt on every affordability change would pass every
// "shows the right buttons" test and lose every click.

static std::vector<entt::entity> barButtons(const entt::registry& registry) {
    std::vector<entt::entity> found;
    for (auto [entity, tag] : registry.view<const TagComponent>().each()) {
        if (tag.tag == "WB Bar Button") found.push_back(entity);
    }
    return found;
}

// With nothing selected the bar is the Build menu, one button per buildable.
static void testTheBarOffersTheBuildMenuByDefault() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    layer.OnFixedUpdate(registry, kTick);

    const auto buttons = barButtons(registry);
    CHECK_MSG(!buttons.empty(), "the build menu has buttons in it");

    bool sawBuild = false;
    for (entt::entity button : buttons) {
        if (has(registry.get<UIButtonComponent>(button).label, "Build ")) sawBuild = true;
    }
    CHECK_MSG(sawBuild, "and they are Build buttons");

    // Every one carries its cost, which is the half a player needs to decide.
    for (entt::entity button : buttons) {
        const std::string label = registry.get<UIButtonComponent>(button).label;
        CHECK_MSG(label.find('(') != std::string::npos,
                  "the button says what it costs: \"" + label + "\"");
    }

    layer.OnDetach(registry);
}

// THE ONE THAT MATTERS: affordability flips must not recreate anything.
//
// Workers deposit wood constantly, so buttons cross the affordable line in
// both directions all through a run. If that recreated them, a press held
// across a single deposit would be lost - and deposits are frequent enough
// that this would be most presses.
//
// THE TREASURY IS DRAINED FIRST, and that is the whole test rather than
// set-up. A run starts with 300 wood against a 150-wood barracks and a
// 120-wood tower, and wood only ever goes UP as workers gather - so an earlier
// draft that simply ran for sixty seconds never crossed the line at all. It
// passed, and it went on passing when affordability was deliberately folded
// into the rebuild signature, which is the exact bug it exists to catch. A
// test that cannot fail is worse than no test, because it is counted.
static void testAffordabilityChangesDoNotRebuildTheBar() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    layer.OnFixedUpdate(registry, kTick);

    Match* match = layer.CurrentMatch();
    if (match == nullptr) { CHECK_MSG(false, "no match"); layer.OnDetach(registry); return; }

    CHECK_MSG(match->Run().TrySpend(Cost{{"wood", match->Run().Amount("wood")}}),
              "drained the treasury so the buttons start out of reach");
    layer.OnFixedUpdate(registry, kTick);

    const auto before = barButtons(registry);
    CHECK_MSG(!before.empty(), "there is a bar to hold still");

    int disabledBefore = 0;
    for (entt::entity button : before) {
        if (!registry.get<UIButtonComponent>(button).enabled) ++disabledBefore;
    }
    CHECK_MSG(disabledBefore > 0, "and something on it is unaffordable to begin with");

    // Now let the workers earn it back, and watch for the crossing.
    bool flipped = false;
    for (int i = 0; i < 30 * 240 && !flipped; ++i) {
        layer.OnFixedUpdate(registry, kTick);
        int disabledNow = 0;
        for (entt::entity button : barButtons(registry)) {
            if (!registry.get<UIButtonComponent>(button).enabled) ++disabledNow;
        }
        flipped = disabledNow < disabledBefore;
    }

    CHECK_MSG(flipped,
              "a button really did become affordable again, or nothing below is being "
              "measured; wood reached " + std::to_string(match->Run().Amount("wood")));

    const auto after = barButtons(registry);
    CHECK_MSG(after == before,
              "the bar's buttons are the SAME entities across an affordability flip - "
              "recreating them loses any press in flight");

    layer.OnDetach(registry);
}

// And the set DOES change when the thing it depends on changes.
//
// The control for the case above: a bar that never rebuilt at all would also
// hold its entities still, and would show the wrong buttons forever.
static void testStartingAPlacementRebuildsTheBarToCancel() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    layer.OnFixedUpdate(registry, kTick);

    const auto before = barButtons(registry);
    CHECK_MSG(!before.empty(), "there is a build button to press");
    if (before.empty()) { layer.OnDetach(registry); return; }

    // Pressed through the tick-latched flag, exactly as a player reaches it.
    //
    // And NOT cleared afterwards, because by then it does not exist: starting a
    // placement replaces the whole strip, so the button that was pressed is
    // destroyed inside the same tick that consumed its click. Reaching for it
    // after the call is what an earlier draft of this test did, and entt
    // asserts on a destroyed entity - which surfaces as a modal dialog and a
    // run that never returns rather than as a failure.
    const entt::entity pressed = before.front();
    registry.get<UIButtonComponent>(pressed).clickedThisTick = true;
    layer.OnFixedUpdate(registry, kTick);
    CHECK_MSG(!registry.valid(pressed),
              "the pressed button is gone, which is why it must not be touched again");

    const Match* match = layer.CurrentMatch();
    if (match == nullptr) { CHECK_MSG(false, "no match"); layer.OnDetach(registry); return; }

    CHECK_MSG(match->Placement().IsActive(),
              "pressing Build started a placement, or the click never reached the game");

    const auto after = barButtons(registry);
    CHECK_MSG(after.size() == 1, "a placement replaces the whole strip with one button");
    if (after.size() == 1) {
        CHECK_MSG(registry.get<UIButtonComponent>(after.front()).label == "Cancel",
                  "and it is Cancel - on touch there is no Escape, so this is the only "
                  "way out of a placement");
    }

    layer.OnDetach(registry);
}

// A button nobody can afford is greyed, not hidden.
//
// bottom_bar.gd disables rather than removes, and the difference is not
// cosmetic: a button that vanishes when unaffordable moves every button beside
// it, so the one the player was reaching for is somewhere else by the time
// their finger lands.
//
// A run STARTS with 300 wood and 100 food against a 150-wood barracks and a
// 120-wood tower, so everything is affordable and an earlier draft of this test
// asserted otherwise and failed. The state has to be arranged: selecting the
// town hall turns the bar into its Train buttons, and a worker costs 50 wood,
// so six of them empty the treasury.
static void testUnaffordableButtonsAreDisabledRatherThanRemoved() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    layer.OnFixedUpdate(registry, kTick);

    Match* match = layer.CurrentMatch();
    if (match == nullptr) { CHECK_MSG(false, "no match"); layer.OnDetach(registry); return; }

    Building* hall = nullptr;
    for (const auto& building : match->Buildings()) {
        if (building->IsComplete()) { hall = building.get(); break; }
    }
    if (hall == nullptr) { CHECK_MSG(false, "no completed building"); layer.OnDetach(registry); return; }

    match->Picked().SelectBuilding(hall);
    layer.OnFixedUpdate(registry, kTick);

    auto buttons = barButtons(registry);
    CHECK_MSG(!buttons.empty(), "selecting the town hall gives it a bar");

    bool sawTrain = false;
    for (entt::entity button : buttons) {
        if (has(registry.get<UIButtonComponent>(button).label, "Train ")) sawTrain = true;
    }
    CHECK_MSG(sawTrain, "and it offers what the building trains");

    // Every button's enabled state IS the affordability question, which is the
    // rule rather than a symptom of the starting numbers.
    for (entt::entity button : buttons) {
        CHECK_MSG(registry.get<UIButtonComponent>(button).visible,
                  "every button is visible before anything is spent");
    }

    // Drain it. Pressing Train is the game's own path to spending, so this
    // exercises the click routing at the same time.
    for (int attempt = 0; attempt < 8; ++attempt) {
        buttons = barButtons(registry);
        entt::entity train = entt::null;
        for (entt::entity button : buttons) {
            if (has(registry.get<UIButtonComponent>(button).label, "Train ")) { train = button; break; }
        }
        if (train == entt::null) break;
        if (!registry.get<UIButtonComponent>(train).enabled) break;
        registry.get<UIButtonComponent>(train).clickedThisTick = true;
        layer.OnFixedUpdate(registry, kTick);
        if (registry.valid(train)) registry.get<UIButtonComponent>(train).clickedThisTick = false;
    }

    CHECK_MSG(match->Run().Amount("wood") < 50,
              "the treasury is actually empty, or nothing below is being tested; wood is " +
                  std::to_string(match->Run().Amount("wood")));

    buttons = barButtons(registry);
    bool sawDisabled = false;
    for (entt::entity button : buttons) {
        const auto& widget = registry.get<UIButtonComponent>(button);
        if (!widget.enabled) sawDisabled = true;
        CHECK_MSG(widget.visible,
                  "a button nobody can afford is greyed, not gone - removing it would "
                  "move every button beside it out from under the player's finger");
    }
    CHECK_MSG(sawDisabled, "and something really is unaffordable now");

    layer.OnDetach(registry);
}

// A disabled button does nothing when pressed.
//
// UIInput already refuses to mark a disabled button clicked, so this is the
// second lock: a button that was affordable when pressed and is not by the tick
// that consumes the click must not spend money the run no longer has.
static void testADisabledButtonDoesNotAct() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    layer.OnFixedUpdate(registry, kTick);

    Match* match = layer.CurrentMatch();
    if (match == nullptr) { CHECK_MSG(false, "no match"); layer.OnDetach(registry); return; }

    // Spend the treasury directly, so the bar is left holding buttons that were
    // affordable a moment ago - exactly the race this guards.
    const int wood = match->Run().Amount("wood");
    CHECK_MSG(match->Run().TrySpend(Cost{{"wood", wood}}), "drained the treasury");
    layer.OnFixedUpdate(registry, kTick);

    const auto buttons = barButtons(registry);
    entt::entity disabled = entt::null;
    for (entt::entity button : buttons) {
        if (!registry.get<UIButtonComponent>(button).enabled) { disabled = button; break; }
    }
    CHECK_MSG(disabled != entt::null, "there is a disabled button to press");
    if (disabled == entt::null) { layer.OnDetach(registry); return; }

    const bool placingBefore = match->Placement().IsActive();
    registry.get<UIButtonComponent>(disabled).clickedThisTick = true;
    layer.OnFixedUpdate(registry, kTick);

    CHECK_MSG(match->Placement().IsActive() == placingBefore,
              "pressing a disabled Build button started nothing");
    CHECK_MSG(match->Run().Amount("wood") == 0, "and spent nothing");

    layer.OnDetach(registry);
}

// ---- The pause overlay -------------------------------------------------

static entt::entity pauseButton(entt::registry& registry) { return byTag(registry, "HUD Pause"); }

// Opening it must not merely draw it - it must take the input.
//
// This is the regression the engine fix behind it exists for: a hidden stack
// used to leave its children with no rectangle, so they fell back to their own
// anchors and landed in a heap in the middle of the screen, invisible and still
// clickable over the running match. A closed pause menu that swallows clicks is
// a game that stops responding for no visible reason.
static void testTheClosedPauseMenuIsInvisibleAndInert() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    layer.OnFixedUpdate(registry, kTick);

    CHECK_MSG(!registry.get<UIStackComponent>(byTag(registry, "Pause Menu")).visible,
              "the overlay starts closed");
    CHECK_MSG(!registry.get<UIPanelComponent>(byTag(registry, "Pause Backdrop")).visible,
              "and so does the dim behind it");

    // The layout pass is the thing that used to strand them.
    const UICanvas::StackedLayout placed =
        UISystem::LayoutStacks(registry, UIRect{glm::vec2(0.0f), glm::vec2(1920.0f, 1080.0f)},
                               nullptr, 1.0f);
    for (const char* tag : { "Pause Resume", "Pause Restart", "Pause Main Menu", "Pause Quit" }) {
        const entt::entity item = byTag(registry, tag);
        CHECK_MSG(placed.Hidden(item),
                  std::string(tag) + " is suppressed while the menu is closed, rather "
                  "than falling back to its own anchor in the middle of the screen");
    }

    layer.OnDetach(registry);
}

// Pressing Pause opens it, and Resume closes it again.
static void testPauseOpensTheOverlayAndResumeClosesIt() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    layer.OnFixedUpdate(registry, kTick);

    registry.get<UIButtonComponent>(pauseButton(registry)).clickedThisTick = true;
    layer.OnFixedUpdate(registry, kTick);
    registry.get<UIButtonComponent>(pauseButton(registry)).clickedThisTick = false;

    CHECK_MSG(registry.get<UIStackComponent>(byTag(registry, "Pause Menu")).visible,
              "the Pause button opened the overlay");
    CHECK_MSG(registry.get<UIPanelComponent>(byTag(registry, "Pause Backdrop")).visible,
              "with its dim");

    const Match* match = layer.CurrentMatch();
    if (match == nullptr) { CHECK_MSG(false, "no match"); layer.OnDetach(registry); return; }
    const double stopped = match->Director().Elapsed();
    for (int i = 0; i < 30 * 3; ++i) layer.OnFixedUpdate(registry, kTick);
    CHECK_MSG(match->Director().Elapsed() == stopped, "and the match is not stepping");

    registry.get<UIButtonComponent>(byTag(registry, "Pause Resume")).clickedThisTick = true;
    layer.OnFixedUpdate(registry, kTick);
    registry.get<UIButtonComponent>(byTag(registry, "Pause Resume")).clickedThisTick = false;

    CHECK_MSG(!registry.get<UIStackComponent>(byTag(registry, "Pause Menu")).visible,
              "Resume closed it");
    for (int i = 0; i < 30 * 3; ++i) layer.OnFixedUpdate(registry, kTick);
    CHECK_MSG(match->Director().Elapsed() > stopped, "and the match is running again");

    layer.OnDetach(registry);
}

// Restart abandons the run and boots a fresh one.
static void testRestartBootsAFreshMatch() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);

    // Get the run somewhere distinctive first.
    for (int i = 0; i < 30 * 30; ++i) layer.OnFixedUpdate(registry, kTick);
    const Match* before = layer.CurrentMatch();
    if (before == nullptr) { CHECK_MSG(false, "no match"); layer.OnDetach(registry); return; }
    const double elapsedBefore = before->Director().Elapsed();
    CHECK_MSG(elapsedBefore > 0.0, "the run got somewhere to be abandoned");
    const std::size_t beforeCount = barButtons(registry).size();
    CHECK_MSG(beforeCount > 0, "and it has a bar to compare against");

    registry.get<UIButtonComponent>(pauseButton(registry)).clickedThisTick = true;
    layer.OnFixedUpdate(registry, kTick);
    registry.get<UIButtonComponent>(pauseButton(registry)).clickedThisTick = false;

    registry.get<UIButtonComponent>(byTag(registry, "Pause Restart")).clickedThisTick = true;
    layer.OnFixedUpdate(registry, kTick);
    registry.get<UIButtonComponent>(byTag(registry, "Pause Restart")).clickedThisTick = false;

    const Match* after = layer.CurrentMatch();
    if (after == nullptr) { CHECK_MSG(false, "no match after restart"); layer.OnDetach(registry); return; }
    CHECK_MSG(after->Director().Elapsed() < elapsedBefore,
              "the clock went back to the start of a fresh run");
    CHECK_MSG(!registry.get<UIStackComponent>(byTag(registry, "Pause Menu")).visible,
              "and it never reloads into a paused game");

    // The bar has to be rebuilt against the new match rather than left pointing
    // at the old one's selection - AND the old buttons have to be gone.
    //
    // Clearing the layer's own list without destroying the entities left them
    // in the registry, still parented to the bar's stack and still drawn and
    // clickable beside the new set: a restarted game grew a second row of
    // buttons wired to a match that no longer existed. Counting them is what
    // catches that; checking the bar is merely non-empty does not.
    layer.OnFixedUpdate(registry, kTick);
    const auto rebuilt = barButtons(registry);
    CHECK_MSG(!rebuilt.empty(), "the bar came back for the new run");
    CHECK_MSG(rebuilt.size() == beforeCount,
              "and it is the same SIZE as before the restart - " +
                  std::to_string(rebuilt.size()) + " against " +
                  std::to_string(beforeCount) + " means the old run's buttons "
                  "were forgotten rather than destroyed");

    layer.OnDetach(registry);
}

// Quit asks the application to stop, and does not stop it itself.
//
// A game could not close itself at all before this: a layer cannot reach the
// Window, and Window::ShouldClose only reports what GLFW already decided. So
// the Quit button on every pause menu ever written had nothing to call.
//
// The latch is checked rather than the process, obviously - but that IS the
// contract. Pressing Quit inside a tick must not tear the device down from
// several systems deep with a frame half-built around it; it raises a request
// the run loop reads at the top of the next frame.
static void testQuitAsksTheApplicationToStop() {
    Application::ClearQuitRequest();

    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    layer.OnFixedUpdate(registry, kTick);

    CHECK_MSG(!Application::QuitRequested(), "nobody has asked yet");

    registry.get<UIButtonComponent>(byTag(registry, "Pause Quit")).clickedThisTick = true;
    layer.OnFixedUpdate(registry, kTick);
    registry.get<UIButtonComponent>(byTag(registry, "Pause Quit")).clickedThisTick = false;

    CHECK_MSG(Application::QuitRequested(), "pressing Quit asked the application to stop");

    // And the tick that asked still finished, rather than the layer having torn
    // anything down under itself.
    CHECK_MSG(layer.CurrentMatch() != nullptr, "the match is still there to shut down tidily");

    Application::ClearQuitRequest();
    CHECK_MSG(!Application::QuitRequested(), "and the request can be withdrawn");

    layer.OnDetach(registry);
}

// Main Menu is still greyed, and named, because there is nowhere to go.
static void testMainMenuIsGreyedUntilThereIsAMenu() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    layer.OnFixedUpdate(registry, kTick);

    const entt::entity item = byTag(registry, "Pause Main Menu");
    CHECK_MSG(item != entt::null, "Main Menu is present rather than left out");
    if (item != entt::null) {
        CHECK_MSG(!registry.get<UIButtonComponent>(item).enabled,
                  "and greyed - there is no screen routing to go to yet");
    }

    for (const char* tag : { "Pause Resume", "Pause Restart", "Pause Quit" }) {
        CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, tag)).enabled,
                  std::string(tag) + " works and is not greyed");
    }

    layer.OnDetach(registry);
}

static void runTests() {
    testAttachingTheLayerBuildsTheHud();
    testTheHudReadsTheSimulation();
    testTheHudFollowsTheRunAsItGoes();
    testThePauseButtonStopsTheSimulation();
    testTheHudIsNotRebuiltEveryTick();
    testTheHudIsActuallyDrawnAndOnScreen();
    testTheBarOffersTheBuildMenuByDefault();
    testAffordabilityChangesDoNotRebuildTheBar();
    testStartingAPlacementRebuildsTheBarToCancel();
    testUnaffordableButtonsAreDisabledRatherThanRemoved();
    testADisabledButtonDoesNotAct();
    testTheClosedPauseMenuIsInvisibleAndInert();
    testPauseOpensTheOverlayAndResumeClosesIt();
    testRestartBootsAFreshMatch();
    testQuitAsksTheApplicationToStop();
    testMainMenuIsGreyedUntilThereIsAMenu();
}

TEST_MAIN("test_wb_hud", 88)
