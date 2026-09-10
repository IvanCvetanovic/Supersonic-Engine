// The layer's filesystem contract, its screens, and what a player keeps.
//
// It began as the filesystem half and grew the screens with them, because the
// menu, the Armory and the Settings screen are all about the profile - and
// several of their cases build a layer with NO save directory, which is the
// contract the first case here asserts.
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

void testNewGameAsksBeforeThrowingARunAway() {
    // `main_menu.gd:71-83`: New Game silently wiping an in-progress run is a
    // footgun, so with something to lose it confirms first. This is also the
    // path that opens a modal over a live screen, which is the shape `b918b84`
    // exists for - a hidden container used to leave its children anchored at
    // the screen centre, invisible and still clickable.
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

    CHECK_MSG(registry.get<UIStackComponent>(byTag(registry, "Menu Confirm")).visible,
              "it asked instead of acting");
    CHECK_MSG(layer.CurrentMatch() == nullptr, "and started nothing yet");
    CHECK_MSG(std::filesystem::exists(scratch.Run()), "and threw nothing away yet");

    // THE MENU UNDER IT IS HIDDEN, not merely covered. The original hides
    // $Center so keyboard focus is trapped; here the reason is the same shape -
    // a backdrop swallows the clicks it covers, but the menu's buttons are
    // still laid out and still hit-testable by anything it does not reach.
    CHECK_MSG(!registry.get<UIStackComponent>(byTag(registry, "Main Menu")).visible,
              "the menu under it is hidden");

    // Cancel puts it back, having changed nothing.
    CHECK_MSG(press(registry, "Menu Confirm No"), "Cancel is there");
    tickOnce(layer, registry);
    CHECK_MSG(!registry.get<UIStackComponent>(byTag(registry, "Menu Confirm")).visible,
              "Cancel closes it");
    CHECK_MSG(registry.get<UIStackComponent>(byTag(registry, "Main Menu")).visible,
              "and the menu comes back");
    CHECK_MSG(std::filesystem::exists(scratch.Run()), "with the run still there");

    // And confirming does what New Game meant.
    press(registry, "Menu New Game");
    tickOnce(layer, registry);
    CHECK_MSG(press(registry, "Menu Confirm Yes"), "the confirmation is there again");
    tickOnce(layer, registry);

    CHECK_MSG(layer.CurrentMatch() != nullptr, "a match is running again");
    CHECK_MSG(!std::filesystem::exists(scratch.Run()),
              "and the run it would have resumed is gone");
    CHECK_MSG(!registry.get<UIStackComponent>(byTag(registry, "Main Menu")).visible,
              "the menu is down");
    CHECK_MSG(registry.get<UITextComponent>(byTag(registry, "HUD Wood")).visible,
              "and the HUD is back");
}

