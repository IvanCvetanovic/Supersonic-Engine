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
#include "core/DynamicResolution.hpp"
#include "core/GameRuntime.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"
#include "core/WindowControl.hpp"
#include "platform/ExecutablePath.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <random>
#include <regex>
#include <sstream>
#include <set>
#include <string>
#include <thread>
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

// --hidden, as the window starts. SupersonicApp made this decision inline, so
// the two terms that keep a hidden run from switching a monitor or sizing
// itself to one had never been checked by anything.
static void testAHiddenRunStartsWindowedAndUnfitted() {
    GameManifest fullscreen;
    fullscreen.isGame = true;
    fullscreen.fullscreen = true;
    GameManifest fitted;
    fitted.isGame = true;
    fitted.fitWindowToMonitor = 0.85f;

    LaunchOptions plain;
    LaunchOptions hidden;
    hidden.hidden = true;

    // The controls: without --hidden the manifest answers, so the flag is the
    // only thing the hidden cases below change.
    CHECK(GameRuntime::ResolveStartWindow(fullscreen, plain).fullscreen);
    CHECK_EQ(GameRuntime::ResolveStartWindow(fitted, plain).fitFraction, 0.85f);
    CHECK(!GameRuntime::ResolveStartWindow(fitted, plain).fullscreen);

    // A game that ships fullscreen starts in a window when hidden.
    const GameRuntime::StartWindow hiddenFullscreen =
        GameRuntime::ResolveStartWindow(fullscreen, hidden);
    CHECK_MSG(!hiddenFullscreen.fullscreen, "--hidden never covers a monitor");
    CHECK_EQ(hiddenFullscreen.fitFraction, 0.0f);

    // And a fitted one keeps the size it was created at.
    CHECK_MSG(GameRuntime::ResolveStartWindow(fitted, hidden).fitFraction == 0.0f,
              "--hidden fits nothing: the capture's size is the command line's");

    // Both at once: still a plain window.
    GameManifest both = fitted;
    both.fullscreen = true;
    const GameRuntime::StartWindow hiddenBoth = GameRuntime::ResolveStartWindow(both, hidden);
    CHECK(!hiddenBoth.fullscreen && hiddenBoth.fitFraction == 0.0f);

    // The rules that were there before --hidden, unchanged: --window names the
    // size and fits nothing, a fullscreen start fits nothing, and --windowed
    // over a fitted fullscreen manifest fits it.
    LaunchOptions sized;
    sized.windowWidth = 1920;
    sized.windowHeight = 1061;
    CHECK_EQ(GameRuntime::ResolveStartWindow(fitted, sized).fitFraction, 0.0f);
    CHECK_EQ(GameRuntime::ResolveStartWindow(both, plain).fitFraction, 0.0f);
    CHECK(GameRuntime::ResolveStartWindow(both, plain).fullscreen);
    LaunchOptions windowed;
    windowed.windowed = true;
    CHECK_EQ(GameRuntime::ResolveStartWindow(both, windowed).fitFraction, 0.85f);

    // What the three-argument form answers is what ResolveStartWindow answers
    // when --hidden is not given, so a caller of either agrees with the other.
    CHECK(GameRuntime::ResolveFullscreen(fullscreen, false, false) ==
          GameRuntime::ResolveStartWindow(fullscreen, plain).fullscreen);
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
    std::vector<DisplayMode> DisplayModes() const override { return modes; }
    DisplayMode DesktopMode() const override { return desktop; }

    bool fullscreen{false};
    glm::uvec2 size{1280u, 720u};
    // No monitor unless a check gives it one.
    std::vector<DisplayMode> modes;
    DisplayMode desktop;
};

// A 1080p monitor at 144 Hz on the desktop, which offers 800x600 only at 60
// and 75 Hz, and 1280x720 at 60 and 144 Hz. Filled in rather than returned: a
// WindowControl cannot be copied.
void PutOnADesktopMonitor(StandInWindow& window) {
    window.modes = {{800, 600, 60},    {800, 600, 75},     {1280, 720, 60},
                    {1280, 720, 144},  {1920, 1080, 60},   {1920, 1080, 144}};
    window.desktop = {1920, 1080, 144};
}

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

static void testAFullscreenModeIsAFullscreenRequestThatCarriesItsSize() {
    StandInWindow window;
    PutOnADesktopMonitor(window);
    CHECK(window.SetFullscreenMode(1280, 720));
    CHECK(window.Pending().Any());
    CHECK_MSG(!window.IsFullscreen(), "asking changes nothing until it is applied");

    WindowControl::Requests taken = window.TakeRequests();
    CHECK(taken.setFullscreen && taken.fullscreen);
    CHECK_EQ(taken.fullscreenMode.x, 1280u);
    CHECK_EQ(taken.fullscreenMode.y, 720u);
    CHECK_MSG(!taken.setWindowedSize, "a mode is not a windowed size");
    CHECK_MSG(!window.Pending().Any(), "taking the requests forgets the mode too");

    // SetFullscreen(true) is the monitor's current mode, as it always was:
    // no size rides along with it.
    window.SetFullscreen(true);
    taken = window.TakeRequests();
    CHECK(taken.setFullscreen && taken.fullscreen);
    CHECK_EQ(taken.fullscreenMode.x, 0u);
    CHECK_EQ(taken.fullscreenMode.y, 0u);
}

