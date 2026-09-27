// Regression tests for the packaged-game manifest.
//
// The manifest is the only thing separating a shipped game from the editor:
// the binary is identical, and packaging is a copy rather than a build. If it
// does not parse, the player gets an editor - which is precisely what
// packaging used to produce every time.
//
// And the window it declares, then changes while it runs: fullscreen from the
// manifest or a flag, and the requests a layer makes through WindowControl,
// which are latched and applied a frame later rather than acted on inside the
// tick that made them.

#include "TestHarness.hpp"
#include "core/GameRuntime.hpp"
#include "core/Input.hpp"
#include "core/WindowControl.hpp"
#include "platform/ExecutablePath.hpp"

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace Supersonic;

static void testRoundTrip() {
    GameManifest source;
    source.isGame = true;
    source.title = "Bug Hunt";
    source.startupScene = "assets/scenes/Level1.scene";

    const GameManifest parsed = GameRuntime::Parse(GameRuntime::Serialize(source));

    CHECK(parsed.isGame);
    CHECK(parsed.title == "Bug Hunt");
    CHECK(parsed.startupScene == "assets/scenes/Level1.scene");
}

static void testTitleWithQuotesSurvives() {
    // A game folder is named by the user and becomes the title, so it is
    // free-form text. Writing it raw produced a manifest that would not parse,
    // and an unparseable manifest silently starts the editor.
    GameManifest source;
    source.isGame = true;
    source.title = R"(My "Great" Game \ 2)";

    const GameManifest parsed = GameRuntime::Parse(GameRuntime::Serialize(source));
    CHECK(parsed.isGame);
    CHECK_MSG(parsed.title == source.title, "an escaped title must round-trip: got " + parsed.title);
}

static void testAbsentGameKeyIsNotAGame() {
    // The key marks a game, not the file's existence. A stray manifest in a
    // build tree must not turn the editor into a game with no way back.
    const GameManifest parsed = GameRuntime::Parse(R"({"Title": "Nope"})");
    CHECK_MSG(!parsed.isGame, "a manifest without \"Game\": true is not a game");
}

static void testFalseGameKeyIsNotAGame() {
    const GameManifest parsed = GameRuntime::Parse(R"({"Game": false, "Title": "Nope"})");
    CHECK(!parsed.isGame);
}

static void testGarbageIsNotAGame() {
    // Better to start the editor than to start a game with no scene: the
    // editor can at least be used to repackage.
    CHECK(!GameRuntime::Parse("this is not json").isGame);
    CHECK(!GameRuntime::Parse("").isGame);
    CHECK(!GameRuntime::Parse("[1, 2, 3]").isGame);
}

static void testDefaultsFillIn() {
    // A hand-written manifest naming only the flag still has to boot.
    const GameManifest parsed = GameRuntime::Parse(R"({"Game": true})");
    CHECK(parsed.isGame);
    CHECK_MSG(!parsed.startupScene.empty(), "a game with no named scene must still have one to try");
    CHECK(!parsed.title.empty());
}


// --- A game's declared window size ----------------------------------------

static void testAManifestCanDeclareItsWindow() {
    const auto manifest = GameRuntime::Parse(
        R"({ "Game": true, "Title": "Wolf Brigade", "Width": 1920, "Height": 1080 })");
    CHECK(manifest.isGame);
    CHECK_EQ(manifest.width, uint32_t{1920});
    CHECK_EQ(manifest.height, uint32_t{1080});
}

static void testAManifestThatSaysNothingLeavesTheDefault() {
    // Every manifest written before this one, and they must keep meaning what
    // they meant. Zero is the caller's cue to use its own default rather than a
    // size this parser invented.
    const auto manifest = GameRuntime::Parse(R"({ "Game": true, "Title": "Old" })");
    CHECK(manifest.isGame);
    CHECK_EQ(manifest.width, uint32_t{0});
    CHECK_EQ(manifest.height, uint32_t{0});
}

static void testHalfASizeIsNotASize() {
    // A manifest naming only a width would otherwise get that width against the
    // default height, which is an aspect ratio nobody chose - and it would look
    // like the engine ignoring the number rather than like the half-written
    // manifest it is.
    const auto wide = GameRuntime::Parse(R"({ "Game": true, "Width": 1920 })");
    CHECK_EQ(wide.width, uint32_t{0});
    CHECK_EQ(wide.height, uint32_t{0});

    const auto tall = GameRuntime::Parse(R"({ "Game": true, "Height": 1080 })");
    CHECK_EQ(tall.width, uint32_t{0});
    CHECK_EQ(tall.height, uint32_t{0});
}