void testNewGameWithNothingToLoseDoesNotAsk() {
    // The other half of `main_menu.gd:73-83`, and it matters: a dialog that
    // always appears is one people learn to dismiss without reading.
    entt::registry registry;
    WolfBrigadeLayer layer;   // no save directory, so no run can exist
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    press(registry, "HUD Pause");
    tickOnce(layer, registry);
    press(registry, "Pause Main Menu");
    tickOnce(layer, registry);
    CHECK_MSG(layer.CurrentMatch() == nullptr, "we are on the menu");

    press(registry, "Menu New Game");
    tickOnce(layer, registry);

    CHECK_MSG(!registry.get<UIStackComponent>(byTag(registry, "Menu Confirm")).visible,
              "nothing to lose, so nothing is asked");
    CHECK_MSG(layer.CurrentMatch() != nullptr, "it just started");

    layer.OnDetach(registry);
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

// --- The radio rows ------------------------------------------------------

void testTheChosenDifficultyIsLitAndTheOthersAreNot() {
    // A radio look is three colours, not one. UISystem picks the fill fresh
    // every frame in the order disabled, pressed, hovered, colour - so a
    // selection written only into `color` VANISHES the moment the pointer
    // crosses it, on the one button the player is most likely pointing at.
    //
    // Asserted as a relation between buttons rather than against literals: the
    // claim is "the chosen one differs from the others in all three", which
    // survives someone re-tuning the palette.
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    press(registry, "HUD Pause");
    tickOnce(layer, registry);
    press(registry, "Pause Main Menu");
    tickOnce(layer, registry);

    // The data's own default is what a player who has chosen nothing sees.
    const entt::entity normal = byTag(registry, "Menu Difficulty Normal");
    const entt::entity hard = byTag(registry, "Menu Difficulty Hard");
    CHECK_MSG(normal != entt::null && hard != entt::null,
              "both difficulty options are on screen");
    if (normal == entt::null || hard == entt::null) { layer.OnDetach(registry); return; }

    {
        const auto& lit = registry.get<UIButtonComponent>(normal);
        const auto& dim = registry.get<UIButtonComponent>(hard);
        CHECK_MSG(lit.color != dim.color, "the default difficulty is lit");
        CHECK_MSG(lit.hoverColor != dim.hoverColor,
                  "and stays lit under the pointer, which one colour would not");
        CHECK_MSG(lit.pressColor != dim.pressColor, "and while pressed");
    }

    // Choosing another moves the light.
    press(registry, "Menu Difficulty Hard");
    tickOnce(layer, registry);

    {
        const auto& wasLit = registry.get<UIButtonComponent>(normal);
        const auto& nowLit = registry.get<UIButtonComponent>(hard);
        CHECK_MSG(nowLit.color != wasLit.color, "the light moved");
        CHECK_MSG(nowLit.hoverColor != wasLit.hoverColor, "in all three");
        CHECK_MSG(nowLit.pressColor != wasLit.pressColor, "colours");
    }

    layer.OnDetach(registry);
}

void testTheDifficultyRowIsInTheOrderTheDataDeclares() {
    // `difficulty.json` carries an "order" array, and it exists because the
    // presets are an object: iterating that gives a std::map's alphabetical
    // order - easy, hard, normal - which would put Hard in the middle.
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    const char* expected[] = { "Menu Difficulty Easy", "Menu Difficulty Normal",
                               "Menu Difficulty Hard" };
    int32_t previous = -1;
    for (const char* tag : expected) {
        const entt::entity entity = byTag(registry, tag);
        CHECK_MSG(entity != entt::null, std::string(tag) + " is present");
        if (entity == entt::null) continue;
        const int32_t order = registry.get<UIOrderComponent>(entity).order;
        CHECK_MSG(order > previous,
                  std::string(tag) + " comes after the one before it, got " +
                      std::to_string(order));
        previous = order;
    }

    layer.OnDetach(registry);
}

void testTheChosenRulesReachTheRunAndSurviveTheLaunch() {
    // The whole point of the rows, and the half a look cannot show. The choice
    // has to arrive in GameState BEFORE the boot, because Reset scales the
    // opening resources by the difficulty and WaveDirector::Setup reads the
    // mode to pick a schedule.
    ScratchDir scratch("wb_route_rules");

    int hardWood = 0;
    {
        entt::registry registry;
        WolfBrigadeLayer layer(scratch.String());
        layer.OnAttach(registry);
        tickOnce(layer, registry);

        Match* fresh = layer.CurrentMatch();
        CHECK_MSG(fresh != nullptr, "a match is running");
        if (fresh == nullptr) return;
        const int normalWood = fresh->Run().Amount("wood");

        press(registry, "HUD Pause");
        tickOnce(layer, registry);
        press(registry, "Pause Main Menu");
        tickOnce(layer, registry);

        press(registry, "Menu Difficulty Hard");
        tickOnce(layer, registry);

        press(registry, "Menu New Game");
        tickOnce(layer, registry);
        press(registry, "Menu Confirm Yes");
        tickOnce(layer, registry);

        Match* started = layer.CurrentMatch();
        CHECK_MSG(started != nullptr, "a match started");
        if (started == nullptr) return;

        CHECK_MSG(started->Run().CurrentDifficulty() == "hard",
                  "the run is on the chosen difficulty, got " +
                      started->Run().CurrentDifficulty());
        CHECK_MSG(started->Run().CurrentLevel() == "level_1",
                  "and on the default level, there being no campaign screen yet");

        // AND IT REACHED THE BOOT, not just the field. Hard scales starting
        // resources by 0.8, so the opening balance is the check that the order
        // was right - setting the difficulty after Reset would leave a run that
        // says "hard" and was dealt a normal hand.
        hardWood = started->Run().Amount("wood");
        CHECK_MSG(hardWood < normalWood,
                  "and was applied before the run was dealt: " +
                      std::to_string(hardWood) + " vs " + std::to_string(normalWood));

        layer.OnDetach(registry);
    }

    // And it is remembered, which is `SaveData.set_difficulty` beside
    // `GameState.set_difficulty` - one is the run, the other is next launch.
    {
        entt::registry registry;
        WolfBrigadeLayer layer(scratch.String());
        layer.OnAttach(registry);
        tickOnce(layer, registry);

        Match* match = layer.CurrentMatch();
        CHECK_MSG(match != nullptr, "the second launch boots");
        if (match == nullptr) return;

        CHECK_MSG(match->Run().CurrentDifficulty() == "hard",
                  "and opens on the difficulty last chosen, got " +
                      match->Run().CurrentDifficulty());
        CHECK_MSG(match->Run().CurrentLevel() == "level_1", "and the level");
        CHECK_EQ(match->Run().Amount("wood"), hardWood);

        layer.OnDetach(registry);
    }
}

void testARestartKeepsTheDifficultyItWasStartedOn() {
    // `game_state.gd:22-31` says it twice: neither difficulty nor mode is
    // cleared by reset(), because the autoload survives the scene reload and
    // only the run state is rebuilt. Here the GameState goes WITH the Match, so
    // keeping them across a Restart is an act rather than the default.
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    press(registry, "HUD Pause");
    tickOnce(layer, registry);
    press(registry, "Pause Main Menu");
    tickOnce(layer, registry);
    press(registry, "Menu Difficulty Hard");
    tickOnce(layer, registry);
    press(registry, "Menu New Game");
    tickOnce(layer, registry);

    Match* started = layer.CurrentMatch();
    CHECK_MSG(started != nullptr, "a match started");
    if (started == nullptr) { layer.OnDetach(registry); return; }
    CHECK_MSG(started->Run().CurrentDifficulty() == "hard", "on hard");
    const int hardWood = started->Run().Amount("wood");

    press(registry, "HUD Pause");
    tickOnce(layer, registry);
    press(registry, "Pause Restart");
    tickOnce(layer, registry);

    Match* restarted = layer.CurrentMatch();
    CHECK_MSG(restarted != nullptr && restarted != started, "a new match is running");
    if (restarted == nullptr) { layer.OnDetach(registry); return; }
    CHECK_MSG(restarted->Run().CurrentDifficulty() == "hard",
              "and it is still hard, got " + restarted->Run().CurrentDifficulty());
    CHECK_EQ(restarted->Run().Amount("wood"), hardWood);

    layer.OnDetach(registry);
}

void testResumingARunDoesNotRetuneIt() {
    // Snapshot::Restore sets both from the document at its own first step, and
    // the run being resumed was played on the difficulty it was started on.
    // Pushing whatever the menu currently shows over that would re-tune a run
    // mid-flight - a player who switches to Easy in the menu and then presses
    // Continue must not find their hard run softened.
    ScratchDir scratch("wb_route_resume_rules");

    entt::registry registry;
    WolfBrigadeLayer layer(scratch.String());
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    // Start a hard run and leave it.
    press(registry, "HUD Pause");
    tickOnce(layer, registry);
    press(registry, "Pause Main Menu");
    tickOnce(layer, registry);
    press(registry, "Menu Difficulty Hard");
    tickOnce(layer, registry);
    press(registry, "Menu New Game");
    tickOnce(layer, registry);

    // There IS a run on disk here - the first match was left through the pause
    // menu - so New Game asks before throwing it away.
    CHECK_MSG(press(registry, "Menu Confirm Yes"), "it confirmed first");
    tickOnce(layer, registry);

    CHECK_MSG(layer.CurrentMatch() != nullptr &&
                  layer.CurrentMatch()->Run().CurrentDifficulty() == "hard",
              "a hard run is going");

    press(registry, "HUD Pause");
    tickOnce(layer, registry);
    press(registry, "Pause Main Menu");
    tickOnce(layer, registry);

    // Change the preference, then resume.
    press(registry, "Menu Difficulty Easy");
    tickOnce(layer, registry);
    CHECK_MSG(press(registry, "Menu Continue"), "Continue is offered");
    tickOnce(layer, registry);

    Match* resumed = layer.CurrentMatch();
    CHECK_MSG(resumed != nullptr, "it resumed");
    if (resumed == nullptr) { layer.OnDetach(registry); return; }
    CHECK_MSG(resumed->Run().CurrentDifficulty() == "hard",
              "and the resumed run is still the one that was saved, got " +
                  resumed->Run().CurrentDifficulty());

    layer.OnDetach(registry);
}

// --- The Armory ----------------------------------------------------------

// Walks from a running match out to the Armory, which is the only way in.
void openArmory(WolfBrigadeLayer& layer, entt::registry& registry) {
    press(registry, "HUD Pause");
    tickOnce(layer, registry);
    press(registry, "Pause Main Menu");
    tickOnce(layer, registry);
    press(registry, "Menu Armory");
    tickOnce(layer, registry);
}

void testTheArmoryListsEveryUpgradeInTheOrderTheOriginalShowsThem() {
    // The data is a JSON object and Supersonic::Json holds one as a std::map,
    // so walking it gives deeper_coffers, fortified_halls, sharper_axes,
    // veteran_soldiers - alphabetical. The original shows the file's own order,
    // because a Godot Dictionary keeps insertion order.
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);
    openArmory(layer, registry);

    CHECK_MSG(registry.get<UIStackComponent>(byTag(registry, "Armory")).visible,
              "the Armory is up");

    const char* expected[] = { "sharper_axes", "veteran_soldiers", "deeper_coffers",
                               "fortified_halls" };
    int32_t previous = -1;
    for (const char* id : expected) {
        const entt::entity row = byTag(registry, std::string("Armory Row ") + id);
        CHECK_MSG(row != entt::null, std::string(id) + " has a row");
        if (row == entt::null) continue;
        const int32_t order = registry.get<UIOrderComponent>(row).order;
        CHECK_MSG(order > previous,
                  std::string(id) + " comes after the row before it, got " +
                      std::to_string(order));
        previous = order;
    }

    layer.OnDetach(registry);
}