static void testTheLastFullscreenRequestWinsWithOrWithoutAMode() {
    StandInWindow window;
    PutOnADesktopMonitor(window);

    // A mode, then plain fullscreen: the plain one, which switches nothing.
    CHECK(window.SetFullscreenMode(800, 600));
    window.SetFullscreen(true);
    WindowControl::Requests taken = window.TakeRequests();
    CHECK(taken.fullscreen);
    CHECK_MSG(taken.fullscreenMode == glm::uvec2(0u), "the later request drops the mode");

    // A mode, then windowed: windowed.
    CHECK(window.SetFullscreenMode(800, 600));
    window.SetFullscreen(false);
    taken = window.TakeRequests();
    CHECK(taken.setFullscreen);
    CHECK(!taken.fullscreen);
    CHECK(taken.fullscreenMode == glm::uvec2(0u));

    // Windowed, then a mode: fullscreen at it.
    window.SetFullscreen(false);
    CHECK(window.SetFullscreenMode(1280, 720));
    taken = window.TakeRequests();
    CHECK(taken.fullscreen);
    CHECK(taken.fullscreenMode == glm::uvec2(1280u, 720u));

    // A windowed size beside a mode is kept: it is the size to come back at.
    CHECK(window.SetWindowedSize(1024, 768));
    CHECK(window.SetFullscreenMode(1280, 720));
    taken = window.TakeRequests();
    CHECK(taken.setWindowedSize && taken.windowedSize == glm::uvec2(1024u, 768u));
    CHECK(taken.fullscreenMode == glm::uvec2(1280u, 720u));
}

static void testAModeTheMonitorDoesNotOfferIsRefusedAndLatchesNothing() {
    StandInWindow window;
    PutOnADesktopMonitor(window);
    CHECK_MSG(!window.SetFullscreenMode(1024, 768), "not in the monitor's list");
    CHECK_MSG(!window.SetFullscreenMode(0, 600), "half a size");
    CHECK_MSG(!window.SetFullscreenMode(0, 0), "no size");
    CHECK_MSG(!window.Pending().Any(), "and a refusal asks for nothing");

    // A refused mode does not cancel one already accepted.
    CHECK(window.SetFullscreenMode(800, 600));
    CHECK(!window.SetFullscreenMode(1600, 900));
    const WindowControl::Requests taken = window.TakeRequests();
    CHECK(taken.fullscreenMode == glm::uvec2(800u, 600u));

    // With no monitor to ask there is no mode to have.
    StandInWindow nowhere;
    CHECK(!nowhere.SetFullscreenMode(1280, 720));
    CHECK(!nowhere.Pending().Any());

    // The desktop's own size is always possible, listed or not: covering the
    // monitor at the mode it is already in needs no list.
    StandInWindow unlisted;
    PutOnADesktopMonitor(unlisted);
    unlisted.modes = {{800, 600, 60}};
    CHECK(unlisted.SetFullscreenMode(1920, 1080));
    CHECK(unlisted.TakeRequests().fullscreenMode == glm::uvec2(1920u, 1080u));
}

static void testAFullscreenModeRunsAtTheDesktopsRateWhenItCan() {
    StandInWindow window;
    PutOnADesktopMonitor(window);
    const std::vector<DisplayMode>& modes = window.modes;
    const DisplayMode& desktop = window.desktop;

    // Offered at the desktop's 144 Hz: that, not merely the highest.
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1280, 720) ==
          (DisplayMode{1280, 720, 144}));
    // Not offered at 144: the highest it is offered at.
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 800, 600) ==
          (DisplayMode{800, 600, 75}));
    // The desktop's size is the desktop's mode itself, so choosing it
    // switches nothing.
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1920, 1080) == desktop);
    CHECK(WindowControl::ChooseFullscreenMode({}, desktop, 1920, 1080) == desktop);

    // A desktop at 60 Hz picks 60 where it is offered, not the higher rate.
    const DisplayMode at60{1920, 1080, 60};
    CHECK(WindowControl::ChooseFullscreenMode(modes, at60, 1280, 720) ==
          (DisplayMode{1280, 720, 60}));

    // Not offered at all: zeroes.
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1024, 768) == DisplayMode{});
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 0, 0) == DisplayMode{});
    CHECK(WindowControl::ChooseFullscreenMode({}, DisplayMode{}, 1280, 720) == DisplayMode{});
}

// --- A refresh rate named, and a window fitted to its monitor (opt-in) --------

// A 1920x1200 panel whose desktop runs at 60 Hz and which also offers 165 Hz
// at its own size (listed), 1280x800 at 60 and 120, and 800x600 at 60 only. The
// desktop's own mode is left out of the list, as a platform may leave it.
static void PutOnAFastPanel(StandInWindow& window) {
    window.modes = {{800, 600, 60}, {1280, 800, 60}, {1280, 800, 120}, {1920, 1200, 165}};
    window.desktop = {1920, 1200, 60};
}

static void testTheHighestRateIsChosenFromTheListAndTheDesktopAlike() {
    StandInWindow window;
    PutOnAFastPanel(window);
    const std::vector<DisplayMode>& modes = window.modes;
    const DisplayMode& desktop = window.desktop;
    constexpr uint32_t kHighest = WindowControl::kHighestRefreshRate;

    // At the desktop's own size the desktop's 60 Hz is not the answer just
    // because it is the desktop's: 165 is listed there.
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1920, 1200, kHighest) ==
          (DisplayMode{1920, 1200, 165}));
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1280, 800, kHighest) ==
          (DisplayMode{1280, 800, 120}));
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 800, 600, kHighest) ==
          (DisplayMode{800, 600, 60}));
    // The desktop's mode counts although unlisted, and wins when nothing
    // listed at its size is faster.
    CHECK(WindowControl::ChooseFullscreenMode({}, desktop, 1920, 1200, kHighest) == desktop);
    // A size neither listed nor the desktop's has no highest.
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1024, 768, kHighest) == DisplayMode{});
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 0, 0, kHighest) == DisplayMode{});

    // Step 23's rule is what the call without a rate still gives: the desktop
    // itself at its size, its rate where offered, else the highest.
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1920, 1200) == desktop);
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1280, 800) == (DisplayMode{1280, 800, 60}));
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1920, 1200, WindowControl::kDesktopRefreshRate) ==
          desktop);
}

