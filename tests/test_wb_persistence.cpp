// The layer's filesystem contract, and what a player keeps between launches.
//
// SEPARATE FROM test_wb_hud on purpose, and the separation is the point rather
// than tidiness. That suite builds a bare layer and asserts about a registry;
// it has never touched a disk, and every case in it would start writing into
// the ctest working directory the moment one of them was given a save
// directory. Two suites reading and writing the same filenames in the same
// directory fail in an order-dependent way that reads as a flake.
//
// So the split is: a layer with no save directory touches nothing - which is
// what makes every OTHER suite safe - and this file is the one that gives it
// one, in a scratch directory it removes afterwards.
//
// The oracle is `tools/verify_continue.gd` and `tools/verify_autosave.gd`,
// which between them assert that a live run written on the way out comes back,
// that a finished one does not, and that a Restart abandons it. Those are
// behaviours of the LAYER here rather than of a bare Match, because the layer
// is what owns the path.

#include "TestHarness.hpp"

#include "WolfBrigadeLayer.hpp"

#include "core/Application.hpp"
#include "core/Components.hpp"

#include "sim/GameState.hpp"
#include "sim/Match.hpp"
#include "sim/Progression.hpp"
#include "sim/Snapshot.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace Supersonic;
using namespace WolfBrigade;

namespace {

constexpr float kTick = 1.0f / 30.0f;

entt::entity byTag(const entt::registry& registry, const std::string& tag) {
    for (auto [entity, name] : registry.view<const TagComponent>().each()) {
        if (name.tag == tag) return entity;
    }
    return entt::null;
}

// The click a real press produces. UIInput latches `clickedThisTick` and the
// layer reads it inside the tick, so setting it directly is the same event
// arriving without a mouse - see test_wb_hud, which drives every button of the
// pause menu this way.
bool press(entt::registry& registry, const std::string& tag) {
    const entt::entity entity = byTag(registry, tag);
    if (entity == entt::null) return false;
    registry.get<UIButtonComponent>(entity).clickedThisTick = true;
    return true;
}

// One tick, and then the latch is cleared the way UIInput clears it.
//
// THE CLEAR IS THE POINT, and leaving it out cost an hour. A click is given to
// exactly ONE tick - that is what `clickedThisTick` means, and `3eedb5e` is the
// commit that made it true. A fixture that sets the flag and never clears it
// leaves every button ever pressed in this test still pressed, so a walk from
// the pause menu to the main menu and back into a game arrives with "Pause Main
// Menu" still down and bounces straight back out to the menu in the same tick.
//
// It reads as the transition not working. It is the fixture holding the button.
void tickOnce(WolfBrigadeLayer& layer, entt::registry& registry) {
    layer.OnFixedUpdate(registry, kTick);
    for (auto [entity, button] : registry.view<UIButtonComponent>().each()) {
        button.clickedThisTick = false;
    }
}

// A scratch directory that removes itself, so a failing case cannot leave a
// profile behind for the next one to read as its own.
class ScratchDir {
public:
    explicit ScratchDir(const char* name)
        : m_path(std::filesystem::temp_directory_path() / name) {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
        std::filesystem::create_directories(m_path, ec);
    }

    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;

    std::string String() const { return m_path.string(); }
    std::filesystem::path Profile() const { return m_path / "wolf_brigade_save.json"; }
    std::filesystem::path Run() const { return m_path / "wolf_brigade_run.json"; }

    int FileCount() const {
        int count = 0;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(m_path, ec)) {
            (void)entry;
            ++count;
        }
        return count;
    }

private:
    std::filesystem::path m_path;
};

// --- The contract every other suite depends on ---------------------------