void testAnUnaffordableUpgradeIsGreyedAndPricedAnyway() {
    // "Available" and "affordable" are different questions - the original greys
    // what cannot be paid for and still shows the price, because most of the
    // reason to open this screen is to see what you are saving up for.
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);
    openArmory(layer, registry);

    const entt::entity buy = byTag(registry, "Armory Buy sharper_axes");
    CHECK_MSG(buy != entt::null, "it has a Buy button");
    if (buy == entt::null) { layer.OnDetach(registry); return; }

    const auto& button = registry.get<UIButtonComponent>(buy);
    CHECK_MSG(!button.enabled, "a new player cannot afford anything");
    CHECK_MSG(button.label.find("40") != std::string::npos,
              "and is told the price anyway, got \"" + button.label + "\"");

    // The level line reads from the profile, not from a constant.
    const std::string level =
        registry.get<UITextComponent>(byTag(registry, "Armory Level sharper_axes")).text;
    CHECK_MSG(level.find("Lv 0/3") != std::string::npos,
              "and the level line is the profile's, got \"" + level + "\"");

    layer.OnDetach(registry);
}

void testBuyingSpendsRenownAndRaisesTheLevel() {
    // The screen's whole job. Driven through the button rather than through
    // Meta::Buy, because Meta::Buy already has a suite and what is untested is
    // the wiring between a press and it.
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    Match* match = layer.CurrentMatch();
    CHECK_MSG(match != nullptr, "a match is running");
    if (match == nullptr) { layer.OnDetach(registry); return; }
    match->PlayerProfile().AddRenown(200);

    openArmory(layer, registry);

    CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, "Armory Buy sharper_axes")).enabled,
              "200 renown affords the 40-renown first level");
    CHECK_MSG(registry.get<UITextComponent>(byTag(registry, "Armory Renown")).text ==
                  "Renown: 200",
              "and the balance is shown");

    press(registry, "Armory Buy sharper_axes");
    tickOnce(layer, registry);

    CHECK_MSG(registry.get<UITextComponent>(byTag(registry, "Armory Renown")).text ==
                  "Renown: 160",
              "buying spent exactly the price, got \"" +
                  registry.get<UITextComponent>(byTag(registry, "Armory Renown")).text + "\"");

    const std::string level =
        registry.get<UITextComponent>(byTag(registry, "Armory Level sharper_axes")).text;
    CHECK_MSG(level.find("Lv 1/3") != std::string::npos,
              "and raised the level, got \"" + level + "\"");

    // The next level costs more, and the row says so without being rebuilt.
    const std::string next =
        registry.get<UIButtonComponent>(byTag(registry, "Armory Buy sharper_axes")).label;
    CHECK_MSG(next.find("80") != std::string::npos,
              "the price rose with the level, got \"" + next + "\"");

    layer.OnDetach(registry);
}

