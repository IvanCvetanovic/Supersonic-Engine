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

static void runTests() {
    testRoundTrip();
    testTitleWithQuotesSurvives();
    testAbsentGameKeyIsNotAGame();
    testFalseGameKeyIsNotAGame();
    testGarbageIsNotAGame();
    testDefaultsFillIn();
}

TEST_MAIN("test_gameruntime", 9)