void testALayerWithNoSaveDirectoryTouchesNothing() {
    // The property that lets test_wb_hud exist. If this ever fails, that suite
    // is writing files in the ctest working directory and its cases can see
    // each other's.
    ScratchDir scratch("wb_no_save_dir");

    const std::filesystem::path before = std::filesystem::current_path();

    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);

    Match* match = layer.CurrentMatch();
    CHECK_MSG(match != nullptr, "it still boots a match");
    if (match != nullptr) {
        CHECK_MSG(!match->AutosaveRun(),
                  "and its match refuses to autosave, having nowhere to write");
    }

    layer.OnFixedUpdate(registry, kTick);
    layer.OnDetach(registry);

    CHECK_EQ(scratch.FileCount(), 0);
    CHECK_MSG(std::filesystem::current_path() == before,
              "and it does not move the working directory either");
}

// --- The profile round trip through the layer ----------------------------

void testRenownEarnedInOneSessionIsThereInTheNext() {
    ScratchDir scratch("wb_profile_across_sessions");

    {
        entt::registry registry;
        WolfBrigadeLayer layer(scratch.String());
        layer.OnAttach(registry);

        Match* match = layer.CurrentMatch();
        CHECK_MSG(match != nullptr, "a match boots");
        if (match == nullptr) return;

        CHECK_EQ(match->Run().CurrentWave(), 0);

        // Win it. Match banks the renown into the Profile itself, which is the
        // only writer - the layer never touches the number.
        match->Run().Win();
        layer.OnFixedUpdate(registry, kTick);

        layer.OnDetach(registry);
    }

    CHECK_MSG(std::filesystem::exists(scratch.Profile()),
              "the profile was written: " + scratch.Profile().string());

    // Read it back the way a second launch does - through a new layer, not
    // through a Profile the test loaded itself. The claim is about the wiring.
    {
        entt::registry registry;
        WolfBrigadeLayer layer(scratch.String());
        layer.OnAttach(registry);

        Match* match = layer.CurrentMatch();
        CHECK_MSG(match != nullptr, "the second session boots too");
        if (match == nullptr) return;

        CHECK_MSG(match->PlayerProfile().Renown() > 0, "and it opens holding what the first one earned");

        layer.OnDetach(registry);
    }
}

void testAFreshProfileIsNotWrittenUntilThereIsSomethingToWrite() {
    // A launch that changes nothing must not create a file. The original does
    // not either - `save.gd` writes only from `_put`.
    ScratchDir scratch("wb_profile_untouched");

    entt::registry registry;
    WolfBrigadeLayer layer(scratch.String());
    layer.OnAttach(registry);
    layer.OnFixedUpdate(registry, kTick);
    layer.OnDetach(registry);

    CHECK_MSG(!std::filesystem::exists(scratch.Profile()),
              "nothing changed, so nothing was written");
}

// --- The Continue run file -----------------------------------------------

void testLeavingALiveRunWritesOneAndComingBackFindsIt() {
    // `verify_autosave` and `verify_continue`, through the layer: a run that
    // was still being played when the window closed is on disk afterwards, and
    // it is a document Snapshot will accept.
    ScratchDir scratch("wb_run_autosave");

    {
        entt::registry registry;
        WolfBrigadeLayer layer(scratch.String());
        layer.OnAttach(registry);
        layer.OnFixedUpdate(registry, kTick);
        layer.OnDetach(registry);
    }

    CHECK_MSG(std::filesystem::exists(scratch.Run()),
              "a live run left on the way out is saved");
    CHECK_MSG(Snapshot::HasRun(scratch.Run().string()), "and the game agrees it is there");
    CHECK_MSG(Snapshot::IsValid(Snapshot::LoadRun(scratch.Run().string())),
              "and it is a document this build will restore");
}

void testAFinishedRunLeavesNoContinue() {
    // The asymmetry that decides whether the menu offers Continue. A run that
    // ENDED is not something to resume, and Match clears the file from its own
    // game-over handler - so quitting from the result screen must not put one
    // back.
    ScratchDir scratch("wb_run_finished");

    entt::registry registry;
    WolfBrigadeLayer layer(scratch.String());
    layer.OnAttach(registry);
    layer.OnFixedUpdate(registry, kTick);

    Match* match = layer.CurrentMatch();
    CHECK_MSG(match != nullptr, "a match boots");
    if (match == nullptr) { layer.OnDetach(registry); return; }

    // Write one first, so the check is that it was REMOVED rather than that it
    // was never there - the two are different and only one is the claim.
    CHECK_MSG(match->AutosaveRun(), "a live run saves");
    CHECK_MSG(std::filesystem::exists(scratch.Run()), "and is on disk");

    match->Run().Lose();
    layer.OnFixedUpdate(registry, kTick);
    layer.OnDetach(registry);

    CHECK_MSG(!std::filesystem::exists(scratch.Run()),
              "a finished run leaves nothing to continue");
}