void testAMaxedUpgradeSaysSoAndCannotBeBoughtAgain() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    Match* match = layer.CurrentMatch();
    CHECK_MSG(match != nullptr, "a match is running");
    if (match == nullptr) { layer.OnDetach(registry); return; }
    match->PlayerProfile().AddRenown(5000);
    match->PlayerProfile().SetMetaLevel("sharper_axes", 3);

    openArmory(layer, registry);

    const auto& buy =
        registry.get<UIButtonComponent>(byTag(registry, "Armory Buy sharper_axes"));
    CHECK_MSG(buy.label == "MAX", "a maxed upgrade says MAX, got \"" + buy.label + "\"");
    CHECK_MSG(!buy.enabled, "and is greyed even with renown to burn");

    layer.OnDetach(registry);
}

void testAPurchaseIsRememberedAcrossLaunches() {
    // The half that makes it META progression rather than a shop. It also
    // exercises the write path: buying dirties the profile and the layer writes
    // it at the end of the same tick.
    ScratchDir scratch("wb_armory_persist");

    {
        entt::registry registry;
        WolfBrigadeLayer layer(scratch.String());
        layer.OnAttach(registry);
        tickOnce(layer, registry);

        Match* match = layer.CurrentMatch();
        CHECK_MSG(match != nullptr, "a match is running");
        if (match == nullptr) return;
        match->PlayerProfile().AddRenown(200);

        openArmory(layer, registry);
        press(registry, "Armory Buy veteran_soldiers");
        tickOnce(layer, registry);

        layer.OnDetach(registry);
    }

    {
        entt::registry registry;
        WolfBrigadeLayer layer(scratch.String());
        layer.OnAttach(registry);
        tickOnce(layer, registry);

        Match* match = layer.CurrentMatch();
        CHECK_MSG(match != nullptr, "the second launch boots");
        if (match == nullptr) return;

        CHECK_EQ(match->PlayerProfile().MetaLevel("veteran_soldiers"), 1);
        CHECK_EQ(match->PlayerProfile().Renown(), 140);

        layer.OnDetach(registry);
    }
}