static void testANamedRateIsThatRateOrNothing() {
    StandInWindow window;
    PutOnAFastPanel(window);
    const std::vector<DisplayMode>& modes = window.modes;
    const DisplayMode& desktop = window.desktop;

    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1280, 800, 120) == (DisplayMode{1280, 800, 120}));
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1280, 800, 60) == (DisplayMode{1280, 800, 60}));
    // The desktop's own size and rate, unlisted: offered.
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1920, 1200, 60) == desktop);
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1920, 1200, 165) == (DisplayMode{1920, 1200, 165}));
    // Not at that size, or not at all: zeroes, never the nearest.
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 800, 600, 120) == DisplayMode{});
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1920, 1200, 144) == DisplayMode{});
    CHECK(WindowControl::ChooseFullscreenMode(modes, desktop, 1024, 768, 60) == DisplayMode{});
}

static void testAFullscreenRequestCarriesItsRate() {
    StandInWindow window;
    PutOnAFastPanel(window);

    CHECK(window.SetFullscreenMode(1920, 1200, WindowControl::kHighestRefreshRate));
    WindowControl::Requests taken = window.TakeRequests();
    CHECK(taken.setFullscreen && taken.fullscreen);
    CHECK(taken.fullscreenMode == glm::uvec2(1920u, 1200u));
    CHECK_EQ(taken.fullscreenRate, WindowControl::kHighestRefreshRate);

    CHECK(window.SetFullscreenMode(1280, 800, 120));
    taken = window.TakeRequests();
    CHECK_EQ(taken.fullscreenRate, 120u);

    // Without a rate, the request is Step 23's to the byte: rate zero.
    CHECK(window.SetFullscreenMode(1280, 800));
    taken = window.TakeRequests();
    CHECK_EQ(taken.fullscreenRate, WindowControl::kDesktopRefreshRate);
    CHECK_EQ(WindowControl::kDesktopRefreshRate, 0u);

    // A later plain request drops the rate with the size.
    CHECK(window.SetFullscreenMode(1280, 800, 120));
    window.SetFullscreen(true);
    taken = window.TakeRequests();
    CHECK(taken.fullscreenMode == glm::uvec2(0u));
    CHECK_EQ(taken.fullscreenRate, WindowControl::kDesktopRefreshRate);

    // A rate the monitor does not offer at the size is refused, and a refusal
    // leaves an accepted request alone.
    CHECK(window.SetFullscreenMode(1280, 800, 60));
    CHECK_MSG(!window.SetFullscreenMode(800, 600, 120), "800x600 is offered at 60 only");
    CHECK_MSG(!window.SetFullscreenMode(1920, 1200, 144), "nor 144 at the panel's size");
    taken = window.TakeRequests();
    CHECK(taken.fullscreenMode == glm::uvec2(1280u, 800u));
    CHECK_EQ(taken.fullscreenRate, 60u);
    CHECK(!window.Pending().Any());

    // A zeroed Requests is what it always was.
    const WindowControl::Requests none;
    CHECK(!none.Any());
    CHECK_EQ(none.fullscreenRate, WindowControl::kDesktopRefreshRate);
    CHECK(!none.centreWindow && !none.fitWindow && !none.setRefreshRate);
}

static void testTheRatesOfASizeAreListedOnceLowestFirst() {
    StandInWindow window;
    PutOnAFastPanel(window);
    using Rates = std::vector<uint32_t>;
    CHECK(WindowControl::RefreshRatesAt(window.modes, window.desktop, 1920, 1200) == (Rates{60, 165}));
    CHECK(WindowControl::RefreshRatesAt(window.modes, window.desktop, 1280, 800) == (Rates{60, 120}));
    CHECK(WindowControl::RefreshRatesAt(window.modes, window.desktop, 800, 600) == (Rates{60}));
    CHECK(WindowControl::RefreshRatesAt(window.modes, window.desktop, 1024, 768).empty());
    CHECK(WindowControl::RefreshRatesAt(window.modes, window.desktop, 0, 0).empty());
    // The desktop's rate listed too is still one rate; a rate of 0 (unknown,
    // as a virtual X server reports) is none.
    const std::vector<DisplayMode> twice = {{1920, 1200, 60}, {1920, 1200, 0}, {1920, 1200, 60}};
    CHECK(WindowControl::RefreshRatesAt(twice, window.desktop, 1920, 1200) == (Rates{60}));
    CHECK(WindowControl::RefreshRatesAt({{3000, 1600, 0}}, DisplayMode{3000, 1600, 0}, 3000, 1600).empty());
}

static void testAFittedWindowIsTheMonitorsShapeInsideItsWorkArea() {
    using Rect = WindowControl::ScreenRect;
    // A 1920x1200 panel above a 48-pixel taskbar: the height binds, and the
    // width follows the panel's 16:10.
    CHECK(WindowControl::FitWindowedSize(Rect{0, 0, 1920, 1152}, DisplayMode{1920, 1200, 60}, 0.85f) ==
          glm::uvec2(1566u, 979u));
    // 1080p over a taskbar.
    CHECK(WindowControl::FitWindowedSize(Rect{0, 0, 1920, 1032}, DisplayMode{1920, 1080, 60}, 0.85f) ==
          glm::uvec2(1559u, 877u));
    // A taskbar down the side: the width binds.
    CHECK(WindowControl::FitWindowedSize(Rect{62, 0, 1858, 1080}, DisplayMode{1920, 1080, 60}, 0.85f) ==
          glm::uvec2(1579u, 888u));
    // The whole of it.
    CHECK(WindowControl::FitWindowedSize(Rect{0, 0, 1920, 1080}, DisplayMode{1920, 1080, 60}, 1.0f) ==
          glm::uvec2(1920u, 1080u));
    // No work area: the desktop's size stands in. No desktop: the area's shape.
    CHECK(WindowControl::FitWindowedSize(Rect{}, DisplayMode{1920, 1200, 60}, 0.5f) == glm::uvec2(960u, 600u));
    CHECK(WindowControl::FitWindowedSize(Rect{0, 0, 1000, 800}, DisplayMode{}, 0.5f) == glm::uvec2(500u, 400u));
    // Nothing to measure, or a fraction that is not one: zeroes.
    CHECK(WindowControl::FitWindowedSize(Rect{}, DisplayMode{}, 0.85f) == glm::uvec2(0u));
    CHECK(WindowControl::FitWindowedSize(Rect{0, 0, 1920, 1080}, DisplayMode{1920, 1080, 60}, 0.0f) ==
          glm::uvec2(0u));
    CHECK(WindowControl::FitWindowedSize(Rect{0, 0, 1920, 1080}, DisplayMode{1920, 1080, 60}, 1.5f) ==
          glm::uvec2(0u));
    // Never a window too small to use.
    CHECK(WindowControl::FitWindowedSize(Rect{0, 0, 40, 30}, DisplayMode{40, 30, 60}, 0.5f) ==
          glm::uvec2(GameManifest::kMinimumExtent, GameManifest::kMinimumExtent));
}