void testARestartAbandonsTheSavedRun() {
    // `pause_menu.gd:45` in its own words. Nothing else would do it: Match
    // clears the run file from its game-over handler, and a Restart is exactly
    // the path that does NOT end a run - so without this the menu would offer
    // to resume the run the player had just restarted out of.
    ScratchDir scratch("wb_run_restart");

    entt::registry registry;
    WolfBrigadeLayer layer(scratch.String());
    layer.OnAttach(registry);
    layer.OnFixedUpdate(registry, kTick);

    Match* first = layer.CurrentMatch();
    CHECK_MSG(first != nullptr, "a match boots");
    if (first == nullptr) { layer.OnDetach(registry); return; }

    CHECK_MSG(first->AutosaveRun(), "the live run saves");
    CHECK_MSG(std::filesystem::exists(scratch.Run()), "and is on disk");

    // Through the pause menu's own button, not through a private helper - the
    // claim is about what the player's press does.
    layer.OnFixedUpdate(registry, kTick);
    CHECK_MSG(press(registry, "Pause Restart"), "the Restart button is there to press");
    layer.OnFixedUpdate(registry, kTick);

    CHECK_MSG(!std::filesystem::exists(scratch.Run()),
              "restarting abandons it");

    // And the new match can still write one, which is what a Restart handed an
    // empty path would have silently lost.
    Match* second = layer.CurrentMatch();
    CHECK_MSG(second != nullptr && second != first, "a new match is running");
    if (second != nullptr) {
        CHECK_MSG(second->AutosaveRun(), "and it can save, so it kept the path");
        CHECK_MSG(std::filesystem::exists(scratch.Run()), "which it did");
    }

    layer.OnDetach(registry);
}

// --- Failure is reported rather than pretended ---------------------------

void testASaveDirectoryThatCannotBeWrittenDoesNotStopTheGame() {
    // A path that does not exist and will not be created. The game must still
    // run - a player with a broken save directory has a game that does not
    // persist, not a game that does not start.
    entt::registry registry;
    WolfBrigadeLayer layer("no/such/directory/anywhere");
    layer.OnAttach(registry);

    Match* match = layer.CurrentMatch();
    CHECK_MSG(match != nullptr, "the match still boots");
    if (match == nullptr) return;

    match->Run().Win();
    layer.OnFixedUpdate(registry, kTick);

    // The profile still holds the renown in memory; only the write failed.
    CHECK_MSG(match->PlayerProfile().Renown() > 0,
              "and still banks what the run earned, in memory");

    layer.OnDetach(registry);
}

// --- Routing between screens ---------------------------------------------