void testBackReturnsToTheMenuWithTheNewBalance() {
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    Match* match = layer.CurrentMatch();
    CHECK_MSG(match != nullptr, "a match is running");
    if (match == nullptr) { layer.OnDetach(registry); return; }
    match->PlayerProfile().AddRenown(200);

    openArmory(layer, registry);
    press(registry, "Armory Buy sharper_axes");
    tickOnce(layer, registry);

    CHECK_MSG(press(registry, "Armory Back"), "Back is there");
    tickOnce(layer, registry);

    CHECK_MSG(!registry.get<UIStackComponent>(byTag(registry, "Armory")).visible,
              "the Armory is down");
    CHECK_MSG(registry.get<UIStackComponent>(byTag(registry, "Main Menu")).visible,
              "and the menu is back");

    // Read on the way IN, which is where main_menu.gd reads it - so the menu
    // shows what the Armory just spent rather than what it held before.
    CHECK_MSG(registry.get<UITextComponent>(byTag(registry, "Menu Renown")).text ==
                  "Renown: 160",
              "showing the balance the Armory left, got \"" +
                  registry.get<UITextComponent>(byTag(registry, "Menu Renown")).text + "\"");

    layer.OnDetach(registry);
}

void testAnOwnedUpgradeReachesTheNextRunsUnits() {
    // The reason any of this exists. Meta::Apply is covered by its own suite;
    // what is not is that a purchase made on this SCREEN reaches a match
    // started afterwards - the Profile is shared by reference, and a layer that
    // handed the Armory a copy would sell upgrades that never arrive.
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    Match* first = layer.CurrentMatch();
    CHECK_MSG(first != nullptr, "a match is running");
    if (first == nullptr) { layer.OnDetach(registry); return; }
    const int plainWood = first->Run().Amount("wood");
    first->PlayerProfile().AddRenown(500);

    openArmory(layer, registry);
    press(registry, "Armory Buy deeper_coffers");
    tickOnce(layer, registry);
    press(registry, "Armory Back");
    tickOnce(layer, registry);
    press(registry, "Menu New Game");
    tickOnce(layer, registry);

    Match* started = layer.CurrentMatch();
    CHECK_MSG(started != nullptr, "a new run started");
    if (started == nullptr) { layer.OnDetach(registry); return; }

    CHECK_EQ(started->PlayerProfile().MetaLevel("deeper_coffers"), 1);
    CHECK_MSG(started->Run().Amount("wood") > plainWood,
              "and Deeper Coffers reached its opening balance: " +
                  std::to_string(started->Run().Amount("wood")) + " vs " +
                  std::to_string(plainWood));

    layer.OnDetach(registry);
}