static void testACentredWindowKeepsItsTitleBarOnScreen() {
    using Rect = WindowControl::ScreenRect;
    const WindowControl::FrameInsets frame{8, 31, 8, 8};
    // 1566x979 on the 1920x1152 work area: the whole window, frame included,
    // centred - the client area 31 pixels below the window's top.
    CHECK(WindowControl::CentredWindowPosition(Rect{0, 0, 1920, 1152}, glm::uvec2(1566u, 979u), frame) ==
          glm::ivec2(177, 98));
    // On a monitor left of the primary, and on one below a top taskbar.
    CHECK(WindowControl::CentredWindowPosition(Rect{-1920, 0, 1920, 1032}, glm::uvec2(1280u, 720u), frame) ==
          glm::ivec2(-1600, 167));
    CHECK(WindowControl::CentredWindowPosition(Rect{0, 40, 1920, 1040}, glm::uvec2(1280u, 720u), {}) ==
          glm::ivec2(320, 200));
    // Taller and wider than the work area: the title bar and the left edge
    // stay on it, the rest hangs off the bottom and the right.
    CHECK(WindowControl::CentredWindowPosition(Rect{0, 0, 1920, 1152}, glm::uvec2(1920u, 1200u), frame) ==
          glm::ivec2(8, 31));
}

static void testAFitAndASizeAreOneRequestTheLaterWins() {
    StandInWindow window;
    CHECK(window.FitWindowToMonitor(0.85f));
    WindowControl::Requests taken = window.TakeRequests();
    CHECK(taken.Any());
    CHECK(taken.fitWindow);
    CHECK(taken.fitFraction == 0.85f);
    CHECK(!taken.setWindowedSize);
    CHECK_MSG(!taken.setFullscreen, "a fitted window is a size, not a fullscreen change");

    CHECK(window.FitWindowToMonitor(0.85f));
    CHECK(window.SetWindowedSize(1280, 720, true));
    taken = window.TakeRequests();
    CHECK(!taken.fitWindow);
    CHECK(taken.setWindowedSize && taken.windowedSize == glm::uvec2(1280u, 720u));
    CHECK(taken.centreWindow);

    CHECK(window.SetWindowedSize(1280, 720));
    CHECK(window.FitWindowToMonitor(0.5f));
    taken = window.TakeRequests();
    CHECK(taken.fitWindow && !taken.setWindowedSize);
    CHECK(taken.fitFraction == 0.5f);

    // SetWindowedSize without `centre` is the request it always was.
    CHECK(window.SetWindowedSize(1024, 768));
    taken = window.TakeRequests();
    CHECK(taken.setWindowedSize && !taken.centreWindow && !taken.fitWindow);

    // A fraction that is not one is refused and latches nothing.
    CHECK(!window.FitWindowToMonitor(0.0f));
    CHECK(!window.FitWindowToMonitor(-0.5f));
    CHECK(!window.FitWindowToMonitor(1.01f));
    CHECK(!window.Pending().Any());

    // Beside a way out of fullscreen, both are kept: ApplyPending sizes first.
    CHECK(window.FitWindowToMonitor(0.85f));
    window.SetFullscreen(false);
    taken = window.TakeRequests();
    CHECK(taken.fitWindow && taken.setFullscreen && !taken.fullscreen);
}

static void testAPreferredRateIsItsOwnRequest() {
    StandInWindow window;
    window.SetPreferredRefreshRate(WindowControl::kHighestRefreshRate);
    CHECK(window.Pending().Any());
    WindowControl::Requests taken = window.TakeRequests();
    CHECK(taken.setRefreshRate);
    CHECK_EQ(taken.refreshRate, WindowControl::kHighestRefreshRate);
    CHECK_MSG(!taken.setFullscreen && !taken.setWindowedSize && !taken.fitWindow,
              "a preferred rate changes nothing about the window's size or mode");

    window.SetPreferredRefreshRate(60);
    window.SetPreferredRefreshRate(WindowControl::kDesktopRefreshRate);
    taken = window.TakeRequests();
    CHECK_MSG(taken.refreshRate == WindowControl::kDesktopRefreshRate, "the last one, withdrawn");
    CHECK(!window.Pending().Any());
}

static void testAManifestFitsItsWindowOnlyWhenItAsks() {
    // Zero, the default, and nothing the packager writes or reads mentions it.
    const GameManifest plain;
    CHECK(plain.fitWindowToMonitor == 0.0f);
    GameManifest fitted;
    fitted.isGame = true;
    fitted.fitWindowToMonitor = 0.85f;
    const GameManifest back = GameRuntime::Parse(GameRuntime::Serialize(fitted));
    CHECK_MSG(back.fitWindowToMonitor == 0.0f, "a game's main sets it; the manifest's text never carries it");
    GameManifest unfitted = fitted;
    unfitted.fitWindowToMonitor = 0.0f;
    CHECK(GameRuntime::Serialize(fitted) == GameRuntime::Serialize(unfitted));
}