static void testAnUnusableSizeFallsBackRatherThanOpeningIt() {
    for (const char* text : {R"({ "Game": true, "Width": 8, "Height": 8 })",
                             R"({ "Game": true, "Width": 99999, "Height": 1080 })",
                             R"({ "Game": true, "Width": -1920, "Height": -1080 })"}) {
        const auto manifest = GameRuntime::Parse(text);
        CHECK_MSG(manifest.isGame, "it is still a game");
        CHECK_MSG(manifest.width == 0 && manifest.height == 0,
                  std::string("and an unusable size is declined: ") + text);
    }
}

static void testTheSizeSurvivesTheRoundTripThePackagerUses() {
    GameManifest manifest;
    manifest.isGame = true;
    manifest.title = "Wolf Brigade";
    manifest.startupScene = "assets/scenes/Lane.scene";
    manifest.width = 1920;
    manifest.height = 1080;

    const auto reparsed = GameRuntime::Parse(GameRuntime::Serialize(manifest));
    CHECK(reparsed.isGame);
    CHECK_MSG(reparsed.title == manifest.title, "the title survives");
    CHECK_MSG(reparsed.startupScene == manifest.startupScene, "and the scene");
    CHECK_EQ(reparsed.width, uint32_t{1920});
    CHECK_EQ(reparsed.height, uint32_t{1080});
}

static void testAGameThatStatedNoSizeGetsTheManifestItAlwaysDid() {
    // Packaging a game that never asked for a size must not start writing keys
    // into its manifest saying the thing their absence already said.
    GameManifest manifest;
    manifest.isGame = true;
    manifest.title = "Plain";

    const std::string text = GameRuntime::Serialize(manifest);
    CHECK_MSG(text.find("Width") == std::string::npos,
              "no Width key: " + text);
    CHECK_MSG(text.find("Height") == std::string::npos,
              "no Height key: " + text);

    const auto reparsed = GameRuntime::Parse(text);
    CHECK(reparsed.isGame);
    CHECK_EQ(reparsed.width, uint32_t{0});
}


// --- Which of the three answers wins --------------------------------------

static void testTheFlagBeatsTheManifestWhichBeatsTheDefault() {
    // The precedence, and it is a rule no suite could reach while it lived
    // inline in SupersonicApp - which no test can construct, because it needs a
    // device, a window and a swapchain.
    GameManifest declared;
    declared.isGame = true;
    declared.width = 1920;
    declared.height = 1080;

    uint32_t w = 0;
    uint32_t h = 0;

    // Nothing stated anywhere.
    GameRuntime::ResolveWindowSize(GameManifest{}, 0, 0, w, h);
    CHECK_EQ(w, GameManifest::kDefaultWidth);
    CHECK_EQ(h, GameManifest::kDefaultHeight);

    // The game says so.
    GameRuntime::ResolveWindowSize(declared, 0, 0, w, h);
    CHECK_EQ(w, uint32_t{1920});
    CHECK_EQ(h, uint32_t{1080});

    // The flag says otherwise, and wins - which is what makes a measurement at
    // another resolution repeatable instead of a source edit.
    GameRuntime::ResolveWindowSize(declared, 800, 600, w, h);
    CHECK_EQ(w, uint32_t{800});
    CHECK_EQ(h, uint32_t{600});

    // And the flag alone, over the default.
    GameRuntime::ResolveWindowSize(GameManifest{}, 800, 600, w, h);
    CHECK_EQ(w, uint32_t{800});
    CHECK_EQ(h, uint32_t{600});
}

static void testHalfAnOverrideIsNotAnOverride() {
    // Same rule as the manifest's, for the same reason: a width against
    // somebody else's height is an aspect ratio nobody chose.
    GameManifest declared;
    declared.isGame = true;
    declared.width = 1920;
    declared.height = 1080;

    uint32_t w = 0;
    uint32_t h = 0;
    GameRuntime::ResolveWindowSize(declared, 800, 0, w, h);
    CHECK_EQ(w, uint32_t{1920});
    CHECK_EQ(h, uint32_t{1080});

    GameRuntime::ResolveWindowSize(declared, 0, 600, w, h);
    CHECK_EQ(w, uint32_t{1920});
    CHECK_EQ(h, uint32_t{1080});
}


// --- Fullscreen, declared and overridden ------------------------------------