// --- Settings ------------------------------------------------------------

void openSettings(WolfBrigadeLayer& layer, entt::registry& registry) {
    press(registry, "HUD Pause");
    tickOnce(layer, registry);
    press(registry, "Pause Main Menu");
    tickOnce(layer, registry);
    press(registry, "Menu Settings");
    tickOnce(layer, registry);
}

void testTheVolumeStepsAndStopsAtBothEnds() {
    // Ten per cent a press, and a button that cannot move is greyed rather than
    // left to do nothing - `settings.gd:66-67`. The setter clamps either way,
    // so a live-looking button that no-ops is the thing being avoided.
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);
    openSettings(layer, registry);

    CHECK_MSG(registry.get<UIStackComponent>(byTag(registry, "Settings")).visible,
              "the Settings screen is up");
    CHECK_MSG(registry.get<UITextComponent>(byTag(registry, "Settings Volume")).text ==
                  "Volume: 100%",
              "a new profile is at full volume, got \"" +
                  registry.get<UITextComponent>(byTag(registry, "Settings Volume")).text + "\"");

    CHECK_MSG(!registry.get<UIButtonComponent>(byTag(registry, "Settings Volume Up")).enabled,
              "and cannot go louder");
    CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, "Settings Volume Down")).enabled,
              "but can go quieter");

    press(registry, "Settings Volume Down");
    tickOnce(layer, registry);
    CHECK_MSG(registry.get<UITextComponent>(byTag(registry, "Settings Volume")).text ==
                  "Volume: 90%",
              "one press is ten per cent, got \"" +
                  registry.get<UITextComponent>(byTag(registry, "Settings Volume")).text + "\"");
    CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, "Settings Volume Up")).enabled,
              "and louder is live again");

    // All the way down, then one more, which must not wrap or go negative.
    for (int i = 0; i < 12; ++i) {
        press(registry, "Settings Volume Down");
        tickOnce(layer, registry);
    }
    CHECK_MSG(registry.get<UITextComponent>(byTag(registry, "Settings Volume")).text ==
                  "Volume: 0%",
              "it stops at silence, got \"" +
                  registry.get<UITextComponent>(byTag(registry, "Settings Volume")).text + "\"");
    CHECK_MSG(!registry.get<UIButtonComponent>(byTag(registry, "Settings Volume Down")).enabled,
              "and quieter is greyed");

    layer.OnDetach(registry);
}

void testMutingIsRememberedAndSaidOnTheButton() {
    ScratchDir scratch("wb_settings_mute");

    {
        entt::registry registry;
        WolfBrigadeLayer layer(scratch.String());
        layer.OnAttach(registry);
        tickOnce(layer, registry);
        openSettings(layer, registry);

        CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, "Settings Mute")).label ==
                      "Sound: On",
                  "it opens un-muted");

        press(registry, "Settings Mute");
        tickOnce(layer, registry);
        CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, "Settings Mute")).label ==
                      "Sound: Off",
                  "and the button says what it did");

        layer.OnDetach(registry);
    }

    {
        entt::registry registry;
        WolfBrigadeLayer layer(scratch.String());
        layer.OnAttach(registry);
        tickOnce(layer, registry);

        Match* match = layer.CurrentMatch();
        CHECK_MSG(match != nullptr, "the second launch boots");
        if (match == nullptr) return;
        CHECK_MSG(match->PlayerProfile().Muted(),
                  "and the game reopens the way it was left");

        layer.OnDetach(registry);
    }
}