void testLeavingToTheMenuSavesTheRunAndTakesTheLaneDown() {
    // `pause_menu.gd:51-62`: Main Menu saves the live run first. Here the save
    // lives inside the one transition function, so this is also the check that
    // the transition passes through it.
    ScratchDir scratch("wb_route_to_menu");

    entt::registry registry;
    WolfBrigadeLayer layer(scratch.String());
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    CHECK_MSG(layer.CurrentMatch() != nullptr, "a match is running");
    CHECK_MSG(!std::filesystem::exists(scratch.Run()), "and nothing is saved yet");

    // Through the pause menu, the way a player gets there.
    CHECK_MSG(press(registry, "HUD Pause"), "the Pause button is there");
    tickOnce(layer, registry);
    CHECK_MSG(press(registry, "Pause Main Menu"), "Main Menu is there and pressable");
    tickOnce(layer, registry);

    CHECK_MSG(std::filesystem::exists(scratch.Run()),
              "leaving a live run saved it");
    CHECK_MSG(layer.CurrentMatch() == nullptr,
              "and the match is gone rather than paused behind the menu");

    // The menu is up and the in-match screens are down.
    CHECK_MSG(registry.get<UIStackComponent>(byTag(registry, "Main Menu")).visible,
              "the menu is showing");
    CHECK_MSG(!registry.get<UITextComponent>(byTag(registry, "HUD Wood")).visible,
              "and the HUD is not");

    // AND THE LANE. The quad pool is not UI, so nothing about hiding a stack
    // reaches it - without an explicit pass the last frame of the match stays
    // drawn, and only the menu's backdrop happening to cover it hides the bug.
    int visibleQuads = 0;
    for (auto [entity, renderable, tag] :
         registry.view<const RenderableComponent, const TagComponent>().each()) {
        if (tag.tag == "WB Quad" && renderable.isVisible) ++visibleQuads;
    }
    CHECK_EQ(visibleQuads, 0);

    layer.OnDetach(registry);
}

void testTheMenuStartsAFreshGameAndAbandonsTheSavedRun() {
    // `_start_new_game`: starting fresh abandons any saved run.
    ScratchDir scratch("wb_route_new_game");

    entt::registry registry;
    WolfBrigadeLayer layer(scratch.String());
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    // Get to the menu with a run on disk.
    press(registry, "HUD Pause");
    tickOnce(layer, registry);
    press(registry, "Pause Main Menu");
    tickOnce(layer, registry);
    CHECK_MSG(std::filesystem::exists(scratch.Run()), "there is a run to abandon");

    CHECK_MSG(press(registry, "Menu New Game"), "New Game is there");
    tickOnce(layer, registry);

    CHECK_MSG(layer.CurrentMatch() != nullptr, "a match is running again");
    CHECK_MSG(!std::filesystem::exists(scratch.Run()),
              "and the run it would have resumed is gone");
    CHECK_MSG(!registry.get<UIStackComponent>(byTag(registry, "Main Menu")).visible,
              "the menu is down");
    CHECK_MSG(registry.get<UITextComponent>(byTag(registry, "HUD Wood")).visible,
              "and the HUD is back");
}

void testContinueIsOfferedOnlyWhenThereIsARunToContinue() {
    // `main_menu.gd:40`. Validity is asked of the DOCUMENT, not of the file's
    // existence - Snapshot::LoadRun deliberately does not validate, so a run
    // written by an older build parses and must not be offered.
    ScratchDir scratch("wb_route_continue");

    entt::registry registry;
    WolfBrigadeLayer layer(scratch.String());
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    // A run that has moved, so a restore is distinguishable from a fresh boot.
    for (int i = 0; i < 30 * 20; ++i) tickOnce(layer, registry);
    Match* first = layer.CurrentMatch();
    CHECK_MSG(first != nullptr, "a match is running");
    if (first == nullptr) { layer.OnDetach(registry); return; }
    const int bankedWood = first->Run().Amount("wood");

    press(registry, "HUD Pause");
    tickOnce(layer, registry);
    press(registry, "Pause Main Menu");
    tickOnce(layer, registry);

    CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, "Menu Continue")).visible,
              "Continue is offered, because there is a run");

    CHECK_MSG(press(registry, "Menu Continue"), "and pressable");
    tickOnce(layer, registry);

    Match* restored = layer.CurrentMatch();
    CHECK_MSG(restored != nullptr, "it booted a match");
    if (restored == nullptr) { layer.OnDetach(registry); return; }

    // The restored run is the one that was saved, not a fresh one. Twenty
    // seconds of gathering is the difference.
    CHECK_EQ(restored->Run().Amount("wood"), bankedWood);

    // And the file it restored from is consumed, or the menu would offer to
    // continue the run that is now live - and a second Continue would rewind
    // the player to where they resumed.
    CHECK_MSG(!std::filesystem::exists(scratch.Run()),
              "the restored run is consumed");

    press(registry, "HUD Pause");
    tickOnce(layer, registry);
    press(registry, "Pause Main Menu");
    tickOnce(layer, registry);
    layer.OnDetach(registry);
}