static void testTheStartupPumpIsOffByDefaultAndNeverInTheManifestsText() {
    // Off by default, like every opt-in beside it: a game that never heard of it
    // starts as it always did. And, as fitWindowToMonitor, a game's main sets
    // it - the manifest's text neither carries nor reads it. (The pump itself
    // needs a window and a device: it is exercised by the game's real start.)
    const GameManifest plain;
    CHECK(!plain.pumpEventsDuringStartup);
    GameManifest pumping;
    pumping.isGame = true;
    pumping.pumpEventsDuringStartup = true;
    GameManifest quiet = pumping;
    quiet.pumpEventsDuringStartup = false;
    CHECK_MSG(GameRuntime::Serialize(pumping) == GameRuntime::Serialize(quiet),
              "the manifest's text must not carry the flag");
    CHECK_MSG(!GameRuntime::Parse(GameRuntime::Serialize(pumping)).pumpEventsDuringStartup,
              "nor read it back");
}

// DYNAMIC RESOLUTION and the sprite-only scene pipelines: two opt-ins of a game's main, off by default and never in
// the manifest's text (the controller's own arithmetic is below; the resize it asks for needs a device and is
// exercised by a game's real run).
static void testTheScenePerformanceOptInsAreOffByDefaultAndNeverInTheManifestsText() {
    const GameManifest plain;
    CHECK(!plain.dynamicResolution.enabled);
    CHECK(!plain.spritesOnlyScenePipelines);

    GameManifest asking;
    asking.isGame = true;
    asking.spritesOnlyScenePipelines = true;
    asking.dynamicResolution.enabled = true;
    asking.dynamicResolution.startScale = 0.5f;
    GameManifest quiet;
    quiet.isGame = true;
    CHECK_MSG(GameRuntime::Serialize(asking) == GameRuntime::Serialize(quiet), "the manifest's text must carry neither");
    const GameManifest reparsed = GameRuntime::Parse(GameRuntime::Serialize(asking));
    CHECK_MSG(!reparsed.spritesOnlyScenePipelines && !reparsed.dynamicResolution.enabled, "nor read either back");
}

// The scene target's size follows the GPU's speed: a stand-in GPU whose frame costs `baseSeconds` at full size and
// follows the pixel count, which follows the square of the scale.
static int driveAStandInGpu(DynamicResolution& controller, double baseSeconds, int frames) {
    int changes = 0;
    for (int i = 0; i < frames; ++i) {
        const float scale = controller.Scale();
        if (controller.Observe(static_cast<float>(baseSeconds * scale * scale))) ++changes;
    }
    return changes;
}

static void testDynamicResolutionFindsTheLargestSizeAGpuKeepsUpWith() {
    DynamicResolutionConfig config;
    config.enabled = true;

    // A GPU that draws full size in 117 ms (the Xiaomi Pad 5's menu before E41): the scale falls until the
    // frame meets the target, in a few steps, and stays there.
    DynamicResolution slow(config);
    const int steps = driveAStandInGpu(slow, 0.117, 2000);
    CHECK_MSG(steps >= 1 && steps <= 6, "a few steps, not a hunt");
    CHECK(slow.Scale() >= config.minScale);
    CHECK_MSG(0.117 * slow.Scale() * slow.Scale() <= 1.0 / config.targetFps + 1e-6, "the frame meets the target");
    const float settled = slow.Scale();
    CHECK_MSG(driveAStandInGpu(slow, 0.117, 600) == 0, "and then it holds");
    CHECK_NEAR(slow.Scale(), settled);

    // A GPU that is fast at full size is never touched, however long it runs.
    DynamicResolution fast(config);
    CHECK_EQ(driveAStandInGpu(fast, 0.004, 3000), 0);
    CHECK_NEAR(fast.Scale(), 1.0f);

    // One that cannot meet the target even at the floor stops at the floor and says nothing more. (A frame longer
    // than DynamicResolution::kIgnoreAboveSeconds is a pause, not a speed: this one draws full size in 0.9 s.)
    DynamicResolution hopeless(config);
    driveAStandInGpu(hopeless, 0.9, 400);
    CHECK_NEAR(hopeless.Scale(), config.minScale);
    CHECK_EQ(driveAStandInGpu(hopeless, 0.9, 400), 0);

    // Started low on a GPU with room, the scale rises, slowly, to the largest size that still keeps the target
    // with 20% to spare - here a GPU that draws full size in 30 ms, so a little under 0.7 - and no further.
    DynamicResolutionConfig low = config;
    low.startScale = 0.4f;
    DynamicResolution rising(low);
    driveAStandInGpu(rising, 0.03, 150000);
    CHECK_MSG(rising.Scale() > 0.55f && rising.Scale() < 0.76f, "it rose, and stopped where the GPU still has room");
    CHECK(0.03 * rising.Scale() * rising.Scale() <= 1.0 / config.targetFps + 1e-6);
    CHECK_EQ(driveAStandInGpu(rising, 0.03, 20000), 0);
}

static void testDynamicResolutionIgnoresPausesLoadsAndOneSlowFrame() {
    DynamicResolutionConfig config;
    config.enabled = true;
    DynamicResolution controller(config);

    // 17 ms frames with a 300 ms hitch among them: the median decides, and it is fine.
    for (int i = 0; i < 400; ++i) {
        const float frame = (i % 25 == 0) ? 0.3f : 0.017f;
        CHECK(!controller.Observe(frame));
    }
    CHECK_NEAR(controller.Scale(), 1.0f);

    // A pause (two seconds) and a nonsense value count for nothing, and the frames after a pause are given a moment.
    CHECK(!controller.Observe(2.0f));
    CHECK(!controller.Observe(0.0f));
    CHECK(!controller.Observe(-1.0f));
    for (int i = 0; i < 200; ++i) CHECK(!controller.Observe(0.017f));
    CHECK_NEAR(controller.Scale(), 1.0f);

    // Disabled, the controller does nothing at all.
    DynamicResolutionConfig off;
    DynamicResolution idle(off);
    for (int i = 0; i < 300; ++i) CHECK(!idle.Observe(0.2f));
    CHECK_NEAR(idle.Scale(), 1.0f);
}

