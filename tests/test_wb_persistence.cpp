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

} // namespace

static void runTests() {
    testALayerWithNoSaveDirectoryTouchesNothing();

    testRenownEarnedInOneSessionIsThereInTheNext();
    testAFreshProfileIsNotWrittenUntilThereIsSomethingToWrite();

    testLeavingALiveRunWritesOneAndComingBackFindsIt();
    testAFinishedRunLeavesNoContinue();
    testARestartAbandonsTheSavedRun();

    testASaveDirectoryThatCannotBeWrittenDoesNotStopTheGame();
}

TEST_MAIN("test_wb_persistence", 27)