static void testAManifestCanAskForFullscreen() {
    const auto manifest = GameRuntime::Parse(
        R"({ "Game": true, "Title": "Penumbra", "Width": 1024, "Height": 768, "Fullscreen": true })");
    CHECK(manifest.isGame);
    CHECK_MSG(manifest.fullscreen, "the key is read");
    CHECK_MSG(manifest.width == 1024 && manifest.height == 768,
              "and the size beside it is still the size, the one it comes back to");
}

static void testAManifestThatSaysNothingIsWindowed() {
    // Every manifest written before the key existed.
    CHECK(!GameRuntime::Parse(R"({ "Game": true, "Title": "Old" })").fullscreen);
    CHECK(!GameRuntime::Parse(R"({ "Game": true, "Fullscreen": false })").fullscreen);

    // Read only once the file is a game's, like every key but Game: a stray
    // manifest in a build tree must not take the editor fullscreen.
    const auto stray = GameRuntime::Parse(R"({ "Fullscreen": true })");
    CHECK(!stray.isGame);
    CHECK_MSG(!stray.fullscreen, "a file that is not a game's asks for nothing");
}

static void testFullscreenSurvivesTheRoundTripThePackagerUses() {
    GameManifest manifest;
    manifest.isGame = true;
    manifest.title = "Penumbra";
    manifest.width = 1024;
    manifest.height = 768;
    manifest.fullscreen = true;

    const auto reparsed = GameRuntime::Parse(GameRuntime::Serialize(manifest));
    CHECK(reparsed.isGame);
    CHECK(reparsed.fullscreen);
    CHECK_EQ(reparsed.width, uint32_t{1024});
    CHECK_EQ(reparsed.height, uint32_t{768});
}

static void testAWindowedGameGetsTheManifestItAlwaysDid() {
    // Byte for byte, not merely "no Fullscreen key": this literal is what
    // Serialize wrote before the key existed, and a packaged windowed game
    // must go on shipping exactly it.
    GameManifest manifest;
    manifest.isGame = true;
    manifest.title = "Plain";

    const std::string expected = "{\n"
                                 "  \"Game\": true,\n"
                                 "  \"Title\": \"Plain\",\n"
                                 "  \"StartupScene\": \"assets/scenes/MainScene.scene\"\n"
                                 "}\n";
    const std::string text = GameRuntime::Serialize(manifest);
    CHECK_MSG(text == expected, "a windowed game's manifest is unchanged: " + text);
}

static void testTheWindowedFlagBeatsTheFullscreenFlagWhichBeatsTheManifest() {
    GameManifest windowed;
    windowed.isGame = true;
    GameManifest fullscreen = windowed;
    fullscreen.fullscreen = true;

    // Nobody says anything: the manifest answers.
    CHECK(!GameRuntime::ResolveFullscreen(windowed, false, false));
    CHECK(GameRuntime::ResolveFullscreen(fullscreen, false, false));

    // --fullscreen over a windowed game.
    CHECK(GameRuntime::ResolveFullscreen(windowed, true, false));

    // --windowed over a fullscreen game, which is the one a headless capture
    // needs: without it, a run with --frames covers the desk it runs on.
    CHECK_MSG(!GameRuntime::ResolveFullscreen(fullscreen, false, true),
              "--windowed must beat a fullscreen manifest");
}


// --- The window a game changes while it runs ---------------------------------
//
// WindowControl latches requests in a class with no platform behind it, so the
// deferral - the whole design, since a request comes from inside a tick - is
// checked here against a stand-in window. The GLFW half cannot run in a suite.
namespace {

class StandInWindow final : public WindowControl {
public:
    bool IsFullscreen() const override { return fullscreen; }
    glm::uvec2 WindowSize() const override { return size; }
    std::vector<DisplayMode> DisplayModes() const override { return {}; }
    DisplayMode DesktopMode() const override { return {}; }

    bool fullscreen{false};
    glm::uvec2 size{1280u, 720u};
};

} // namespace

static void testARequestWaitsToBeTaken() {
    StandInWindow window;
    CHECK_MSG(!window.Pending().Any(), "nothing is asked for at first");

    window.SetFullscreen(true);
    CHECK(window.Pending().Any());
    CHECK(window.Pending().setFullscreen && window.Pending().fullscreen);
    CHECK_MSG(!window.IsFullscreen(),
              "asking changes nothing about what the window is until it is applied");

    const WindowControl::Requests taken = window.TakeRequests();
    CHECK(taken.setFullscreen && taken.fullscreen);
    CHECK_MSG(!taken.setWindowedSize, "and nothing that was not asked for");
    CHECK_MSG(!window.Pending().Any(), "taking the requests forgets them");
    CHECK_MSG(!window.TakeRequests().Any(), "so a second frame applies nothing again");
}