void testAMenuWithNoSavedRunDoesNotOfferContinue() {
    // The other half, and the one a fresh install sees.
    entt::registry registry;
    WolfBrigadeLayer layer;   // no save directory at all: nothing can be saved
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    press(registry, "HUD Pause");
    tickOnce(layer, registry);
    press(registry, "Pause Main Menu");
    tickOnce(layer, registry);

    CHECK_MSG(!registry.get<UIButtonComponent>(byTag(registry, "Menu Continue")).visible,
              "nothing to continue, so nothing is offered");

    // New Game still works, which is the whole of a first launch.
    CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, "Menu New Game")).enabled,
              "and New Game is live");

    layer.OnDetach(registry);
}

void testLeavingFromTheResultScreenTakesTheResultWithIt() {
    // Both modal states are cleared at every transition. The original throws
    // the scene away and gets this free; here they are fields, and a result
    // overlay left standing would sit on top of the menu - which is the same
    // class of bug as the pause menu that could be raised over a finished game.
    ScratchDir scratch("wb_route_from_result");

    entt::registry registry;
    WolfBrigadeLayer layer(scratch.String());
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    Match* match = layer.CurrentMatch();
    CHECK_MSG(match != nullptr, "a match is running");
    if (match == nullptr) { layer.OnDetach(registry); return; }

    match->Run().Win();
    tickOnce(layer, registry);
    CHECK_MSG(registry.get<UIStackComponent>(byTag(registry, "Result Menu")).visible,
              "the result is up");

    CHECK_MSG(press(registry, "Result Main Menu"), "its Main Menu is there");
    tickOnce(layer, registry);

    CHECK_MSG(!registry.get<UIStackComponent>(byTag(registry, "Result Menu")).visible,
              "and leaving takes it down");
    CHECK_MSG(registry.get<UIStackComponent>(byTag(registry, "Main Menu")).visible,
              "leaving the menu itself visible");

    // A FINISHED run is not saved on the way out. goTo calls AutosaveRun
    // unconditionally and that is still right, because AutosaveRun refuses a
    // game that is over - so this path writes nothing and the menu will not
    // offer to continue a run that ended.
    CHECK_MSG(!std::filesystem::exists(scratch.Run()),
              "a finished run leaves no Continue behind");

    layer.OnDetach(registry);
}

void testTheMenuShowsWhatThePlayerHasEarned() {
    // `_refresh_best` and the renown line, read on the way IN to the screen -
    // which is where main_menu.gd reads them, in _ready.
    ScratchDir scratch("wb_route_labels");

    entt::registry registry;
    WolfBrigadeLayer layer(scratch.String());
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    Match* match = layer.CurrentMatch();
    CHECK_MSG(match != nullptr, "a match is running");
    if (match == nullptr) { layer.OnDetach(registry); return; }

    // Reach a wave and end the run, so both numbers have something to say.
    match->PlayerProfile().RecordWave(7);
    match->Run().Win();
    tickOnce(layer, registry);

    // READ BEFORE LEAVING. Going to the menu destroys the Match, and with it
    // the Profile reference this pointer reaches - so asking it afterwards is
    // a use-after-free that happens to segfault rather than a wrong number.
    // The renown itself outlives the run, on the layer's Profile; it is only
    // this HANDLE that dies.
    const int banked = match->PlayerProfile().Renown();

    press(registry, "Result Main Menu");
    tickOnce(layer, registry);

    const std::string best =
        registry.get<UITextComponent>(byTag(registry, "Menu Best")).text;
    CHECK_MSG(best == "Best: wave 7", "the high score is shown: got \"" + best + "\"");

    const std::string renown =
        registry.get<UITextComponent>(byTag(registry, "Menu Renown")).text;
    const std::string expected = "Renown: " + std::to_string(banked);
    CHECK_MSG(renown == expected,
              "and the renown balance: got \"" + renown + "\", expected \"" + expected + "\"");

    layer.OnDetach(registry);
}