void testResettingProgressTakesTwoTapsAndKeepsThePreferences() {
    // A profile is the only thing in this game a player cannot get back, and it
    // sits one press away from a volume control. `settings.gd:44-60`.
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    Match* match = layer.CurrentMatch();
    CHECK_MSG(match != nullptr, "a match is running");
    if (match == nullptr) { layer.OnDetach(registry); return; }
    match->PlayerProfile().AddRenown(300);
    match->PlayerProfile().SetMetaLevel("sharper_axes", 2);
    match->PlayerProfile().RecordWave(9);

    openSettings(layer, registry);

    // Set a preference, so the keep-half of ResetProgress has something to keep.
    press(registry, "Settings Volume Down");
    tickOnce(layer, registry);

    CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, "Settings Reset")).label ==
                  "Reset Progress",
              "it starts disarmed");

    press(registry, "Settings Reset");
    tickOnce(layer, registry);

    CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, "Settings Reset")).label ==
                  "Tap again to confirm",
              "the first tap arms and says so");

    // NOTHING IS GONE YET. This is the check that makes it a guard rather than
    // a label.
    //
    // Read from the LAYER's profile, not from a match: getting to this screen
    // goes through the menu, which drops the match. The profile is the thing
    // that outlives one, which is the whole reason this screen can edit it.
    Profile* profile = layer.PlayerProfile();
    CHECK_MSG(profile != nullptr, "the profile is still there");
    if (profile == nullptr) { layer.OnDetach(registry); return; }
    CHECK_EQ(profile->Renown(), 300);

    press(registry, "Settings Reset");
    tickOnce(layer, registry);

    CHECK_EQ(profile->Renown(), 0);
    CHECK_EQ(profile->MetaLevel("sharper_axes"), 0);
    CHECK_EQ(profile->BestWave(), 0);

    // AND THE PREFERENCES SURVIVE, which is `verify_settings.gd:79` - "Reset
    // keeps preferences (volume untouched)". The two halves of that document
    // have different lifetimes and this screen is where the difference shows.
    CHECK_MSG(registry.get<UITextComponent>(byTag(registry, "Settings Volume")).text ==
                  "Volume: 90%",
              "the volume is untouched, got \"" +
                  registry.get<UITextComponent>(byTag(registry, "Settings Volume")).text + "\"");

    layer.OnDetach(registry);
}

void testAnArmedResetDisarmsItselfBeforeItCanBeMisTapped() {
    // Counted in TICKS, not off a wall clock - every other deadline in this
    // game is simulated time. Without it an armed button survives the player
    // wandering off, and the next stray tap wipes a profile.
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    Match* match = layer.CurrentMatch();
    CHECK_MSG(match != nullptr, "a match is running");
    if (match == nullptr) { layer.OnDetach(registry); return; }
    match->PlayerProfile().AddRenown(300);

    openSettings(layer, registry);

    press(registry, "Settings Reset");
    tickOnce(layer, registry);
    CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, "Settings Reset")).label ==
                  "Tap again to confirm",
              "armed");

    // Four seconds, past the three the original allows.
    for (int i = 0; i < 30 * 4; ++i) tickOnce(layer, registry);

    CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, "Settings Reset")).label ==
                  "Reset Progress",
              "and it disarms itself, got \"" +
                  registry.get<UIButtonComponent>(byTag(registry, "Settings Reset")).label + "\"");

    // A tap now ARMS rather than wipes.
    press(registry, "Settings Reset");
    tickOnce(layer, registry);
    CHECK_EQ(layer.PlayerProfile()->Renown(), 300);

    layer.OnDetach(registry);
}