static void testTheLastRequestBeforeTheFrameWins() {
    // A toggle pressed twice between two frames is back where it started, not
    // applied twice; a size asked for twice is the second size.
    StandInWindow window;
    window.SetFullscreen(true);
    window.SetFullscreen(false);
    CHECK(window.SetWindowedSize(1280, 720));
    CHECK(window.SetWindowedSize(1024, 768));

    const WindowControl::Requests taken = window.TakeRequests();
    CHECK(taken.setFullscreen);
    CHECK_MSG(!taken.fullscreen, "the later answer");
    CHECK(taken.setWindowedSize);
    CHECK_EQ(taken.windowedSize.x, 1024u);
    CHECK_EQ(taken.windowedSize.y, 768u);
}

static void testAnUnusableWindowedSizeIsRefusedAndLatchesNothing() {
    StandInWindow window;
    CHECK_MSG(!window.SetWindowedSize(8, 8), "too small to see");
    CHECK_MSG(!window.SetWindowedSize(1920, 99999), "too large to be meant");
    CHECK_MSG(!window.SetWindowedSize(0, 720), "half a size");
    CHECK_MSG(!window.Pending().Any(), "and a refusal asks for nothing");

    // The manifest's own bounds, inclusive, so the three places a size comes
    // from cannot disagree about which ones exist.
    CHECK(window.SetWindowedSize(GameManifest::kMinimumExtent, GameManifest::kMinimumExtent));
    CHECK(window.SetWindowedSize(GameManifest::kMaximumExtent, GameManifest::kMaximumExtent));
    CHECK(!WindowControl::IsUsableWindowSize(GameManifest::kMinimumExtent - 1, 720));
    CHECK(!WindowControl::IsUsableWindowSize(1280, GameManifest::kMaximumExtent + 1));

    // A refused request does not cancel one already accepted.
    CHECK(window.SetWindowedSize(1280, 720));
    CHECK(!window.SetWindowedSize(4, 4));
    const WindowControl::Requests taken = window.TakeRequests();
    CHECK(taken.setWindowedSize);
    CHECK_EQ(taken.windowedSize.x, 1280u);
    CHECK_EQ(taken.windowedSize.y, 720u);
}

static void testHidingTheCursorIsTheSameRequestInputArbitrates() {
    // Through Input and nowhere else, so the editor's hold on the pointer and
    // a window without focus still veto it. And a locked pointer is left
    // locked either way: hiding the cursor must not end a mouse-look.
    StandInWindow window;

    Input::SetCursorMode(CursorMode::Normal);
    window.SetCursorVisible(false);
    CHECK(Input::RequestedCursorMode() == CursorMode::Hidden);
    window.SetCursorVisible(true);
    CHECK(Input::RequestedCursorMode() == CursorMode::Normal);

    Input::SetCursorMode(CursorMode::Locked);
    window.SetCursorVisible(false);
    CHECK_MSG(Input::RequestedCursorMode() == CursorMode::Locked, "hiding leaves a lock alone");
    window.SetCursorVisible(true);
    CHECK_MSG(Input::RequestedCursorMode() == CursorMode::Locked, "and so does showing");

    CHECK_MSG(!window.Pending().Any(), "the pointer is not a window request");
    Input::SetCursorMode(CursorMode::Normal);
}

static void testTheModesAMenuListsAreTheTrueColourOnesOnceEach() {
    using Mode = WindowControl::VideoMode;
    const std::vector<Mode> reported = {
        {1920, 1080, 8, 8, 8, 60},
        {800, 600, 5, 6, 5, 60},     // 16-bit: dropped
        {1280, 720, 8, 8, 8, 60},
        {1920, 1080, 8, 8, 8, 60},   // listed twice by the platform
        {1920, 1080, 8, 8, 8, 144},
        {1280, 1024, 8, 8, 8, 60},
        {1280, 720, 5, 6, 5, 60},    // 16-bit twin of a kept mode
        {0, 0, 8, 8, 8, 60},         // nothing
    };

    const std::vector<DisplayMode> modes = WindowControl::SelectDisplayModes(reported);
    const std::vector<DisplayMode> expected = {
        {1280, 720, 60}, {1280, 1024, 60}, {1920, 1080, 60}, {1920, 1080, 144}};
    CHECK_EQ(modes.size(), expected.size());
    CHECK_MSG(modes == expected, "smallest first, then by rate, each once");
    CHECK(WindowControl::SelectDisplayModes({}).empty());
}