void testAFreshProfileSaysSoRatherThanClaimingWaveZero() {
    // "Best: no runs yet", not "Best: wave 0". The original branches on it and
    // a port that printed the number would tell a new player they had a score.
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    press(registry, "HUD Pause");
    tickOnce(layer, registry);
    press(registry, "Pause Main Menu");
    tickOnce(layer, registry);

    const std::string best =
        registry.get<UITextComponent>(byTag(registry, "Menu Best")).text;
    CHECK_MSG(best == "Best: no runs yet", "got \"" + best + "\"");

    layer.OnDetach(registry);
}

void testARunFromAnOlderBuildIsNotOffered() {
    // THE DISTINCTION BETWEEN "there is a file" AND "there is a run".
    //
    // Snapshot::LoadRun deliberately does not validate - HasRun and IsValid are
    // separate questions, and the original keeps them separate for exactly this
    // case. A run written by a build with a different schema PARSES; offering it
    // would hand Snapshot::Restore a document it refuses, and the player would
    // press Continue and get nothing.
    //
    // Written here rather than assumed, because a menu that asked HasRun passes
    // every other case in this file: in all of them the file that exists is
    // also valid, so the two questions have the same answer.
    //
    // Reached through a FINISHED run, and that is the only way to reach it: the
    // transition autosaves on the way out, so a live match would overwrite the
    // stale document with a current one before the menu ever read it. A game
    // that is over writes nothing - AutosaveRun refuses it - which leaves the
    // file on disk exactly as planted.
    ScratchDir scratch("wb_route_stale_run");

    entt::registry registry;
    WolfBrigadeLayer layer(scratch.String());
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    Match* match = layer.CurrentMatch();
    CHECK_MSG(match != nullptr, "a match is running");
    if (match == nullptr) { layer.OnDetach(registry); return; }

    match->Run().Lose();
    tickOnce(layer, registry);
    CHECK_MSG(!std::filesystem::exists(scratch.Run()),
              "the finished run cleared its own file");

    // Now the save a previous version of the game left behind.
    {
        std::ofstream out(scratch.Run(), std::ios::binary);
        out << R"({"version": 0, "state": {}, "units": [], "buildings": [],)"
               R"( "resource_nodes": [], "director": {}})";
    }
    CHECK_MSG(Snapshot::HasRun(scratch.Run().string()), "the stale file is there");
    CHECK_MSG(!Snapshot::IsValid(Snapshot::LoadRun(scratch.Run().string())),
              "and this build refuses it");

    press(registry, "Result Main Menu");
    tickOnce(layer, registry);

    CHECK_MSG(std::filesystem::exists(scratch.Run()),
              "leaving a finished run wrote nothing over it");
    CHECK_MSG(!registry.get<UIButtonComponent>(byTag(registry, "Menu Continue")).visible,
              "so the menu does not offer to continue something it cannot restore");

    layer.OnDetach(registry);
}

} // namespace

static void runTests() {
    testALayerWithNoSaveDirectoryTouchesNothing();

    testRenownEarnedInOneSessionIsThereInTheNext();
    testAFreshProfileIsNotWrittenUntilThereIsSomethingToWrite();

    testLeavingALiveRunWritesOneAndComingBackFindsIt();
    testAFinishedRunLeavesNoContinue();
    testARestartAbandonsTheSavedRun();

    testASaveDirectoryThatCannotBeWrittenDoesNotStopTheGame();

    testLeavingToTheMenuSavesTheRunAndTakesTheLaneDown();
    testTheMenuStartsAFreshGameAndAbandonsTheSavedRun();
    testContinueIsOfferedOnlyWhenThereIsARunToContinue();
    testAMenuWithNoSavedRunDoesNotOfferContinue();
    testLeavingFromTheResultScreenTakesTheResultWithIt();
    testTheMenuShowsWhatThePlayerHasEarned();
    testAFreshProfileSaysSoRatherThanClaimingWaveZero();
    testARunFromAnOlderBuildIsNotOffered();
}

TEST_MAIN("test_wb_persistence", 66)