static void testDynamicResolutionRulesAndMendedConfigs() {
    // The rules in numbers. Half the target speed: cost follows the pixel count, so the scale falls by the square
    // root of the ratio, less 3%.
    CHECK_NEAR(DynamicResolution::ScaleAfterShortfall(1.0f, 29.0f, 58.0f, 0.3f), 0.69f);
    CHECK_NEAR(DynamicResolution::ScaleAfterShortfall(1.0f, 58.0f, 58.0f, 0.3f), 1.0f);   // at the target: unchanged
    CHECK_NEAR(DynamicResolution::ScaleAfterShortfall(1.0f, 0.5f, 58.0f, 0.3f), 0.5f);    // never below half in one step
    CHECK_NEAR(DynamicResolution::ScaleAfterShortfall(0.4f, 5.0f, 58.0f, 0.3f), 0.3f);    // nor below the floor
    // And the way up: a quarter at most, never past the ceiling.
    CHECK_NEAR(DynamicResolution::ScaleAfterHeadroom(0.5f, 240.0f, 58.0f, 1.0f), 0.63f);
    CHECK_NEAR(DynamicResolution::ScaleAfterHeadroom(0.9f, 240.0f, 58.0f, 1.0f), 1.0f);
    CHECK_NEAR(DynamicResolution::ScaleAfterHeadroom(0.5f, 60.0f, 58.0f, 1.0f), 0.5f);    // no room: unchanged

    // A config that contradicts itself is mended: a floor above the ceiling, a start outside both.
    DynamicResolutionConfig odd;
    odd.enabled = true;
    odd.minScale = 0.8f;
    odd.maxScale = 0.4f;
    odd.startScale = 5.0f;
    DynamicResolution mended(odd);
    CHECK(mended.Scale() >= 0.8f && mended.Scale() <= 1.0f);
}

// A stand-in whose frame is a part that ignores the picture's size (the CPU, the driver, a display's pacing) plus a part
// that follows the pixel count. Runs for `seconds` of simulated time; reports the changes, the reverts among them, and the
// share of the time the scale spent below where it started.
struct MixedRun {
    int changes{0};
    int reverts{0};
    double secondsBelowStart{0.0};
    double seconds{0.0};
};

static MixedRun driveAMixedGpu(DynamicResolution& controller, double fixedSeconds, double pixelSeconds, double seconds) {
    MixedRun run;
    const float start = controller.Scale();
    while (run.seconds < seconds) {
        const float scale = controller.Scale();
        const double frame = fixedSeconds + pixelSeconds * scale * scale;
        if (scale < start - 0.005f) run.secondsBelowStart += frame;
        run.seconds += frame;
        if (controller.Observe(static_cast<float>(frame))) {
            ++run.changes;
            if (controller.LastChangeWasARevert()) ++run.reverts;
        }
    }
    return run;
}

static void testAStepThatDoesNotPayForItselfIsTakenBackAndNotRepeatedForAWhile() {
    DynamicResolutionConfig config;
    config.enabled = true;
    config.startScale = 0.8f;

    // A frame the CPU decides (25 ms whatever the picture's size): a smaller picture buys nothing, so the controller tries
    // once, sees that nothing arrived, takes the step back, and then leaves the picture alone for half a minute, then a
    // minute, then two: over five simulated minutes it spends almost no time below its start.
    DynamicResolution cpuBound(config);
    const MixedRun cpu = driveAMixedGpu(cpuBound, 0.025, 0.0, 300.0);
    CHECK_MSG(cpu.reverts >= 2, "each try is taken back");
    CHECK_MSG(cpu.changes <= 12, "and the tries get rarer");
    CHECK_MSG(cpu.secondsBelowStart < 0.1 * cpu.seconds, "so the picture is almost never smaller for nothing");

    // A frame that is part fixed cost and part pixels - the tablet's menu fits 10 ms + 81 ms x scale squared - gains less
    // than the pixel count predicts but plenty: no step is taken back, and the target is met.
    config.startScale = 0.73f;
    DynamicResolution tablet(config);
    const MixedRun mixed = driveAMixedGpu(tablet, 0.010, 0.081, 120.0);
    CHECK_EQ(mixed.reverts, 0);
    CHECK_MSG(mixed.changes >= 2 && mixed.changes <= 8, "a few steps");
    CHECK_MSG(0.010 + 0.081 * tablet.Scale() * tablet.Scale() <= 1.0 / (config.targetFps * DynamicResolution::kShortfallTolerance) + 1e-6,
              "it settled on a size that meets the target");

    // A much heavier scene ends the hold early: after the revert the frames get three times slower, and the controller tries
    // again within seconds instead of waiting out the half minute.
    DynamicResolutionConfig again = config;
    again.startScale = 0.8f;
    DynamicResolution held(again);
    int guard = 0;
    while (guard++ < 4000) {
        if (held.Observe(0.025f) && held.LastChangeWasARevert()) break;
    }
    CHECK_MSG(held.LastChangeWasARevert(), "the first try was taken back");
    const float before = held.Scale();
    int changesAfterHeavier = 0;
    for (int i = 0; i < 400 && changesAfterHeavier == 0; ++i) {   // 400 frames of 80 ms: 32 s, about one hold
        if (held.Observe(0.08f)) ++changesAfterHeavier;
    }
    CHECK_MSG(changesAfterHeavier == 1 && held.Scale() < before, "a scene three times slower is tried at once");
}