void testLeavingAndReturningDoesNotArriveArmed() {
    // A Reset armed on one visit and left there would be one tap from wiping a
    // profile the moment somebody opened the screen again.
    entt::registry registry;
    WolfBrigadeLayer layer;
    layer.OnAttach(registry);
    tickOnce(layer, registry);

    Match* match = layer.CurrentMatch();
    CHECK_MSG(match != nullptr, "a match is running");
    if (match == nullptr) { layer.OnDetach(registry); return; }
    match->PlayerProfile().AddRenown(300);

    openSettings(layer, registry);
    press(registry, "Settings Reset");
    tickOnce(layer, registry);
    CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, "Settings Reset")).label ==
                  "Tap again to confirm",
              "armed");

    press(registry, "Settings Back");
    tickOnce(layer, registry);
    CHECK_MSG(registry.get<UIStackComponent>(byTag(registry, "Main Menu")).visible,
              "Back returns to the menu");
    CHECK_MSG(!registry.get<UIStackComponent>(byTag(registry, "Settings")).visible,
              "and takes the screen down");

    press(registry, "Menu Settings");
    tickOnce(layer, registry);
    CHECK_MSG(registry.get<UIButtonComponent>(byTag(registry, "Settings Reset")).label ==
                  "Reset Progress",
              "and it comes back disarmed");

    press(registry, "Settings Reset");
    tickOnce(layer, registry);
    CHECK_EQ(layer.PlayerProfile()->Renown(), 300);

    layer.OnDetach(registry);
}

void testTheVolumeSurvivesTheLaunchThatSetIt() {
    ScratchDir scratch("wb_settings_volume");

    {
        entt::registry registry;
        WolfBrigadeLayer layer(scratch.String());
        layer.OnAttach(registry);
        tickOnce(layer, registry);
        openSettings(layer, registry);

        for (int i = 0; i < 3; ++i) {
            press(registry, "Settings Volume Down");
            tickOnce(layer, registry);
        }
        CHECK_MSG(registry.get<UITextComponent>(byTag(registry, "Settings Volume")).text ==
                      "Volume: 70%",
                  "three presses, got \"" +
                      registry.get<UITextComponent>(byTag(registry, "Settings Volume")).text + "\"");

        layer.OnDetach(registry);
    }

    {
        entt::registry registry;
        WolfBrigadeLayer layer(scratch.String());
        layer.OnAttach(registry);
        tickOnce(layer, registry);
        openSettings(layer, registry);

        CHECK_MSG(registry.get<UITextComponent>(byTag(registry, "Settings Volume")).text ==
                      "Volume: 70%",
                  "and it reopens where it was left, got \"" +
                      registry.get<UITextComponent>(byTag(registry, "Settings Volume")).text + "\"");

        layer.OnDetach(registry);
    }
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
    testNewGameAsksBeforeThrowingARunAway();
    testNewGameWithNothingToLoseDoesNotAsk();
    testContinueIsOfferedOnlyWhenThereIsARunToContinue();
    testAMenuWithNoSavedRunDoesNotOfferContinue();
    testLeavingFromTheResultScreenTakesTheResultWithIt();
    testTheMenuShowsWhatThePlayerHasEarned();
    testAFreshProfileSaysSoRatherThanClaimingWaveZero();
    testARunFromAnOlderBuildIsNotOffered();

    testTheChosenDifficultyIsLitAndTheOthersAreNot();
    testTheDifficultyRowIsInTheOrderTheDataDeclares();
    testTheChosenRulesReachTheRunAndSurviveTheLaunch();
    testARestartKeepsTheDifficultyItWasStartedOn();
    testResumingARunDoesNotRetuneIt();

    testTheArmoryListsEveryUpgradeInTheOrderTheOriginalShowsThem();
    testAnUnaffordableUpgradeIsGreyedAndPricedAnyway();
    testBuyingSpendsRenownAndRaisesTheLevel();
    testAMaxedUpgradeSaysSoAndCannotBeBoughtAgain();
    testAPurchaseIsRememberedAcrossLaunches();
    testBackReturnsToTheMenuWithTheNewBalance();
    testAnOwnedUpgradeReachesTheNextRunsUnits();

    testTheVolumeStepsAndStopsAtBothEnds();
    testMutingIsRememberedAndSaidOnTheButton();
    testResettingProgressTakesTwoTapsAndKeepsThePreferences();
    testAnArmedResetDisarmsItselfBeforeItCanBeMisTapped();
    testLeavingAndReturningDoesNotArriveArmed();
    testTheVolumeSurvivesTheLaunchThatSetIt();
}

TEST_MAIN("test_wb_persistence", 178)
