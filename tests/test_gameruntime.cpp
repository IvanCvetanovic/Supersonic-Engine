// Regression tests for the packaged-game manifest.
//
// The manifest is the only thing separating a shipped game from the editor:
// the binary is identical, and packaging is a copy rather than a build. If it
// does not parse, the player gets an editor - which is precisely what
// packaging used to produce every time.

#include "TestHarness.hpp"
#include "core/GameRuntime.hpp"

#include <string>

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
}

TEST_MAIN("test_gameruntime", 50)