static void testAWindowGoesFullscreenOnTheMonitorHoldingMostOfIt() {
    using Rect = WindowControl::ScreenRect;
    // A 1080p screen, a 1440p one to its right, and one to the left of the
    // primary, which is where negative desktop coordinates come from.
    const std::vector<Rect> monitors = {
        {0, 0, 1920, 1080}, {1920, 0, 2560, 1440}, {-1920, 0, 1920, 1080}};

    CHECK_EQ(WindowControl::MonitorUnder({100, 100, 800, 600}, monitors), 0);

    // Straddling: 120 columns on the first, 680 on the second.
    CHECK_EQ(WindowControl::MonitorUnder({1800, 100, 800, 600}, monitors), 1);

    CHECK_EQ(WindowControl::MonitorUnder({-1000, 200, 800, 600}, monitors), 2);

    // Split exactly in half: the first, not whichever was looked at last.
    CHECK_EQ(WindowControl::MonitorUnder({1520, 100, 800, 600}, monitors), 0);

    // Off every screen: nobody's, and the caller takes the primary.
    CHECK_EQ(WindowControl::MonitorUnder({-9000, -9000, 100, 100}, monitors), -1);
    CHECK_EQ(WindowControl::MonitorUnder({100, 100, 800, 600}, {}), -1);

    // Touching an edge is not overlapping it.
    CHECK_EQ(WindowControl::MonitorUnder({1920, 1440, 100, 100}, monitors), -1);
}

// WHERE A GAME'S ENGINE FILES COME FROM: ChooseAssetRoot, asked of made-up
// directories. The filesystem is never touched - the predicate is a set - so
// these hold on any machine and move nobody's working directory.
namespace {

namespace fs = std::filesystem;

// The candidates a game in its own repository sees: its executable deep in the
// game's build tree, launched from the game's root, and the engine checkout
// the build named.
const fs::path kExecutable = fs::path("/repos/game/build/game");
const fs::path kGameRoot = fs::path("/repos/game");
const fs::path kEngineRoot = fs::path("/repos/game/engine");

struct Layout {
    std::set<fs::path> holding;
    std::vector<fs::path> asked;

    std::function<bool(const fs::path&)> Predicate() {
        return [this](const fs::path& candidate) {
            asked.push_back(candidate);
            return holding.count(candidate) > 0;
        };
    }
};

bool Asked(const Layout& layout, const fs::path& candidate) {
    for (const fs::path& p : layout.asked) {
        if (p == candidate) return true;
    }
    return false;
}

} // namespace

static void testAPackagedFolderOutranksEverything() {
    // Rule 1. A packaged copy of a game that was built against a checkout still
    // carries that checkout's path; it must use its own folder, or it would go
    // looking for its shaders in somebody's source tree.
    Layout layout;
    layout.holding = {kExecutable, kGameRoot, kEngineRoot};
    const AssetRootChoice choice =
        ChooseAssetRoot(kExecutable, kGameRoot, kEngineRoot, layout.Predicate());
    CHECK(choice.source == AssetRootSource::PackagedFolder);
    CHECK(choice.root == kExecutable);

    Layout packagedOnly;
    packagedOnly.holding = {kExecutable, kEngineRoot};
    const AssetRootChoice packaged =
        ChooseAssetRoot(kExecutable, kGameRoot, kEngineRoot, packagedOnly.Predicate());
    CHECK_MSG(packaged.source == AssetRootSource::PackagedFolder,
              "the configured root must never outrank the executable's own folder");
}

static void testARunFromTheEnginesRootIsLeftAsItWas() {
    // Rule 2, and the one that keeps every existing run identical: a game run
    // from a directory that holds the engine's files resolves from it, and the
    // configured root is not even asked about.
    Layout layout;
    layout.holding = {kGameRoot, kEngineRoot};
    const AssetRootChoice choice =
        ChooseAssetRoot(kExecutable, kGameRoot, kEngineRoot, layout.Predicate());
    CHECK(choice.source == AssetRootSource::WorkingDirectory);
    CHECK_MSG(choice.root.native() == kGameRoot.native(),
              "the working directory must come back exactly as it was given");
    CHECK_MSG(!Asked(layout, kEngineRoot),
              "a working directory that holds the files must decide on its own");
}