static void testAStepThatBoughtARealShareIsKeptAndNeverOscillates() {
    DynamicResolutionConfig config;
    config.enabled = true;

    // A frame that is half fixed cost and half pixels (100 ms + 100 ms x scale squared): halving the picture buys +60%,
    // far under the 300% the pixel count predicts and the 75% a quarter of that asks for. It used to be taken back and the
    // hold cancelled in the very next window (the speed it was released against was the smaller size's), so the picture
    // hunted between two sizes with a resize every second or two. Now the gain is kept and the run settles.
    DynamicResolution hunting(config);
    const MixedRun half = driveAMixedGpu(hunting, 0.100, 0.100, 120.0);
    // (It still probes the floor, which buys 14.6% here, just under the 15% bar: after 30 s, then after 60 s. That is 7
    // changes in two minutes; the old rules made 62.)
    CHECK_MSG(half.changes <= 8, "it settles instead of hunting");
    CHECK_MSG(half.reverts <= 3, "and takes back only the rare probe");
    CHECK_MSG(hunting.Scale() <= 0.5f, "on a size that bought the speed");

    // A frame with a fixed share of 40 ms and 100 ms of pixels: 0.5 gives 15 fps, 0.3 gives 20. Both steps buy over 15%,
    // so both are kept and it ends at the floor, where it used to be held at 0.5 for no reason.
    DynamicResolution partial(config);
    const MixedRun mixed = driveAMixedGpu(partial, 0.040, 0.100, 120.0);
    CHECK_EQ(mixed.reverts, 0);
    CHECK_NEAR(partial.Scale(), config.minScale);

    // The window's own speed is there for a log line, whether or not it changed anything, and says when the target is
    // still missed at the floor.
    CHECK_MSG(partial.AtTheFloor() && partial.WindowMissedTheTarget(), "at the floor and still short of the target");
    CHECK_MSG(partial.WindowMedianMilliseconds() > 40.0f && partial.WindowMedianMilliseconds() < 60.0f, "with its median");

    // A CPU-bound frame still gets nothing from a smaller picture and is still taken back (see the test before this one):
    // the lower bar is a share of a gain that arrived, not a licence to keep a step that bought nothing.
    DynamicResolutionConfig cpu = config;
    cpu.startScale = 0.8f;
    DynamicResolution cpuBound(cpu);
    const MixedRun flat = driveAMixedGpu(cpuBound, 0.025, 0.0, 120.0);
    CHECK_MSG(flat.reverts >= 1 && cpuBound.Scale() >= 0.79f, "a step that buys nothing is still taken back");
}

static void testDynamicResolutionTakesANewConfigWhileRunning() {
    DynamicResolutionConfig config;
    config.enabled = true;
    config.startScale = 0.6f;
    DynamicResolution controller(config);
    driveAStandInGpu(controller, 0.117, 600);   // it has stepped down and learned something
    const float before = controller.Scale();
    CHECK(before < 0.6f);

    // A tighter floor moves the scale into the new range, and the new rules apply from the next window.
    DynamicResolutionConfig tighter = config;
    tighter.minScale = 0.7f;
    tighter.maxScale = 0.9f;
    controller.Reconfigure(tighter);
    CHECK_NEAR(controller.Scale(), 0.7f);

    // A contradictory config is mended exactly as the constructor mends it.
    DynamicResolutionConfig odd = config;
    odd.minScale = 0.8f;
    odd.maxScale = 0.4f;
    controller.Reconfigure(odd);
    CHECK(controller.Scale() >= 0.8f && controller.Scale() <= 1.0f);

    // Turned off, it does nothing more.
    DynamicResolutionConfig off;
    controller.Reconfigure(off);
    int changes = 0;
    for (int i = 0; i < 300; ++i) changes += controller.Observe(0.2f) ? 1 : 0;
    CHECK_EQ(changes, 0);
}

static void testTheVulkanMinimumIsOneTwoByDefaultAndClampedIntoOneToTwo() {
    // Off the shelf the engine takes a Vulkan 1.2 GPU and nothing less: a game
    // that never heard of the setting behaves as it always did. A game that says
    // 1 gets 1.1 GPUs too, and a number outside 1 to 2 is clamped rather than
    // trusted (1.0 lacks the core functions the allocator binds; nothing asks
    // for 1.3). Never in the manifest's text.
    const GameManifest plain;
    CHECK_EQ(plain.minimumVulkanMinor, 2u);
    CHECK_EQ(GameRuntime::ResolveVulkanMinor(plain), 2u);
    GameManifest wants11;
    wants11.minimumVulkanMinor = 1;
    CHECK_EQ(GameRuntime::ResolveVulkanMinor(wants11), 1u);
    wants11.minimumVulkanMinor = 0;
    CHECK_EQ(GameRuntime::ResolveVulkanMinor(wants11), 1u);
    wants11.minimumVulkanMinor = 3;
    CHECK_EQ(GameRuntime::ResolveVulkanMinor(wants11), 2u);
    wants11.minimumVulkanMinor = 4000000000u;
    CHECK_EQ(GameRuntime::ResolveVulkanMinor(wants11), 2u);

    // The allocator is told the device's own minor, capped at 2: a 1.2 or newer device gets what it always
    // got, a 1.1 device its own.
    CHECK_EQ(GameRuntime::AllocatorVulkanMinor(1), 1u);
    CHECK_EQ(GameRuntime::AllocatorVulkanMinor(2), 2u);
    CHECK_EQ(GameRuntime::AllocatorVulkanMinor(3), 2u);
    CHECK_EQ(GameRuntime::AllocatorVulkanMinor(4), 2u);

    GameManifest one;
    one.isGame = true;
    one.minimumVulkanMinor = 1;
    GameManifest two = one;
    two.minimumVulkanMinor = 2;
    CHECK_MSG(GameRuntime::Serialize(one) == GameRuntime::Serialize(two), "the manifest's text must not carry it");
    CHECK_MSG(GameRuntime::Parse(GameRuntime::Serialize(one)).minimumVulkanMinor == 2u, "nor read it back");
}

static std::string ReadWhole(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The lines of `text` that mention `needle`, without their line ends.
static std::vector<std::string> LinesWith(const std::string& text, const std::string& needle) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find(needle) != std::string::npos) lines.push_back(line);
    }
    return lines;
}

static void testTheFileLogCarriesElapsedTimeOnlyWhenAsked() {
    // The form: seconds with milliseconds, a plus sign, an s. A clock that ran
    // backwards is not a negative time in a log.
    CHECK(Log::FormatElapsed(0.0) == "+0.000s");
    CHECK(Log::FormatElapsed(1.2345) == "+1.234s" || Log::FormatElapsed(1.2345) == "+1.235s");
    CHECK(Log::FormatElapsed(12.0) == "+12.000s");
    CHECK(Log::FormatElapsed(3725.5) == "+3725.500s");
    CHECK(Log::FormatElapsed(-4.0) == "+0.000s");

    namespace fs = std::filesystem;
    std::error_code ec;
    // A name of its own: two runs of this suite at once must not share a sink.
    std::random_device entropy;
    const fs::path file = fs::temp_directory_path(ec) /
                          ("supersonic_test_log_elapsed_" + std::to_string(entropy()) + ".txt");
    CHECK(Log::SetFileSink(file.string()));

    // Off, the default: the line is what it always was, byte for byte.
    Log::SetElapsedTimestamps(false);
    Log::Submit(Log::Level::Info, "Probe", "plain");
    // On: the time sits between the level and the category, so the prefix a
    // reader greps for ("INFO [Probe]") still finds a line by its category.
    Log::SetElapsedTimestamps(true);
    Log::Submit(Log::Level::Warning, "Probe", "stamped one");
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    Log::Submit(Log::Level::Warning, "Probe", "stamped two");
    Log::SetElapsedTimestamps(false);
    Log::Submit(Log::Level::Error, "Probe", "plain again");
    Log::CloseFileSink();

    const std::string text = ReadWhole(file);
    fs::remove(file, ec);
    const std::vector<std::string> lines = LinesWith(text, "[Probe]");
    CHECK_MSG(lines.size() == 4, "four lines were logged, got " + std::to_string(lines.size()));
    if (lines.size() == 4) {
        CHECK_MSG(lines[0] == "INFO [Probe] plain", "off is the old form: " + lines[0]);
        CHECK_MSG(lines[3] == "ERROR [Probe] plain again", "off again is the old form: " + lines[3]);

        // On adds the elapsed time after the level; and it is the clock's: seconds,
        // never negative, and later in the second line by about the time slept.
        const std::regex stamped(R"(WARN \+([0-9]+\.[0-9]{3})s \[Probe\] (stamped (?:one|two)))");
        std::smatch first;
        std::smatch second;
        CHECK_MSG(std::regex_match(lines[1], first, stamped), "on adds the elapsed time: " + lines[1]);
        CHECK_MSG(std::regex_match(lines[2], second, stamped), "and on every line: " + lines[2]);
        if (first.size() == 3 && second.size() == 3) {
            const double t1 = std::stod(first[1]);
            const double t2 = std::stod(second[1]);
            CHECK_MSG(t1 >= 0.0, "a time since the first line is never negative");
            CHECK_MSG(t2 - t1 >= 0.04, "80 ms apart is at least 40 ms apart in the log (lower bound only)");
            CHECK_MSG(t2 < 3600.0, "and in seconds, not milliseconds or an epoch");
        }
    }

    // The console and the in-memory buffer are not stamped: the editor's panel
    // has a column of its own, and the console is read live.
    const std::vector<Log::Entry> entries = Log::Snapshot();
    bool sawStamped = false;
    for (const Log::Entry& entry : entries) {
        if (entry.category == "Probe" && entry.message == "stamped one") sawStamped = true;
        if (entry.category == "Probe") {
            CHECK_MSG(entry.message.empty() || entry.message[0] != '+', "a buffered message is stamped: " + entry.message);
        }
    }
    CHECK_MSG(sawStamped, "the buffer holds the message as it was written, unstamped");
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
    testAHiddenRunStartsWindowedAndUnfitted();

    testARequestWaitsToBeTaken();
    testTheLastRequestBeforeTheFrameWins();
    testAnUnusableWindowedSizeIsRefusedAndLatchesNothing();
    testAFullscreenModeIsAFullscreenRequestThatCarriesItsSize();
    testTheLastFullscreenRequestWinsWithOrWithoutAMode();
    testAModeTheMonitorDoesNotOfferIsRefusedAndLatchesNothing();
    testAFullscreenModeRunsAtTheDesktopsRateWhenItCan();
    testTheHighestRateIsChosenFromTheListAndTheDesktopAlike();
    testANamedRateIsThatRateOrNothing();
    testAFullscreenRequestCarriesItsRate();
    testTheRatesOfASizeAreListedOnceLowestFirst();
    testAFittedWindowIsTheMonitorsShapeInsideItsWorkArea();
    testACentredWindowKeepsItsTitleBarOnScreen();
    testAFitAndASizeAreOneRequestTheLaterWins();
    testAPreferredRateIsItsOwnRequest();
    testAManifestFitsItsWindowOnlyWhenItAsks();
    testTheStartupPumpIsOffByDefaultAndNeverInTheManifestsText();
    testTheScenePerformanceOptInsAreOffByDefaultAndNeverInTheManifestsText();
    testDynamicResolutionFindsTheLargestSizeAGpuKeepsUpWith();
    testDynamicResolutionIgnoresPausesLoadsAndOneSlowFrame();
    testDynamicResolutionRulesAndMendedConfigs();
    testAStepThatDoesNotPayForItselfIsTakenBackAndNotRepeatedForAWhile();
    testAStepThatBoughtARealShareIsKeptAndNeverOscillates();
    testDynamicResolutionTakesANewConfigWhileRunning();
    testTheVulkanMinimumIsOneTwoByDefaultAndClampedIntoOneToTwo();
    testTheFileLogCarriesElapsedTimeOnlyWhenAsked();
    testHidingTheCursorIsTheSameRequestInputArbitrates();
    testTheModesAMenuListsAreTheTrueColourOnesOnceEach();
    testAWindowGoesFullscreenOnTheMonitorHoldingMostOfIt();

    testAPackagedFolderOutranksEverything();
    testARunFromTheEnginesRootIsLeftAsItWas();
    testAGameInItsOwnRepositoryFindsTheEngine();
    testARootWithoutTheFilesIsNoRoot();
    testTheEnginesOwnBuildNamesNoRoot();
}

TEST_MAIN("test_gameruntime", 62)