static void testAGameInItsOwnRepositoryFindsTheEngine() {
    // Rule 3: launched from the game's root, which has no assets/shaders, the
    // game resolves from the engine checkout its build named - wherever it was
    // launched from.
    Layout layout;
    layout.holding = {kEngineRoot};
    const AssetRootChoice choice =
        ChooseAssetRoot(kExecutable, kGameRoot, kEngineRoot, layout.Predicate());
    CHECK(choice.source == AssetRootSource::ConfiguredRoot);
    CHECK(choice.root == kEngineRoot);

    const fs::path elsewhere = fs::path("/tmp/anywhere");
    const AssetRootChoice fromElsewhere =
        ChooseAssetRoot(kExecutable, elsewhere, kEngineRoot, layout.Predicate());
    CHECK(fromElsewhere.source == AssetRootSource::ConfiguredRoot);
    CHECK(fromElsewhere.root == kEngineRoot);
}

static void testARootWithoutTheFilesIsNoRoot() {
    // A configured root that does not hold the shaders - a checkout moved or
    // deleted since the build - is passed over, and the run fails on its first
    // .spv from the working directory, exactly as a run with no engine files
    // always has.
    Layout layout;
    const AssetRootChoice choice =
        ChooseAssetRoot(kExecutable, kGameRoot, kEngineRoot, layout.Predicate());
    CHECK(choice.source == AssetRootSource::Unresolved);
    CHECK(choice.root.native() == kGameRoot.native());
    CHECK(Asked(layout, kEngineRoot));
}

static void testTheEnginesOwnBuildNamesNoRoot() {
    // The engine built for itself bakes no root. With none, there is no third
    // rule: nothing is asked about an empty path, which would otherwise be a
    // question about the filesystem root, and the answer is the working
    // directory, as it was before the rule existed.
    Layout layout;
    const AssetRootChoice choice =
        ChooseAssetRoot(kExecutable, kGameRoot, fs::path{}, layout.Predicate());
    CHECK(choice.source == AssetRootSource::Unresolved);
    CHECK(choice.root.native() == kGameRoot.native());
    CHECK_MSG(!Asked(layout, fs::path{}), "an empty root must never be asked about");

    Layout unknownExecutable;
    unknownExecutable.holding = {kGameRoot};
    const AssetRootChoice fromRoot =
        ChooseAssetRoot(fs::path{}, kGameRoot, fs::path{}, unknownExecutable.Predicate());
    CHECK(fromRoot.source == AssetRootSource::WorkingDirectory);
    CHECK_MSG(!Asked(unknownExecutable, fs::path{}),
              "an unknown executable directory must not become the filesystem root");

    CHECK(!HoldsEngineAssets(fs::path{}));
}

static void runTests() {
    testRoundTrip();
    testTitleWithQuotesSurvives();
    testAbsentGameKeyIsNotAGame();
    testFalseGameKeyIsNotAGame();
    testGarbageIsNotAGame();
    testDefaultsFillIn();

    testAManifestCanDeclareItsWindow();
    testAManifestThatSaysNothingLeavesTheDefault();
    testHalfASizeIsNotASize();
    testAnUnusableSizeFallsBackRatherThanOpeningIt();
    testTheSizeSurvivesTheRoundTripThePackagerUses();
    testAGameThatStatedNoSizeGetsTheManifestItAlwaysDid();

    testTheFlagBeatsTheManifestWhichBeatsTheDefault();
    testHalfAnOverrideIsNotAnOverride();

    testAManifestCanAskForFullscreen();
    testAManifestThatSaysNothingIsWindowed();
    testFullscreenSurvivesTheRoundTripThePackagerUses();
    testAWindowedGameGetsTheManifestItAlwaysDid();
    testTheWindowedFlagBeatsTheFullscreenFlagWhichBeatsTheManifest();

    testARequestWaitsToBeTaken();
    testTheLastRequestBeforeTheFrameWins();
    testAnUnusableWindowedSizeIsRefusedAndLatchesNothing();
    testHidingTheCursorIsTheSameRequestInputArbitrates();
    testTheModesAMenuListsAreTheTrueColourOnesOnceEach();
    testAWindowGoesFullscreenOnTheMonitorHoldingMostOfIt();

    testAPackagedFolderOutranksEverything();
    testARunFromTheEnginesRootIsLeftAsItWas();
    testAGameInItsOwnRepositoryFindsTheEngine();
    testARootWithoutTheFilesIsNoRoot();
    testTheEnginesOwnBuildNamesNoRoot();
}

TEST_MAIN("test_gameruntime", 50)
