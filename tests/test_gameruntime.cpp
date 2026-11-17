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
#include "core/FramePerf.hpp"
#include "core/PerfOverlay.hpp"
#include "core/Profiler.hpp"
#include "core/RenderScaleControl.hpp"
#include "core/GameRuntime.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"
#include "core/WindowControl.hpp"
#include "platform/ExecutablePath.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
    asking.dynamicResolution.relaxedRise = true;        // E47
    asking.dynamicResolution.riseMargin = 1.4f;
    asking.dynamicResolution.minTargetHeight = 360;
    asking.dynamicResolution.emergencyBelowFps = 30.0f;
    asking.dynamicResolution.emergencyScale = 0.3f;
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


// ---- E47: the way up that does not need 110 fps, the floor in pixels, and the player's say over the size ----------------

// Simulated seconds at a frame of `fixedSeconds` + `pixelSeconds` x scale squared; counts the rises and the falls.
struct RiseRun {
    int rises{0};
    int falls{0};
    double seconds{0.0};
};

static RiseRun driveCountingRises(DynamicResolution& controller, double fixedSeconds, double pixelSeconds, double seconds) {
    RiseRun run;
    double elapsed = 0.0;
    while (elapsed < seconds) {
        const float scale = controller.Scale();
        const double frame = fixedSeconds + pixelSeconds * scale * scale;
        elapsed += frame;
        if (controller.Observe(static_cast<float>(frame))) {
            if (controller.LastChangeWasARise()) ++run.rises;
            else ++run.falls;
        }
    }
    run.seconds = elapsed;
    return run;
}

static void testTheOptInsOfE47AreOffByDefault() {
    const DynamicResolutionConfig defaults;
    CHECK_MSG(!defaults.relaxedRise && defaults.minTargetHeight == 0, "the controller is E41's unless a game asks");
    CHECK_MSG(defaults.emergencyBelowFps == 0.0f && defaults.emergencyScale == 0.0f, "and its floor is a hard one");
    DynamicResolution controller(defaults);
    CHECK_MSG(!controller.LoopPaced(), "and a loop is unpaced until the engine says otherwise");
}

static void testARiseNeedsNo110FpsWhenTheGameAsksAndTheLoopIsNotPaced() {
    DynamicResolutionConfig config;
    config.enabled = true;
    config.startScale = 0.3f;

    // A frame of 9.5 ms that ignores the picture's size plus 20 ms x scale squared: 88 fps at 0.3. E41 needs 110 fps for 8 s to
    // raise the scale, and this frame can never show it (the fixed part alone is 105 fps), so a size once lost is lost.
    DynamicResolution legacy(config);
    const RiseRun still = driveCountingRises(legacy, 0.0095, 0.020, 300.0);
    CHECK_EQ(still.rises + still.falls, 0);
    CHECK_NEAR(legacy.Scale(), 0.3f);

    // The same frame, relaxed: the scale climbs to where the frame is at the target again, in a few steps, and holds.
    DynamicResolutionConfig relaxed = config;
    relaxed.relaxedRise = true;
    DynamicResolution rising(relaxed);
    const RiseRun climb = driveCountingRises(rising, 0.0095, 0.020, 300.0);
    CHECK_MSG(climb.rises >= 3 && climb.rises <= 10 && climb.falls == 0, "a few rises, no falls");
    CHECK_MSG(rising.Scale() > 0.42f && rising.Scale() < 0.56f, "to where the frame still has room");
    const double frame = 0.0095 + 0.020 * rising.Scale() * rising.Scale();
    CHECK_MSG(frame <= 1.0 / (config.targetFps * DynamicResolution::kShortfallTolerance), "without a shortfall");
    const RiseRun held = driveCountingRises(rising, 0.0095, 0.020, 180.0);
    CHECK_EQ(held.rises + held.falls, 0);

    // A GPU already at the target is left alone.
    DynamicResolutionConfig full = relaxed;
    full.startScale = 1.0f;
    DynamicResolution steady(full);
    const RiseRun leftAlone = driveCountingRises(steady, 0.0, 0.0175, 240.0);
    CHECK_EQ(leftAlone.rises + leftAlone.falls, 0);

    // A loop the display paces cannot show room: a window never reads above the refresh rate, so the rule stands down and
    // the controller is E41's.
    DynamicResolution paced(relaxed);
    paced.SetLoopPaced(true);
    const RiseRun pacedRun = driveCountingRises(paced, 0.0095, 0.020, 300.0);
    CHECK_EQ(pacedRun.rises + pacedRun.falls, 0);
    CHECK_NEAR(paced.Scale(), 0.3f);

    // 110 fps still raises it, with or without the option (the old way is not removed).
    DynamicResolution fast(config);
    CHECK_MSG(driveCountingRises(fast, 0.0, 0.003, 120.0).rises >= 1, "a GPU with a lot of room rises as before");
}

static void testARiseThatDoesNotHoldWaitsBeforeTheNextOne() {
    DynamicResolutionConfig config;
    config.enabled = true;
    config.startScale = 0.3f;
    config.relaxedRise = true;
    DynamicResolution controller(config);

    // Climb until the first rise.
    int guard = 0;
    while (guard++ < 20000) {
        const float scale = controller.Scale();
        const double frame = 0.0095 + 0.020 * scale * scale;
        if (controller.Observe(static_cast<float>(frame)) && controller.LastChangeWasARise()) break;
    }
    CHECK_MSG(controller.LastChangeWasARise(), "the first rise came");
    const float afterRise = controller.Scale();

    // The scene gets eight times heavier in pixels a few seconds later (a shortfall at once, and one a smaller picture cures):
    // the rise did not hold.
    int falls = 0;
    for (double t = 0.0; t < 8.0;) {
        const float scale = controller.Scale();
        const double frame = 0.0095 + 0.160 * scale * scale;
        t += frame;
        if (controller.Observe(static_cast<float>(frame)) && !controller.LastChangeWasARise()) ++falls;
    }
    CHECK_MSG(falls >= 1 && controller.Scale() < afterRise, "the heavier frames brought the scale down");

    // The scene is light again. For the hold (60 s) no rise is taken, however calm the frames are...
    const RiseRun during = driveCountingRises(controller, 0.0095, 0.020, 40.0);
    CHECK_MSG(during.rises == 0, "no new rise while the hold runs");
    // ...and once it has passed, it climbs again.
    const RiseRun after = driveCountingRises(controller, 0.0095, 0.020, 200.0);
    CHECK_MSG(after.rises >= 1, "the way up is open again");
}

static void testThePixelFloorIsTheScaleThatKeepsTheTargetTallEnough() {
    CHECK_NEAR(DynamicResolution::FloorForTargetHeight(0.3f, 1.0f, 720, 360), 0.5f);     // a phone's 1600x720: 800x360
    CHECK_NEAR(DynamicResolution::FloorForTargetHeight(0.3f, 1.0f, 1600, 360), 0.3f);    // a tablet's 1600 lines: the old floor binds
    CHECK_NEAR(DynamicResolution::FloorForTargetHeight(0.3f, 1.0f, 1080, 360), 0.34f);   // rounded UP: 0.33 would be 356 lines
    CHECK_NEAR(DynamicResolution::FloorForTargetHeight(0.3f, 1.0f, 0, 360), 0.3f);       // no window yet: the floor as given
    CHECK_NEAR(DynamicResolution::FloorForTargetHeight(0.3f, 1.0f, 720, 0), 0.3f);       // no pixel floor asked
    CHECK_NEAR(DynamicResolution::FloorForTargetHeight(0.3f, 0.4f, 480, 360), 0.4f);     // never above the ceiling
    CHECK_NEAR(DynamicResolution::FloorForTargetHeight(0.6f, 1.0f, 1600, 360), 0.6f);    // never below the floor as given

    // The target a scale makes (the window's height times the scale, truncated and made even: EditorLayer::ScaledExtent) is at
    // least that many lines high, for every window height and every floor in pixels the picture can have, whatever the float
    // arithmetic does to the product (a 150-line window at 0.84 is 125.99999 lines, not 126).
    for (std::uint32_t height = 100; height <= 3000; height += 7) {
        for (const std::uint32_t lines : {126u, 240u, 360u, 480u, 720u}) {
            if (lines > height) continue;
            const float scale = DynamicResolution::FloorForTargetHeight(0.3f, 1.0f, height, lines);
            const std::uint32_t target = scale >= 1.0f ? height : (static_cast<std::uint32_t>(static_cast<float>(height) * scale) & ~1u);
            CHECK_MSG(target >= lines, "a " + std::to_string(height) + "-line window with a floor of " + std::to_string(lines) +
                                           " lines made a target of " + std::to_string(target));
        }
    }
}

static void testTheFloorGivesWayOnlyWhenTheGameWouldBeUnplayablySlow() {
    DynamicResolutionConfig config;
    config.enabled = true;
    config.minScale = 0.5f;              // the floor of a picture worth looking at
    config.emergencyScale = 0.3f;
    config.emergencyBelowFps = 30.0f;

    // 90 ms at full size and all of it following the pixel count: 22.5 ms at the floor, 44 fps. A shortfall, but a playable
    // one: the picture stops at the floor and keeps its legibility.
    DynamicResolution playable(config);
    driveAStandInGpu(playable, 0.09, 4000);
    CHECK_NEAR(playable.Scale(), 0.5f);

    // 200 ms at full size: 50 ms at the floor, 20 fps. Not a speed anyone plays at: the scale goes on down to the emergency
    // floor, where the same GPU draws in 18 ms.
    DynamicResolution crawling(config);
    driveAStandInGpu(crawling, 0.2, 4000);
    CHECK_NEAR(crawling.Scale(), 0.3f);
    CHECK_MSG(crawling.AtTheFloor(), "and the floor it is at is the emergency one");

    // Without the speed that triggers it, the floor is a hard one (the emergency scale is ignored, not obeyed).
    DynamicResolutionConfig hard = config;
    hard.emergencyBelowFps = 0.0f;
    DynamicResolution hardFloor(hard);
    driveAStandInGpu(hardFloor, 0.2, 4000);
    CHECK_NEAR(hardFloor.Scale(), 0.5f);

    // An emergency scale that is not under the floor is no emergency scale at all.
    DynamicResolutionConfig odd = config;
    odd.emergencyScale = 0.8f;
    DynamicResolution mended(odd);
    driveAStandInGpu(mended, 0.2, 4000);
    CHECK_NEAR(mended.Scale(), 0.5f);

    // From the emergency floor the way up is the relaxed rise: when the GPU gets fast the picture climbs back over the
    // comfortable floor and stays there, without hovering between the two.
    DynamicResolutionConfig climbing = config;
    climbing.relaxedRise = true;
    DynamicResolution recovering(climbing);
    driveAStandInGpu(recovering, 0.2, 4000);
    CHECK_NEAR(recovering.Scale(), 0.3f);
    const RiseRun back = driveCountingRises(recovering, 0.0, 0.02, 300.0);
    CHECK_MSG(recovering.Scale() > 0.5f, "back over the comfortable floor");
    CHECK_MSG(back.rises >= 2 && back.rises <= 8 && back.falls == 0, "in a few rises, with no fall between");
    const RiseRun settled = driveCountingRises(recovering, 0.0, 0.02, 120.0);
    CHECK_EQ(settled.rises + settled.falls, 0);
}

// Simulated seconds at a frame of `fixedSeconds` + `pixelSeconds` x scale squared, ignoring what changes.
static void driveFor(DynamicResolution& controller, double fixedSeconds, double pixelSeconds, double seconds) {
    for (double t = 0.0; t < seconds;) {
        const float scale = controller.Scale();
        const double frame = fixedSeconds + pixelSeconds * scale * scale;
        t += frame;
        controller.Observe(static_cast<float>(frame));
    }
}

// The simulated seconds until the next rise on room, or -1 if none comes within `limit`.
static double secondsUntilARise(DynamicResolution& controller, double fixedSeconds, double pixelSeconds, double limit) {
    for (double t = 0.0; t < limit;) {
        const float scale = controller.Scale();
        const double frame = fixedSeconds + pixelSeconds * scale * scale;
        t += frame;
        if (controller.Observe(static_cast<float>(frame)) && controller.LastChangeWasARise()) return t;
    }
    return -1.0;
}

static void testARiseHoldDoublesWithEachFailureAndIsCapped() {
    DynamicResolutionConfig config;
    config.enabled = true;
    config.startScale = 0.3f;
    config.relaxedRise = true;
    DynamicResolution controller(config);

    // A light scene: the picture climbs. Then a scene eight times heavier in pixels at once: the rise did not hold, and the
    // next one waits 60 s, then 120, 240, 480 and (the cap) 480 again, each measured from the heavy scene's end.
    CHECK_MSG(secondsUntilARise(controller, 0.0095, 0.020, 100.0) > 0.0, "the first rise");
    const double holds[] = {60.0, 120.0, 240.0, 480.0, 480.0};
    for (const double hold : holds) {
        driveFor(controller, 0.0095, 0.160, 6.0);
        const double wait = secondsUntilARise(controller, 0.0095, 0.020, 900.0);
        CHECK_MSG(wait >= hold - 10.0 && wait <= hold + 45.0, "the next rise came after " + std::to_string(wait) + " s, the hold was " + std::to_string(hold));
    }
}

static void testARiseThatHeldIsNotHeldAgainstTheNextFailureAndReconfigureForgets() {
    DynamicResolutionConfig config;
    config.enabled = true;
    config.startScale = 0.3f;
    config.relaxedRise = true;

    // After the trial (20 s without a shortfall) the rise is trusted: a heavy scene much later is an ordinary fall, with no hold.
    DynamicResolution trusted(config);
    driveFor(trusted, 0.0095, 0.020, 150.0);   // climbed and settled; the last rise is long past
    driveFor(trusted, 0.0095, 0.160, 6.0);
    const double quick = secondsUntilARise(trusted, 0.0095, 0.020, 300.0);
    CHECK_MSG(quick > 0.0 && quick < 40.0, "no hold after a trusted rise: " + std::to_string(quick));

    // A hold in force is forgotten by Reconfigure, as every other judgement is.
    DynamicResolution held(config);
    CHECK(secondsUntilARise(held, 0.0095, 0.020, 100.0) > 0.0);
    driveFor(held, 0.0095, 0.160, 6.0);   // the rise did not hold: 60 s
    held.Reconfigure(config);
    const double forgotten = secondsUntilARise(held, 0.0095, 0.020, 300.0);
    CHECK_MSG(forgotten > 0.0 && forgotten < 40.0, "Reconfigure forgot the hold: " + std::to_string(forgotten));
}

static void testTheRiseMarginSetsWhereTheWayUpOpens() {
    DynamicResolutionConfig config;
    config.enabled = true;
    config.startScale = 0.3f;
    config.relaxedRise = true;
    // 88 fps at 0.3 (9.5 ms + 20 ms x 0.09). The way up opens at 58 x 1.25 = 72.5 fps, so it climbs; with a margin of 1.8 it
    // opens at 104 fps and does not.
    DynamicResolution open(config);
    CHECK(secondsUntilARise(open, 0.0095, 0.020, 100.0) > 0.0);
    config.riseMargin = 1.8f;
    DynamicResolution shut(config);
    CHECK_MSG(secondsUntilARise(shut, 0.0095, 0.020, 200.0) < 0.0, "a margin of 1.8 asks for 104 fps");
}

static void testAFallIsMeasuredAtTheComfortableFloorFirst() {
    DynamicResolutionConfig config;
    config.enabled = true;
    config.minScale = 0.5f;
    config.emergencyScale = 0.3f;
    config.emergencyBelowFps = 30.0f;
    // 5 fps at full size: the first step down stops at the comfortable floor, however slow the frame was, and is judged there.
    DynamicResolution controller(config);
    for (int guard = 0; guard < 400 && !controller.Observe(0.2f); ++guard) {}
    CHECK_NEAR(controller.Scale(), 0.5f);
    CHECK_MSG(!controller.AtTheFloor(), "the comfortable floor is not the floor the picture cannot go below");
}

static void testTheEmergencyFloorIsReachedFromJustAboveTheFloor() {
    DynamicResolutionConfig config;
    config.enabled = true;
    config.startScale = 0.51f;           // one hundredth above the floor: a step to it is under the controller's 0.01 test
    config.minScale = 0.5f;
    config.emergencyScale = 0.3f;
    config.emergencyBelowFps = 30.0f;
    DynamicResolution controller(config);
    driveAStandInGpu(controller, 0.2, 4000);   // 52 ms at 0.51: 19 fps
    CHECK_NEAR(controller.Scale(), 0.3f);

    // And the same start on a playable GPU stays where it is, a hundredth over the floor.
    DynamicResolution playable(config);
    driveAStandInGpu(playable, 0.09, 4000);    // 23.4 ms at 0.51: 43 fps
    CHECK_MSG(playable.Scale() >= 0.5f && playable.Scale() <= 0.51f, "a shortfall above 30 fps does not give up the picture");
}

static void testAFloorThatRoseLiftsAPictureThatHadFallenBelowIt() {
    DynamicResolutionConfig config;
    config.enabled = true;
    config.minScale = 0.3f;
    config.relaxedRise = true;
    DynamicResolution controller(config);
    driveAStandInGpu(controller, 0.117, 2000);   // a slow GPU: the picture settled low
    CHECK(controller.Scale() < 0.5f);

    // The window got shorter, so the floor is now 0.5 with the old floor kept for an emergency: the picture is lifted into the
    // comfortable range and judged there, instead of being left under it.
    DynamicResolutionConfig raised = DynamicResolution::ForWindow(
        [&] { DynamicResolutionConfig declared = config; declared.minTargetHeight = 360; declared.emergencyBelowFps = 30.0f; return declared; }(), 720);
    CHECK_NEAR(raised.minScale, 0.5f);
    CHECK_NEAR(raised.emergencyScale, 0.3f);
    controller.Reconfigure(raised);
    CHECK_NEAR(controller.Scale(), 0.5f);
}

static void testAPinnedSizeHoldsWhateverTheGpuDoes() {
    DynamicResolutionConfig declared;
    declared.enabled = true;
    declared.relaxedRise = true;
    declared.minTargetHeight = 360;
    declared.emergencyBelowFps = 30.0f;
    const DynamicResolutionConfig forPhone = DynamicResolution::ForWindow(declared, 720);   // floor 0.5, emergency 0.3

    // The controller has fallen to the emergency floor on a slow GPU...
    DynamicResolution controller(forPhone);
    driveAStandInGpu(controller, 0.4, 4000);
    CHECK_NEAR(controller.Scale(), 0.3f);

    // ...and the player pins the picture at 0.74: it is that at once, and stays that however slow the frames are (no emergency
    // floor to fall to, no rise, no pixel floor).
    const DynamicResolutionConfig pinned = DynamicResolution::Pinned(forPhone, 0.74f);
    CHECK(!pinned.relaxedRise && pinned.emergencyBelowFps == 0.0f && pinned.emergencyScale == 0.0f && pinned.minTargetHeight == 0);
    controller.Reconfigure(pinned);
    CHECK_NEAR(controller.Scale(), 0.74f);
    CHECK_EQ(driveAStandInGpu(controller, 0.4, 3000), 0);
    CHECK_NEAR(controller.Scale(), 0.74f);
    // And a fast GPU does not raise it.
    CHECK_EQ(driveAStandInGpu(controller, 0.003, 6000), 0);
    CHECK_NEAR(controller.Scale(), 0.74f);

    // Back to automatic: the controller decides again from where it is.
    controller.Reconfigure(forPhone);
    CHECK(controller.Scale() >= 0.5f);
}

static void testTheDeclaredConfigIsWorkedOutForEachWindowHeight() {
    DynamicResolutionConfig declared;
    declared.minScale = 0.3f;
    declared.minTargetHeight = 360;
    declared.emergencyBelowFps = 30.0f;

    // A phone's 720 lines: 0.5, the old floor kept for an emergency. A tablet's 1600: 0.3 is already more than 360 lines, so the
    // pixel floor changes nothing and there is no emergency floor under it.
    const DynamicResolutionConfig phone = DynamicResolution::ForWindow(declared, 720);
    CHECK_NEAR(phone.minScale, 0.5f);
    CHECK_NEAR(phone.emergencyScale, 0.3f);
    const DynamicResolutionConfig tablet = DynamicResolution::ForWindow(declared, 1600);
    CHECK_NEAR(tablet.minScale, 0.3f);
    CHECK_NEAR(tablet.emergencyScale, 0.0f);
    CHECK_NEAR(DynamicResolution::ForWindow(declared, 0).minScale, 0.3f);

    // No emergency asked for: the raised floor is a hard one.
    declared.emergencyBelowFps = 0.0f;
    const DynamicResolutionConfig hard = DynamicResolution::ForWindow(declared, 720);
    CHECK_NEAR(hard.minScale, 0.5f);
    CHECK_NEAR(hard.emergencyScale, 0.0f);

    // No pixel floor asked for: the declaration as it was, whatever the window.
    declared.minTargetHeight = 0;
    declared.emergencyBelowFps = 30.0f;
    for (const std::uint32_t height : {0u, 240u, 720u, 1600u}) {
        const DynamicResolutionConfig same = DynamicResolution::ForWindow(declared, height);
        CHECK_NEAR(same.minScale, 0.3f);
        CHECK_NEAR(same.emergencyScale, 0.0f);
    }
}

static void testTheRenderScaleControlLatchesTheLastRequestAndDropsOneWithNothingToAsk() {
    RenderScaleControl control;
    RenderScaleRequest request;
    CHECK_MSG(!control.Available() && control.Mode() == RenderScaleMode::Automatic, "nothing to ask before the engine says so");

    // A request before there is a controller is dropped, not kept for later.
    control.RequestFixed(0.5f);
    control.Publish(false, 1.0f);
    CHECK(!control.TakeRequest(&request));

    control.Publish(true, 0.8f);
    CHECK(control.Available());
    CHECK_NEAR(control.CurrentScale(), 0.8f);
    CHECK_MSG(!control.TakeRequest(&request), "nothing asked, nothing taken");

    // The last request before the frame wins; taking it clears it; the mode reads back what was asked.
    control.RequestFixed(0.5f);
    control.RequestFixed(2.0f);          // above the window: held at the window's size
    CHECK(control.Mode() == RenderScaleMode::Fixed);
    CHECK(control.TakeRequest(&request));
    CHECK(request.mode == RenderScaleMode::Fixed);
    CHECK_NEAR(request.fraction, 1.0f);
    CHECK(!control.TakeRequest(&request));
    control.RequestFixed(-3.0f);         // nonsense: the smallest picture the controller allows, never zero
    CHECK(control.TakeRequest(&request));
    CHECK_MSG(request.fraction > 0.0f && request.fraction <= 0.11f, "a size that is not a size is the smallest, not zero");
    control.RequestAutomatic();
    CHECK(control.TakeRequest(&request));
    CHECK(request.mode == RenderScaleMode::Automatic);
    CHECK(control.Mode() == RenderScaleMode::Automatic);
}

// ---- E50: a lower tier under a quality floor, and a memory per scene (opt-in) ----------------------------------------------
//
// The stand-in devices below are hand estimates of real ones (no tablet was measured at these costs). Every bound that is an
// estimate is a named constant beside its test, and each such test prints one "E50 rest:" line (the case, its tier, the
// scale, the frame rate and the resizes it came to rest at), so that the bounds can be set from a run with a margin of at
// least 0.03. A tier assertion is never loosened.

constexpr float kE50SteadyFps = 28.2f;   // "a steady 30": a window median at 94% of it

// A sequence that is the same on every compiler (std::uniform_real_distribution is not): uniform in [-1, 1].
struct E50Lcg {
    std::uint32_t state;
    double Next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / 8388607.5 - 1.0;
    }
};

// A GPU and a CPU: the frame is the longer of the CPU's work and the GPU's (a fixed part plus a part that follows the pixel
// count), the CPU waits for the rest, both with +-5% jitter; a display-paced loop (FIFO) rounds the frame up to whole
// sixtieths. Milliseconds. `thermal` swings the GPU's cost by that share over `thermalPeriodSeconds`.
struct E50Device {
    double megapixels{1.0};
    double gpuFixedMs{2.0};
    double msPerMegapixel{100.0};
    double cpuMs{10.0};
    bool fifo{false};
    double thermal{0.0};
    double thermalPeriodSeconds{60.0};
};

struct E50Frame {
    float frameSeconds;
    float waitSeconds;
};

static E50Frame e50Frame(const E50Device& device, float scale, double t, E50Lcg& rng) {
    const double heat = 1.0 + device.thermal * std::sin(6.283185307179586 * t / device.thermalPeriodSeconds);
    const double pixels = static_cast<double>(scale) * static_cast<double>(scale);
    const double gpu = (device.gpuFixedMs + device.msPerMegapixel * device.megapixels * pixels) * heat * (1.0 + 0.05 * rng.Next());
    const double cpu = device.cpuMs * (1.0 + 0.05 * rng.Next());
    double frame = std::max(cpu, gpu);
    if (device.fifo) frame = std::ceil(frame * 0.06 - 1e-9) / 0.06;
    return {static_cast<float>(frame / 1000.0), static_cast<float>(std::max(0.0, frame - cpu) / 1000.0)};
}

// One stretch of a scene: 5 simulated minutes by default, the last 4 of them the rest.
struct E50Drive {
    double seconds{300.0};
    double restFrom{60.0};
    double slowSeconds{0.0};          // first, frames at 8-15 fps whatever the size (a launch's compiles, a load's first draws)
    float loadFrameSeconds{0.0f};     // first of all, one frame this long
    double hitchEverySeconds{0.0};    // a 150 ms frame this often (it takes no draw from the sequence)
    int hitchFrames{1};               // ... or a burst of this many in a row
    float hitchWaitSeconds{0.0f};     // each with this wait for the GPU
};

// (Built field by field: a brace list that names only some fields is a warning on some compilers.)
static E50Device e50Device(double megapixels, double gpuFixedMs, double msPerMegapixel, double cpuMs) {
    E50Device device;
    device.megapixels = megapixels;
    device.gpuFixedMs = gpuFixedMs;
    device.msPerMegapixel = msPerMegapixel;
    device.cpuMs = cpuMs;
    return device;
}

static E50Drive e50Stretch(double seconds, double restFrom) {
    E50Drive drive;
    drive.seconds = seconds;
    drive.restFrom = restFrom;
    return drive;
}

struct E50Run {
    double seconds{0.0};
    int resizes{0};
    int restResizes{0};
    int tierChanges{0};
    int downs{0};
    int probes{0};
    int probeFailures{0};
    double firstSteadyWindowSeconds{-1.0};
    double secondsBelowStart{0.0};    // below 0.99 of the scale the stretch started at
    float maxScale{0.0f};             // the largest scale the controller held at any frame, the start included
    std::vector<double> resizeTimes;
    std::vector<double> probeTimes;
    std::vector<double> failTimes;
    std::vector<double> holdsAtFailure;
    std::vector<float> restFrames;

    float RestFps() const {
        if (restFrames.empty()) return 0.0f;
        std::vector<float> sorted = restFrames;
        std::sort(sorted.begin(), sorted.end());
        return 1.0f / sorted[sorted.size() / 2];
    }
    int ResizesWithin(double limit) const {
        return static_cast<int>(std::count_if(resizeTimes.begin(), resizeTimes.end(), [limit](double t) { return t <= limit; }));
    }
};

static E50Run e50Drive(DynamicResolution& controller, const E50Device& device, const E50Drive& drive, E50Lcg& rng) {
    E50Run run;
    const float start = controller.Scale();
    run.maxScale = start;
    std::uint64_t windows = controller.WindowsEnded();
    double nextHitch = drive.hitchEverySeconds > 0.0 ? drive.hitchEverySeconds : 1e300;
    int hitchesLeft = 0;
    bool first = true;
    double t = 0.0;
    while (t < drive.seconds) {
        E50Frame frame{};
        if (t >= nextHitch) {
            hitchesLeft = drive.hitchFrames;
            nextHitch += drive.hitchEverySeconds;
        }
        if (first && drive.loadFrameSeconds > 0.0f) {
            frame = {drive.loadFrameSeconds, -1.0f};
        } else if (hitchesLeft > 0) {
            frame = {0.150f, drive.hitchWaitSeconds};
            --hitchesLeft;
        } else if (t < drive.slowSeconds) {
            frame = {static_cast<float>(1.0 / (11.5 + 3.5 * rng.Next())), 0.0f};
        } else {
            frame = e50Frame(device, controller.Scale(), t, rng);
        }
        first = false;
        if (controller.Scale() < 0.99f * start) run.secondsBelowStart += frame.frameSeconds;
        t += frame.frameSeconds;
        const bool resting = t >= drive.restFrom;
        if (resting) run.restFrames.push_back(frame.frameSeconds);
        if (controller.ObserveFrame(frame.frameSeconds, frame.waitSeconds)) {
            ++run.resizes;
            run.resizeTimes.push_back(t);
            if (resting) ++run.restResizes;
        }
        run.maxScale = std::max(run.maxScale, controller.Scale());
        switch (controller.TakeTierChange()) {
        case DynamicResolution::TierChange::Down:
            ++run.tierChanges;
            ++run.downs;
            break;
        case DynamicResolution::TierChange::Probe:
            ++run.tierChanges;
            ++run.probes;
            run.probeTimes.push_back(t);
            break;
        case DynamicResolution::TierChange::ProbeFailed:
            ++run.tierChanges;
            ++run.probeFailures;
            run.failTimes.push_back(t);
            run.holdsAtFailure.push_back(controller.TierHoldSeconds());
            break;
        case DynamicResolution::TierChange::None:
            break;
        }
        if (controller.WindowsEnded() != windows) {
            windows = controller.WindowsEnded();
            if (run.firstSteadyWindowSeconds < 0.0 && controller.WindowMedianMilliseconds() > 0.0f &&
                1000.0f / controller.WindowMedianMilliseconds() >= kE50SteadyFps) {
                run.firstSteadyWindowSeconds = t;
            }
        }
    }
    run.seconds = t;
    return run;
}

static void e50PrintRest(const std::string& name, const DynamicResolution& controller, const E50Run& run) {
    std::printf("E50 rest: %s: tier %.0f, scale %.2f, %.1f fps, %d resizes (%d at rest), %d tier changes (%d probes), "
                "first window at %.1f fps or more after %.1f s\n",
                name.c_str(), static_cast<double>(controller.TierFps()), static_cast<double>(controller.Scale()),
                static_cast<double>(run.RestFps()), run.resizes, run.restResizes, run.tierChanges, run.probes,
                static_cast<double>(kE50SteadyFps), run.firstSteadyWindowSeconds);
}

// The configuration a phone game declares (E47's refinements, as Penumbra sets them), with the tiers (30 fps below 540 lines)
// or without; and the same worked out for a window `windowHeight` lines high.
static DynamicResolutionConfig e50Declared(float startScale, bool tiers = true) {
    DynamicResolutionConfig declared;
    declared.enabled = true;
    declared.startScale = startScale;
    declared.relaxedRise = true;
    declared.minTargetHeight = 360;
    declared.emergencyBelowFps = 30.0f;
    if (tiers) {
        declared.lowTierFps = 30.0f;
        declared.qualityTargetHeight = 540;
    }
    return declared;
}

static DynamicResolutionConfig e50Config(float startScale, std::uint32_t windowHeight, bool tiers = true) {
    return DynamicResolution::ForWindow(e50Declared(startScale, tiers), windowHeight);
}

static bool e50InTier(const DynamicResolution& controller, float fps) {
    return std::fabs(controller.TierFps() - fps) < 0.5f;
}

static bool e50SameBits(float a, float b) {
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

static bool e50SameState(const DynamicResolution& a, const DynamicResolution& b) {
    return e50SameBits(a.Scale(), b.Scale()) && e50SameBits(a.LastMedianMilliseconds(), b.LastMedianMilliseconds()) &&
           e50SameBits(a.WindowMedianMilliseconds(), b.WindowMedianMilliseconds()) &&
           a.LastChangeWasARise() == b.LastChangeWasARise() && a.WindowMissedTheTarget() == b.WindowMissedTheTarget() &&
           a.AtTheFloor() == b.AtTheFloor() && a.LastChangeWasARevert() == b.LastChangeWasARevert() &&
           a.LoopPaced() == b.LoopPaced();
}

// 1. Off is E47: with no tiers in force (none asked, or one Mended drops) and no scene noted, ObserveFrame is Observe, return
// for return and getter for getter, through pauses, nonsense frames, unknown waits, paced loops, the emergency floor, a live
// Reconfigure and a pin. And none of E50's fields is ever in the manifest's text.
static void testE50WithNoTiersAndNoSceneIsE47FrameForFrame() {
    E50Lcg rng{20501u};
    int tracesThatDiffered = 0;
    int tierSignals = 0;
    for (int trace = 0; trace < 1000; ++trace) {
        DynamicResolutionConfig declared;
        declared.enabled = rng.Next() > -0.9;
        declared.startScale = static_cast<float>(0.65 + 0.35 * rng.Next());
        declared.relaxedRise = rng.Next() > 0.0;
        declared.minTargetHeight = rng.Next() > 0.0 ? 360u : 0u;
        declared.emergencyBelowFps = rng.Next() > 0.0 ? 30.0f : 0.0f;
        declared.lowTierFps = rng.Next() > 0.0 ? 50.0f : 0.0f;   // over 58 / 1.5: mended to no tiers
        declared.qualityTargetHeight = rng.Next() > 0.0 ? 540u : 0u;
        declared.qualityMaxShare = static_cast<float>(0.5 + 0.5 * rng.Next());
        const DynamicResolutionConfig config = DynamicResolution::ForWindow(declared, rng.Next() > 0.0 ? 720u : 1600u);
        DynamicResolution e47(config);
        DynamicResolution e50(config);
        const bool paced = rng.Next() > 0.6;
        e47.SetLoopPaced(paced);
        e50.SetLoopPaced(paced);
        const double pixelSeconds = 0.002 + 0.1 * (rng.Next() + 1.0);
        const double fixedSeconds = 0.01 * (rng.Next() + 1.0);
        bool same = e50SameState(e47, e50);
        for (int i = 0; i < 400 && same; ++i) {
            if (i == 200 && trace % 5 == 0) {
                const DynamicResolutionConfig again = trace % 10 == 0 ? DynamicResolution::Pinned(config, 0.6f) : config;
                e47.Reconfigure(again);
                e50.Reconfigure(again);
            }
            const double kind = rng.Next();
            float frame = 0.0f;
            if (kind > 0.97) {
                frame = 2.0f;
            } else if (kind > 0.95) {
                frame = 0.0f;
            } else if (kind > 0.93) {
                frame = -0.01f;
            } else if (kind > 0.90) {
                frame = 0.3f;
            } else {
                const double scale = static_cast<double>(e47.Scale());
                frame = static_cast<float>((fixedSeconds + pixelSeconds * scale * scale) * (1.0 + 0.05 * rng.Next()));
            }
            const float wait = rng.Next() > 0.0 ? -1.0f : 0.5f * frame;
            const bool a = e47.Observe(frame);
            const bool b = e50.ObserveFrame(frame, wait);
            same = a == b && e50SameState(e47, e50);
            if (e50.TakeTierChange() != DynamicResolution::TierChange::None || e50.Tiered() || e50.TierFps() > 0.0f) ++tierSignals;
        }
        if (!same) ++tracesThatDiffered;
    }
    CHECK_EQ(tracesThatDiffered, 0);
    CHECK_EQ(tierSignals, 0);

    const DynamicResolutionConfig defaults;
    CHECK_MSG(defaults.lowTierFps <= 0.0f && defaults.qualityTargetHeight == 0u && defaults.windowHeight == 0u,
              "no tiers unless a game asks");
    CHECK(!DynamicResolution(defaults).Tiered());
    GameManifest asking;
    asking.isGame = true;
    asking.dynamicResolution.enabled = true;
    asking.dynamicResolution.lowTierFps = 30.0f;
    asking.dynamicResolution.qualityTargetHeight = 540;
    asking.dynamicResolution.qualityMaxShare = 0.6f;
    asking.dynamicResolution.windowHeight = 800;
    GameManifest quiet;
    quiet.isGame = true;
    CHECK_MSG(GameRuntime::Serialize(asking) == GameRuntime::Serialize(quiet), "the manifest's text carries none of E50's fields");
}

// 10. The quality floor for each window and scene (lines of art 0 = unknown, 439 a phone level at 175%, 614 a tablet level at
// 125%, 768 a menu), from the formula: 540 lines or the scene's own if fewer, at most 75% of the window, never under the
// 360-line floor. And the mending of the new fields.
static void testE50TheQualityFloorForEachWindowAndScene() {
    DynamicResolutionConfig declared;
    declared.enabled = true;
    declared.minTargetHeight = 360;
    declared.emergencyBelowFps = 30.0f;
    declared.lowTierFps = 30.0f;
    declared.qualityTargetHeight = 540;
    struct Row {
        std::uint32_t height;
        float q[4];
    };
    const std::uint32_t lines[4] = {0u, 439u, 614u, 768u};
    const Row rows[] = {
        {720u, {0.75f, 0.62f, 0.75f, 0.75f}},    // 1600x720: 540 lines is exactly 75%; 446 lines for 439
        {800u, {0.68f, 0.55f, 0.68f, 0.68f}},    // 1340x800 (the tablet): 544 lines
        {1080u, {0.50f, 0.41f, 0.50f, 0.50f}},
        {1344u, {0.41f, 0.33f, 0.41f, 0.41f}},   // 2992x1344
        {1600u, {0.34f, 0.30f, 0.34f, 0.34f}},   // 2560x1600 (the Pad 5): 439 lines is under the 0.30 floor
    };
    for (const Row& row : rows) {
        const DynamicResolutionConfig forWindow = DynamicResolution::ForWindow(declared, row.height);
        CHECK_EQ(forWindow.windowHeight, row.height);
        for (int i = 0; i < 4; ++i) {
            CHECK_MSG(std::fabs(DynamicResolution::QualityScaleFor(forWindow, lines[i]) - row.q[i]) < 1e-4f,
                      "Q for " + std::to_string(row.height) + " lines and a scene of " + std::to_string(lines[i]) + " was " +
                          std::to_string(DynamicResolution::QualityScaleFor(forWindow, lines[i])));
        }
    }
    // No tiers, or no height: the floor as it is.
    DynamicResolutionConfig untiered = declared;
    untiered.lowTierFps = 0.0f;
    CHECK_NEAR(DynamicResolution::QualityScaleFor(DynamicResolution::ForWindow(untiered, 800), 768), 0.45f);
    CHECK_NEAR(DynamicResolution::QualityScaleFor(declared, 768), declared.minScale);
    // A share under the lines' own asks for less.
    DynamicResolutionConfig shared = declared;
    shared.qualityMaxShare = 0.6f;
    CHECK_NEAR(DynamicResolution::QualityScaleFor(DynamicResolution::ForWindow(shared, 720), 768), 0.6f);

    // Mended: a lower tier in [10, target / 1.5], a share in [0.05, 1].
    const auto lowTier = [&](float target, float low) {
        DynamicResolutionConfig config = declared;
        config.targetFps = target;
        config.lowTierFps = low;
        return DynamicResolution::Mended(config).lowTierFps;
    };
    CHECK_NEAR(lowTier(58.0f, 30.0f), 30.0f);
    CHECK_NEAR(lowTier(58.0f, 10.0f), 10.0f);
    CHECK_NEAR(lowTier(58.0f, 38.6f), 38.6f);
    CHECK_NEAR(lowTier(58.0f, 39.0f), 0.0f);
    CHECK_NEAR(lowTier(58.0f, 9.9f), 0.0f);
    CHECK_NEAR(lowTier(30.0f, 30.0f), 0.0f);
    CHECK_NEAR(lowTier(58.0f, -5.0f), 0.0f);
    DynamicResolutionConfig share = declared;
    share.qualityMaxShare = 2.0f;
    CHECK_NEAR(DynamicResolution::Mended(share).qualityMaxShare, 1.0f);
    share.qualityMaxShare = 0.0f;
    CHECK_NEAR(DynamicResolution::Mended(share).qualityMaxShare, 0.05f);
}

// 11. The scene note is latched as a request is: the last before the frame wins, taking it clears it, and with no controller
// to take it it is dropped.
static void testE50TheSceneNoteIsLatchedLikeARequest() {
    RenderScaleControl control;
    std::string key;
    std::uint32_t lines = 7;
    CHECK(!control.TakeScene(&key, &lines));
    control.Publish(true, 1.0f);
    control.NoteScene("menu", 768);
    control.NoteScene("level1", 439);
    CHECK(control.TakeScene(&key, &lines));
    CHECK(key == "level1");
    CHECK_EQ(lines, 439u);
    CHECK_MSG(!control.TakeScene(&key, &lines), "taking it cleared it");
    control.NoteScene("level2", 0);
    control.Publish(false, 1.0f);
    CHECK_MSG(!control.TakeScene(&key, &lines), "no controller: the note is dropped, not kept for later");
    control.Publish(true, 1.0f);
    control.NoteScene("level3", 614);
    RenderScaleRequest request;
    CHECK_MSG(!control.TakeRequest(&request), "a note is not a request");
    CHECK(control.TakeScene(nullptr, nullptr));
}

// 11, the ceiling. RequestCapped is latched as RequestFixed is: the last request before the frame wins (a ceiling over a
// pin, and a pin over a ceiling), the share is clamped the same way, and with no controller to take it it is dropped.
static void testE50ARequestForACeilingIsLatchedAndClampedAsAFixedOneIs() {
    RenderScaleControl control;
    RenderScaleRequest request;
    control.RequestCapped(0.5f);
    control.Publish(false, 1.0f);
    CHECK_MSG(!control.TakeRequest(&request), "no controller: the ceiling is dropped, not kept for later");

    control.Publish(true, 1.0f);
    control.RequestFixed(1.0f);
    control.RequestCapped(0.7f);
    CHECK_MSG(control.Mode() == RenderScaleMode::Capped, "the last request wins, and reads back as asked");
    CHECK_NEAR(control.FixedFraction(), 0.7f);
    CHECK(control.TakeRequest(&request));
    CHECK(request.mode == RenderScaleMode::Capped);
    CHECK_NEAR(request.fraction, 0.7f);
    CHECK_MSG(!control.TakeRequest(&request), "taking it cleared it");

    control.RequestCapped(0.5f);
    control.RequestFixed(0.5f);
    CHECK(control.TakeRequest(&request));
    CHECK_MSG(request.mode == RenderScaleMode::Fixed, "a pin after a ceiling wins the same way");

    control.RequestCapped(2.0f);   // above the start size: the start size
    CHECK(control.TakeRequest(&request));
    CHECK(request.mode == RenderScaleMode::Capped);
    CHECK_NEAR(request.fraction, 1.0f);
    control.RequestCapped(-3.0f);  // nonsense: the smallest picture the controller allows, never zero
    CHECK(control.TakeRequest(&request));
    CHECK_NEAR(request.fraction, 0.1f);
    control.RequestCapped(std::nanf(""));
    CHECK(control.TakeRequest(&request));
    CHECK_NEAR(request.fraction, 0.1f);

    // Asked, then the controller went away before the frame took it: dropped as well. And a ceiling is no scene note.
    control.RequestCapped(0.5f);
    control.Publish(false, 1.0f);
    CHECK(!control.TakeRequest(&request));
    control.Publish(true, 1.0f);
    control.RequestCapped(0.5f);
    CHECK(!control.TakeScene(nullptr, nullptr));
}

// ---- 12. Balanced and Fast as ceilings (RenderScaleMode::Capped) ----

// The windows of the quality floor's table, each started at the scale the game's 2.2 MP budget allows (SupersonicApp's
// arithmetic): 1.0, 1.0, 0.92 and 0.73.
struct E50Window {
    const char* name;
    std::uint32_t width;
    std::uint32_t height;
};
constexpr E50Window kE50Windows[] = {
    {"1600x720", 1600u, 720u},
    {"1340x800", 1340u, 800u},
    {"2400x1080", 2400u, 1080u},
    {"2560x1600", 2560u, 1600u},
};

// A controller's scales are two decimals, so one under a ceiling that is not (0.73 x 0.5 = 0.365) starts at the ceiling
// rounded (0.37), as a pin of that size is held at it: half a hundredth, and a hair for float (0.365 + 0.005 is 0.37 only
// to the last bit).
constexpr float kE50CeilingSlack = 0.0051f;

// The game's declaration for a window, with the start its pixel budget allows.
static DynamicResolutionConfig e50DeclaredFor(const E50Window& window) {
    constexpr std::uint32_t kStartMaxPixels = 2200000;
    DynamicResolutionConfig declared = e50Declared(1.0f);
    const double windowPixels = static_cast<double>(window.width) * static_cast<double>(window.height);
    if (windowPixels > kStartMaxPixels) {
        declared.startScale = std::min(declared.startScale, static_cast<float>(std::sqrt(kStartMaxPixels / windowPixels)));
    }
    return declared;
}

// A ceiling as the tests expect it: what was asked, no higher than the window's quality floor `q` where the switch
// DynamicResolution::kCeilingAtMostTheQualityFloor is on (`underQ`), inside [the window's floor, 1].
static float e50Ceiling(float asked, float windowFloor, float q, bool underQ) {
    const float ceiling = underQ ? std::min(asked, q) : asked;
    return std::min(std::max(ceiling, windowFloor), 1.0f);
}

// Every field of a configuration, the floats bit for bit (a field added to DynamicResolutionConfig belongs here too).
static bool e50SameConfig(const DynamicResolutionConfig& a, const DynamicResolutionConfig& b) {
    return a.enabled == b.enabled && e50SameBits(a.startScale, b.startScale) && a.startMaxPixels == b.startMaxPixels &&
           e50SameBits(a.minScale, b.minScale) && e50SameBits(a.maxScale, b.maxScale) &&
           e50SameBits(a.targetFps, b.targetFps) && e50SameBits(a.headroomFps, b.headroomFps) &&
           e50SameBits(a.windowSeconds, b.windowSeconds) && e50SameBits(a.upAfterSeconds, b.upAfterSeconds) &&
           a.relaxedRise == b.relaxedRise && e50SameBits(a.riseMargin, b.riseMargin) &&
           a.minTargetHeight == b.minTargetHeight && e50SameBits(a.emergencyBelowFps, b.emergencyBelowFps) &&
           e50SameBits(a.emergencyScale, b.emergencyScale) && e50SameBits(a.lowTierFps, b.lowTierFps) &&
           a.qualityTargetHeight == b.qualityTargetHeight && e50SameBits(a.qualityMaxShare, b.qualityMaxShare) &&
           a.windowHeight == b.windowHeight;
}

// Whether a pick asks for what the controller already is (DynamicResolution::RequestIsApplied, asked before anything is
// changed: a new configuration forgets what the controller learned). Every request a picture row makes against every state
// the engine can be in, and one it is never left in. The rules for a game that never asks for a ceiling are E47's: a pin is
// applied when held at its share, Automatic whenever nothing is held (an overlay's measurement size included: Automatic then
// acts).
static void testE50WhetherAPickIsAlreadyAppliedInEveryState() {
    struct Request {
        const char* name;
        RenderScaleMode mode;
        float fraction;
    };
    const Request requests[] = {
        {"Automatic", RenderScaleMode::Automatic, 1.0f},   // RequestAutomatic's fraction
        {"Sharp (held at 1)", RenderScaleMode::Fixed, 1.0f},
        {"held at 0.5", RenderScaleMode::Fixed, 0.5f},
        {"Balanced (capped at 0.7)", RenderScaleMode::Capped, 0.7f},
        {"Fast (capped at 0.5)", RenderScaleMode::Capped, 0.5f},
    };
    struct State {
        const char* name;
        bool pinned;
        float pinnedFraction;
        float cappedFraction;
        bool applied[5];   // for each of `requests`, in order
    };
    const State states[] = {
        {"the game's own controller (or the overlay's Auto)", false, 0.0f, 0.0f, {true, false, false, false, false}},
        {"held at an overlay's measurement size", true, 0.0f, 0.0f, {false, false, false, false, false}},
        {"held at Sharp", true, 1.0f, 0.0f, {false, true, false, false, false}},
        {"held at 0.5", true, 0.5f, 0.0f, {false, false, true, false, false}},
        {"capped at Balanced", false, 0.0f, 0.7f, {false, false, false, true, false}},
        {"capped at Fast", false, 0.0f, 0.5f, {false, false, false, false, true}},
        {"capped at the whole start size (no lower tier: not Automatic)", false, 0.0f, 1.0f, {false, false, false, false, false}},
        {"held at Sharp with a ceiling left over (never so)", true, 1.0f, 0.5f, {false, true, false, false, false}},
    };
    for (const State& state : states) {
        for (std::size_t i = 0; i < std::size(requests); ++i) {
            const bool applied = DynamicResolution::RequestIsApplied(requests[i].mode, requests[i].fraction, state.pinned,
                                                                     state.pinnedFraction, state.cappedFraction);
            CHECK_MSG(applied == state.applied[i], std::string(requests[i].name) + " asked when " + state.name + ": " +
                                                       (applied ? "applied" : "not applied"));
        }
    }
}

// The configuration each pick gives the controller, for the four windows: Automatic is the window's own, field for field;
// Fixed is the pin E47 made inline, bit for bit; Capped is the window's own with its start and its ceiling at max(floor,
// start x f), every floor, the emergency floor, the relaxed rise and the target kept, and no lower tier. A ceiling under the
// floor is the floor, and still falls to the emergency floor where the frames ask; so is one under 1.25 x the floor where
// there is an emergency floor (DynamicResolution::kCeilingNearTheFloor), and only there.
static void testE50EachPickIsAConfigurationForItsWindow() {
    struct Expected {
        float start;
        float floor;
        float q;
        float balanced;
        float fast;
        float fastFloor;   // the floor Fast's configuration runs on, on a loop the display paces (unpaced: the window's)
    };
    const Expected expected[] = {
        {1.0f, 0.5f, 0.75f, 0.7f, 0.5f, 0.5f},          // 1600x720: Fast's ceiling is the 360-line floor itself
        {1.0f, 0.45f, 0.68f, 0.7f, 0.5f, 0.5f},         // 1340x800: Balanced's above Q; Fast's under 0.5625, the floor
        {0.92f, 0.34f, 0.5f, 0.644f, 0.46f, 0.34f},     // 2400x1080: Balanced's above Q; Fast's over 0.425
        {0.73f, 0.3f, 0.34f, 0.511f, 0.365f, 0.3f},     // 2560x1600: both above Q; no emergency floor, so Fast's floor stays
    };
    const bool underQ = DynamicResolution::kCeilingAtMostTheQualityFloor;
    for (std::size_t w = 0; w < std::size(kE50Windows); ++w) {
        const E50Window& window = kE50Windows[w];
        const Expected& want = expected[w];
        const std::string name = window.name;
        const DynamicResolutionConfig declared = e50DeclaredFor(window);
        const DynamicResolutionConfig forWindow = DynamicResolution::ForWindow(declared, window.height);
        const float start = DynamicResolution(forWindow).Scale();
        CHECK_MSG(std::fabs(start - want.start) < 1e-4f, name + ": started at " + std::to_string(start));
        CHECK_MSG(std::fabs(forWindow.minScale - want.floor) < 1e-4f, name + ": floor " + std::to_string(forWindow.minScale));

        CHECK_MSG(e50SameConfig(DynamicResolution::ForRequest(declared, window.height, start, RenderScaleMode::Automatic, 1.0f),
                                forWindow),
                  name + ": Automatic is the window's own configuration");
        for (const float fraction : {1.0f, 0.7f, 0.5f, 0.1f}) {
            // SupersonicApp's pin before ForRequest, operand for operand (its m_resolutionConfig is ForWindow's).
            const DynamicResolutionConfig e47 =
                DynamicResolution::Pinned(forWindow, std::min(1.0f, std::max(forWindow.minScale, start * fraction)));
            CHECK_MSG(e50SameConfig(DynamicResolution::ForRequest(declared, window.height, start, RenderScaleMode::Fixed, fraction),
                                    e47),
                      name + ": Fixed " + std::to_string(fraction) + " is E47's pin, bit for bit");
        }
        const float asked[] = {want.balanced, want.fast, 0.0f};   // 0: a tenth of the start, under every floor
        const float floors[] = {want.floor, want.fastFloor, want.floor};
        const float fractions[] = {0.7f, 0.5f, 0.1f};
        for (std::size_t i = 0; i < std::size(fractions); ++i) {
            const DynamicResolutionConfig capped =
                DynamicResolution::ForRequest(declared, window.height, start, RenderScaleMode::Capped, fractions[i], true);
            const float ceiling = e50Ceiling(asked[i], want.floor, want.q, underQ);
            const std::string what = name + ": capped at " + std::to_string(fractions[i]);
            CHECK_MSG(std::fabs(capped.maxScale - ceiling) < 1e-4f && std::fabs(capped.startScale - ceiling) < 1e-4f,
                      what + " has its ceiling at " + std::to_string(capped.maxScale) + ", expected " + std::to_string(ceiling));
            DynamicResolutionConfig kept = forWindow;
            kept.startScale = kept.maxScale = capped.maxScale;
            if (std::fabs(floors[i] - forWindow.minScale) > 1e-4f) kept.minScale = capped.maxScale;   // the ceiling is the floor
            kept.lowTierFps = 0.0f;
            CHECK_MSG(e50SameConfig(capped, kept) && std::fabs(capped.minScale - floors[i]) < 1e-4f,
                      what + ": floor " + std::to_string(capped.minScale) + "; everything else is the window's own, but the lower tier");
            const DynamicResolution controller(capped);
            CHECK_MSG(!controller.Tiered() && controller.Scale() <= ceiling + kE50CeilingSlack,
                      what + ": no tiers, and it starts at the ceiling (" + std::to_string(controller.Scale()) + ")");
        }
    }

    // Capped itself: a ceiling above the window is the window; one under the floor is the floor, and the emergency floor is
    // still under it: a slow GPU takes the picture there, where a pin of the same size would hold it.
    const DynamicResolutionConfig forPhone = DynamicResolution::ForWindow(e50Declared(1.0f), 720);   // floor 0.5, emergency 0.3
    CHECK_NEAR(DynamicResolution::Capped(forPhone, 2.0f).maxScale, 1.0f);
    const DynamicResolutionConfig atTheFloor = DynamicResolution::Capped(forPhone, 0.1f);
    CHECK_MSG(std::fabs(atTheFloor.maxScale - 0.5f) < 1e-4f && std::fabs(atTheFloor.emergencyScale - 0.3f) < 1e-4f,
              "a ceiling under the floor is the floor, with the emergency floor kept");
    DynamicResolution slow(atTheFloor);
    driveAStandInGpu(slow, 0.4, 4000);
    CHECK_NEAR(slow.Scale(), 0.3f);
    // Near the floor: under 1.25 x the 360-line floor (0.45 on 800 lines: 0.5625) the ceiling is the floor, with the
    // emergency floor and its trigger kept; at or over it the floor is the window's. With no emergency floor (1600 lines,
    // whose 360-line floor is under the declared 0.3) the floor stays, the one way down.
    const DynamicResolutionConfig forTablet = DynamicResolution::ForWindow(e50Declared(1.0f), 800);
    const DynamicResolutionConfig nearTheFloor = DynamicResolution::Capped(forTablet, 0.56f, true);
    CHECK_MSG(std::fabs(nearTheFloor.minScale - 0.56f) < 1e-4f && std::fabs(nearTheFloor.maxScale - 0.56f) < 1e-4f &&
                  std::fabs(nearTheFloor.emergencyScale - 0.3f) < 1e-4f &&
                  std::fabs(nearTheFloor.emergencyBelowFps - 30.0f) < 1e-4f,
              "a ceiling of 0.56 over a 0.45 floor is the floor, the emergency floor under it: floor " +
                  std::to_string(nearTheFloor.minScale));
    CHECK_MSG(std::fabs(DynamicResolution::Capped(forTablet, 0.57f, true).minScale - 0.45f) < 1e-4f,
              "a ceiling of 0.57 over a 0.45 floor keeps the floor");
    CHECK_MSG(std::fabs(DynamicResolution::Capped(forTablet, 0.56f, false).minScale - 0.45f) < 1e-4f,
              "unpaced, a ceiling near the floor keeps the floor: the step to it is judged on what it bought");
    const DynamicResolutionConfig forPad = DynamicResolution::ForWindow(e50Declared(0.73f), 1600);
    CHECK_MSG(forPad.emergencyScale == 0.0f &&
                  std::fabs(DynamicResolution::Capped(forPad, 0.365f, true).minScale - 0.3f) < 1e-4f,
              "with no emergency floor a ceiling near the floor keeps the floor");
}

// A ceiling at its floor (1340x800's Fast: 0.5 over the 0.45 floor, made the floor) still falls to the emergency floor, so
// it is not a size held: a scene left remembers its size and holds, a scene entered settles, a first visit starts where the
// last scene was and rises, and a scene seen before is restored. A heavy menu at 288 ms/MP falls to the emergency floor; a
// light level after it rises back to the ceiling; the menu again is where it was left, settling.
static void testE50ACeilingAtItsFloorRemembersEachScene() {
    const E50Window& tablet = kE50Windows[1];   // 1340x800
    const DynamicResolutionConfig declared = e50DeclaredFor(tablet);
    const float start = DynamicResolution(DynamicResolution::ForWindow(declared, tablet.height)).Scale();
    const DynamicResolutionConfig config =
        DynamicResolution::ForRequest(declared, tablet.height, start, RenderScaleMode::Capped, 0.5f, true);
    CHECK_MSG(std::fabs(config.minScale - 0.5f) < 1e-4f && std::fabs(config.maxScale - 0.5f) < 1e-4f &&
                  std::fabs(config.emergencyScale - 0.3f) < 1e-4f,
              "Fast on 1340x800 is a ceiling at its floor with the emergency floor under it");
    DynamicResolution controller(config);
    E50Lcg rng{1301u};
    controller.EnterScene("menu", 768);
    e50Drive(controller, e50Device(1.072, 2.0, 288.0, 10.0), e50Stretch(60.0, 0.0), rng);
    const float menuScale = controller.Scale();
    CHECK_MSG(menuScale < 0.45f - 0.001f, "the heavy menu falls under the pixel floor: " + std::to_string(menuScale));

    controller.EnterScene("level", 614);
    CHECK_MSG(controller.SceneSettling() && !controller.LastSceneRemembered(),
              "a first visit settles and is not a scene seen before");
    CHECK_MSG(std::fabs(controller.Scale() - menuScale) < 1e-4f,
              "a first visit starts where the menu was: " + std::to_string(controller.Scale()));
    e50Drive(controller, e50Device(1.072, 2.0, 10.0, 5.0), e50Stretch(60.0, 0.0), rng);
    CHECK_MSG(std::fabs(controller.Scale() - 0.5f) < 1e-4f, "the light level rose to the ceiling: " + std::to_string(controller.Scale()));

    const bool moved = controller.EnterScene("menu", 768);
    CHECK_MSG(moved && controller.LastSceneRemembered() && controller.SceneSettling(),
              "the menu again is remembered, settles, and moves the picture");
    CHECK_MSG(std::fabs(controller.Scale() - menuScale) < 1e-4f,
              "the menu's own size restored: " + std::to_string(controller.Scale()) + " against " + std::to_string(menuScale));
}

// The point of a ceiling: on the slow tablet's heaviest menu a pin at Fast's size is held there at 12-13 fps, where the
// ceiling falls through the 360-line floor to the emergency floor (as the game's own controller does) and keeps 30 or more;
// and on a fast GPU afterwards it rises back to its ceiling and no further.
static void testE50ACeilingFallsWhereAPinIsHeldAndRisesNoHigherThanItself() {
    const E50Window& tablet = kE50Windows[1];   // 1340x800
    const DynamicResolutionConfig declared = e50DeclaredFor(tablet);
    const float start = DynamicResolution(DynamicResolution::ForWindow(declared, tablet.height)).Scale();
    DynamicResolution pin(DynamicResolution::ForRequest(declared, tablet.height, start, RenderScaleMode::Fixed, 0.5f));
    DynamicResolution ceiling(DynamicResolution::ForRequest(declared, tablet.height, start, RenderScaleMode::Capped, 0.5f));
    pin.EnterScene("menu", 768);
    ceiling.EnterScene("menu", 768);
    const E50Device heavy = e50Device(1.072, 2.0, 288.0, 10.0);
    E50Lcg pinRng{1201u};
    E50Lcg ceilingRng{1201u};
    const E50Run pinRun = e50Drive(pin, heavy, E50Drive{}, pinRng);
    const E50Run ceilingRun = e50Drive(ceiling, heavy, E50Drive{}, ceilingRng);
    e50PrintRest("case 12, GE8320 menu 288 ms/MP held at Fast's size", pin, pinRun);
    e50PrintRest("case 12, GE8320 menu 288 ms/MP capped at Fast's size", ceiling, ceilingRun);
    CHECK_EQ(pinRun.resizes, 0);
    CHECK_NEAR(pin.Scale(), 0.5f);
    CHECK_MSG(ceiling.Scale() < 0.45f - 0.001f, "under the 360-line floor: the emergency floor is the ceiling's too");
    CHECK_MSG(ceilingRun.RestFps() > 1.5f * pinRun.RestFps() && ceilingRun.RestFps() >= kE50SteadyFps,
              "far faster than the pin: " + std::to_string(ceilingRun.RestFps()) + " fps against " +
                  std::to_string(pinRun.RestFps()));
    CHECK(ceilingRun.maxScale <= 0.5f + kE50CeilingSlack);

    const E50Run fastRun = e50Drive(ceiling, e50Device(1.072, 2.0, 10.0, 5.0), E50Drive{}, ceilingRng);
    CHECK_MSG(std::fabs(ceiling.Scale() - 0.5f) < 1e-4f, "back at its ceiling: " + std::to_string(ceiling.Scale()));
    CHECK(fastRun.maxScale <= 0.5f + kE50CeilingSlack);
}

// Never slower. The four picks against the same stand-in devices for 180 simulated seconds each (the last two minutes the
// rest): a GPU with a part that ignores the picture's size of {0, 9, 20} ms and a part that follows the pixel count of {10,
// 40, 120, 480} ms per megapixel (of the window's pixels), a CPU of kGridCpuMs, on the four windows, unpaced and paced
// (FIFO). The picks: the game's own two-tier policy (Automatic), Balanced and Fast (ceilings of 0.7 and 0.5 of the start
// size) and Sharp (the start size, held). No ceiling is ever exceeded (to the two decimals of kE50CeilingSlack), Balanced's
// and Fast's median frame rates at rest are each at least kNeverSlowerShare of Automatic's, and the pin never resizes.
// Nothing makes the second hold by construction where the policy's quality floor Q is under a ceiling (Balanced on
// 1340x800, 2400x1080 and 2560x1600; Fast on 2560x1600, 0.365 over 0.34): both aim at the target under Q, and a ceiling's
// small step down can be taken back where the policy's larger one is kept (DynamicResolution::kCeilingAtMostTheQualityFloor
// is the switch for that). A ceiling just above the 360-line floor is that floor (DynamicResolution::kCeilingNearTheFloor),
// so its fall to the emergency floor does not wait on a step to the pixel floor that a paced loop can take back. A failing
// cell prints one "E50 grid:" line; the summary names the tightest cell of each pick.
constexpr double kGridCpuMs = 5.0;
constexpr double kGridSeconds = 180.0;
constexpr double kGridRestFrom = 60.0;
constexpr float kNeverSlowerShare = 0.94f;

static void testE50ACeilingIsNeverSlowerThanAutomaticAndNeverExceeded() {
    const double fixedCosts[] = {0.0, 9.0, 20.0};
    const double pixelCosts[] = {10.0, 40.0, 120.0, 480.0};
    const char* const picks[] = {"Automatic", "Balanced", "Fast", "Sharp"};
    const bool underQ = DynamicResolution::kCeilingAtMostTheQualityFloor;
    int cells = 0;
    int overACeiling = 0;
    int balancedSlower = 0;
    int fastSlower = 0;
    int pinResized = 0;
    float tightestBalanced = 1e9f;
    float tightestFast = 1e9f;
    std::string tightestBalancedCell;
    std::string tightestFastCell;
    std::uint32_t seed = 12000u;
    for (const E50Window& window : kE50Windows) {
        const DynamicResolutionConfig declared = e50DeclaredFor(window);
        const DynamicResolutionConfig forWindow = DynamicResolution::ForWindow(declared, window.height);
        const float start = DynamicResolution(forWindow).Scale();
        const float q = DynamicResolution::QualityScaleFor(forWindow, 0);
        const float ceilings[4] = {1.0f, e50Ceiling(start * 0.7f, forWindow.minScale, q, underQ),
                                   e50Ceiling(start * 0.5f, forWindow.minScale, q, underQ), start};
        const double megapixels = static_cast<double>(window.width) * static_cast<double>(window.height) / 1e6;
        for (const bool paced : {false, true}) {
            // A ceiling near its floor is made the floor on a paced loop only (DynamicResolution::Capped).
            const DynamicResolutionConfig configs[4] = {
                DynamicResolution::ForRequest(declared, window.height, start, RenderScaleMode::Automatic, 1.0f),
                DynamicResolution::ForRequest(declared, window.height, start, RenderScaleMode::Capped, 0.7f, paced),
                DynamicResolution::ForRequest(declared, window.height, start, RenderScaleMode::Capped, 0.5f, paced),
                DynamicResolution::ForRequest(declared, window.height, start, RenderScaleMode::Fixed, 1.0f),
            };
            for (const double fixedMs : fixedCosts) {
                for (const double pixelMs : pixelCosts) {
                    ++cells;
                    ++seed;
                    E50Device device = e50Device(megapixels, fixedMs, pixelMs, kGridCpuMs);
                    device.fifo = paced;
                    E50Run runs[4];
                    float scales[4] = {};
                    float autoTier = 0.0f;
                    for (int i = 0; i < 4; ++i) {
                        DynamicResolution controller(configs[i]);
                        controller.SetLoopPaced(paced);
                        controller.EnterScene("grid", 768);   // a game that notes scenes notes one for every new controller
                        E50Lcg rng{seed};                     // the same jitter for the four
                        runs[i] = e50Drive(controller, device, e50Stretch(kGridSeconds, kGridRestFrom), rng);
                        scales[i] = controller.Scale();
                        if (i == 0) autoTier = controller.TierFps();
                    }
                    const bool over = runs[1].maxScale > ceilings[1] + kE50CeilingSlack ||
                                      runs[2].maxScale > ceilings[2] + kE50CeilingSlack;
                    const float autoFps = runs[0].RestFps();
                    const float balancedRatio = autoFps > 0.0f ? runs[1].RestFps() / autoFps : 0.0f;
                    const float fastRatio = autoFps > 0.0f ? runs[2].RestFps() / autoFps : 0.0f;
                    const bool balancedSlow = !(balancedRatio >= kNeverSlowerShare);
                    const bool fastSlow = !(fastRatio >= kNeverSlowerShare);
                    const bool resized = runs[3].resizes != 0;
                    char cell[96];
                    std::snprintf(cell, sizeof(cell), "%s %s, GPU %.0f ms + %.0f ms/MP", window.name,
                                  paced ? "paced" : "unpaced", fixedMs, pixelMs);
                    if (balancedRatio < tightestBalanced) {
                        tightestBalanced = balancedRatio;
                        tightestBalancedCell = cell;
                    }
                    if (fastRatio < tightestFast) {
                        tightestFast = fastRatio;
                        tightestFastCell = cell;
                    }
                    overACeiling += over ? 1 : 0;
                    balancedSlower += balancedSlow ? 1 : 0;
                    fastSlower += fastSlow ? 1 : 0;
                    pinResized += resized ? 1 : 0;
                    if (over || balancedSlow || fastSlow || resized) {
                        std::string line = std::string("E50 grid: ") + cell + ":";
                        for (int i = 0; i < 4; ++i) {
                            char part[160];
                            std::snprintf(part, sizeof(part), " %s%s at %.2f (most %.3f, ceiling %.3f), %.1f fps, %d resizes;",
                                          picks[i], i == 0 ? (autoTier > 40.0f ? " in the upper tier" : " in the lower tier") : "",
                                          static_cast<double>(scales[i]), static_cast<double>(runs[i].maxScale),
                                          static_cast<double>(ceilings[i]), static_cast<double>(runs[i].RestFps()),
                                          runs[i].resizes);
                            line += part;
                        }
                        char verdict[200];
                        std::snprintf(verdict, sizeof(verdict),
                                      " Balanced at %.3f and Fast at %.3f of Automatic's frame rate%s%s%s%s",
                                      static_cast<double>(balancedRatio), static_cast<double>(fastRatio),
                                      over ? ", a ceiling exceeded" : "", balancedSlow ? ", Balanced slower" : "",
                                      fastSlow ? ", Fast slower" : "", resized ? ", the pin resized" : "");
                        line += verdict;
                        std::printf("%s\n", line.c_str());
                    }
                }
            }
        }
    }
    std::printf("E50 grid: %d cells; %d with a ceiling exceeded, %d with Balanced and %d with Fast under %.2f of Automatic's "
                "frame rate, %d with the pin resized; the tightest: Balanced at %.3f of Automatic's (%s), Fast at %.3f (%s)\n",
                cells, overACeiling, balancedSlower, fastSlower, static_cast<double>(kNeverSlowerShare), pinResized,
                static_cast<double>(tightestBalanced), tightestBalancedCell.c_str(), static_cast<double>(tightestFast),
                tightestFastCell.c_str());
    CHECK_EQ(cells, 96);
    CHECK_EQ(overACeiling, 0);
    CHECK_EQ(balancedSlower, 0);
    CHECK_EQ(fastSlower, 0);
    CHECK_EQ(pinResized, 0);
}

// 2. The Galaxy Tab A7 Lite (PowerVR GE8320, 1340x800, 1.072 MP): its menu cannot keep 58 fps at the quality floor (0.68), so
// it goes to the lower tier and keeps 30 there, at a size above the emergency floor where it can; its levels (a third of the
// menu's cost) keep 30 at a far sharper size than the menu's.
constexpr float kGe8320MenuScaleLow = 0.30f;
constexpr float kGe8320MenuScaleHigh = 0.45f;          // exclusive: under the 360-line floor, as the menu needs
constexpr int kGe8320MenuRestResizes = 1;
constexpr double kGe8320FirstSteadyWindowSeconds = 12.0;   // E47 took about 6-7 s, by the same hand walk
constexpr float kGe8320LevelScaleLight = 0.58f;        // levels at 54 ms/MP
constexpr float kGe8320LevelScaleHeavy = 0.45f;        // levels at 96 ms/MP

static void testE50ASlowTabletKeepsThirtyAtTheLargestSizeThatHoldsIt() {
    E50Lcg rng{2u};
    for (const double cpuMs : {10.0, 26.0}) {
        for (const double menuCost : {162.0, 288.0}) {
            DynamicResolution controller(e50Config(1.0f, 800));
            controller.EnterScene("menu", 768);
            const E50Device menu = e50Device(1.072, 2.0, menuCost, cpuMs);
            const E50Run menuRun = e50Drive(controller, menu, E50Drive{}, rng);
            const std::string name = "case 2, GE8320 menu " + std::to_string(static_cast<int>(menuCost)) + " ms/MP, CPU " +
                                     std::to_string(static_cast<int>(cpuMs)) + " ms";
            e50PrintRest(name, controller, menuRun);
            CHECK_MSG(e50InTier(controller, 30.0f), name + ": the lower tier");
            CHECK_MSG(controller.Scale() >= kGe8320MenuScaleLow - 0.001f && controller.Scale() < kGe8320MenuScaleHigh, name + ": its size");
            CHECK_MSG(menuRun.RestFps() >= kE50SteadyFps, name + ": a steady 30");
            CHECK_MSG(menuRun.restResizes <= kGe8320MenuRestResizes, name + ": at rest");
            CHECK_MSG(menuRun.firstSteadyWindowSeconds > 0.0 && menuRun.firstSteadyWindowSeconds <= kGe8320FirstSteadyWindowSeconds,
                      name + ": the first steady window");

            E50Device level = menu;
            level.msPerMegapixel = menuCost / 3.0;
            controller.EnterScene("level", 614);
            const E50Run levelRun = e50Drive(controller, level, E50Drive{}, rng);
            const std::string levelName = "case 2, GE8320 level " + std::to_string(static_cast<int>(level.msPerMegapixel)) +
                                          " ms/MP, CPU " + std::to_string(static_cast<int>(cpuMs)) + " ms";
            e50PrintRest(levelName, controller, levelRun);
            CHECK_MSG(e50InTier(controller, 30.0f), levelName + ": the lower tier");
            CHECK_MSG(levelRun.RestFps() >= kE50SteadyFps, levelName + ": a steady 30");
            // With 26 ms of CPU the frame is the CPU's under about 0.65 (0.48 for the heavy level), so a rise sized as if the
            // frame were the GPU's is a step of about 0.01 from the menu's 0.31 and never taken: the lower tier's least rise
            // (kLowTierMinRiseStep) is what brings these up.
            const float sharp = menuCost < 200.0 ? kGe8320LevelScaleLight : kGe8320LevelScaleHeavy;
            CHECK_MSG(controller.Scale() >= sharp, levelName + ": sharper than the menu");
        }
    }
}

// 3. A Galaxy A05-like phone (1600x720, 1.152 MP): the menu (82 ms/MP) cannot hold 58 at its quality floor (0.75) and keeps 30
// above the 360-line floor; a level (24 ms/MP, 439 lines of art) holds 58 at its own floor (0.62) and goes back up to it.
constexpr float kA05MenuScaleLow = 0.50f;
constexpr float kA05LevelScaleLow = 0.62f;
constexpr float kA05LevelFpsLow = 54.5f;

static void testE50APhoneKeepsThirtyInItsMenuAndSixtyInItsLevels() {
    E50Lcg rng{3u};
    DynamicResolution controller(e50Config(1.0f, 720));
    controller.EnterScene("menu", 768);
    const E50Run menuRun = e50Drive(controller, e50Device(1.152, 2.0, 82.0, 10.0), E50Drive{}, rng);
    e50PrintRest("case 3, A05 menu", controller, menuRun);
    CHECK_MSG(e50InTier(controller, 30.0f), "the menu keeps 30");
    CHECK(controller.Scale() >= kA05MenuScaleLow - 0.001f);
    CHECK(menuRun.RestFps() >= kE50SteadyFps);

    controller.EnterScene("level", 439);
    const E50Run levelRun = e50Drive(controller, e50Device(1.152, 2.0, 24.0, 10.0), E50Drive{}, rng);
    e50PrintRest("case 3, A05 level", controller, levelRun);
    CHECK_MSG(e50InTier(controller, 58.0f), "the level is back in the upper tier");
    CHECK(controller.Scale() >= kA05LevelScaleLow - 0.001f);
    CHECK(levelRun.RestFps() >= kA05LevelFpsLow);
}

// 4. A Xiaomi Pad 5-like tablet (2560x1600, 4.1 MP, starting at 0.73): it holds 58 above its quality floor (0.34), so it never
// leaves the upper tier, not even for the three windows at 8-15 fps it shows at launch and at each scene.
constexpr float kPad5RestLow = 0.35f;
constexpr float kPad5RestHigh = 0.43f;

static void testE50AFastTabletStaysInTheUpperTierThroughItsSlowStarts() {
    E50Lcg rng{4u};
    DynamicResolution controller(e50Config(0.73f, 1600));
    controller.EnterScene("menu", 768);
    const E50Device menu = e50Device(4.1, 5.8, 17.0, 5.0);
    E50Drive slowStart;
    slowStart.slowSeconds = 3.6;
    const E50Run menuRun = e50Drive(controller, menu, slowStart, rng);
    e50PrintRest("case 4, Pad 5 menu", controller, menuRun);
    CHECK_EQ(menuRun.tierChanges, 0);
    CHECK(e50InTier(controller, 58.0f));
    CHECK_MSG(controller.Scale() >= kPad5RestLow && controller.Scale() <= kPad5RestHigh, "where E41 left it: 0.39 measured");

    E50Device level = menu;
    level.msPerMegapixel = 6.0;
    controller.EnterScene("level", 614);
    const E50Run levelRun = e50Drive(controller, level, slowStart, rng);
    e50PrintRest("case 4, Pad 5 level", controller, levelRun);
    CHECK_EQ(levelRun.tierChanges, 0);
    CHECK(e50InTier(controller, 58.0f));

    controller.EnterScene("menu", 768);
    CHECK(controller.LastSceneRemembered());
    const E50Run backRun = e50Drive(controller, menu, slowStart, rng);
    CHECK_EQ(backRun.tierChanges, 0);
}

// 5. A CPU-bound phone: a smaller picture buys nothing. At 22 ms it holds 30 at full size in the lower tier and never probes (the
// frame's work is the CPU's, so no prediction says 58 would hold); at 40 ms (under 30 everywhere) it keeps its picture.
constexpr double kCpuBound40ShareAtStart = 0.90;
// (By the hand walk exactly 12: two steps to Q taken back in the upper tier, the second with the tier down, then a step and
// its revert in the lower tier at its start and after holds of 30, 60 and 120 s; the 240 s hold outlasts the run.)
constexpr int kCpuBound40MaxResizes = 12;

static void testE50ACpuBoundPhoneKeepsItsPictureAndNeverProbes() {
    E50Lcg rng{5u};
    DynamicResolution c22(e50Config(1.0f, 800));
    c22.EnterScene("menu", 768);
    const E50Run run22 = e50Drive(c22, e50Device(1.072, 2.0, 10.0, 22.0), E50Drive{}, rng);
    e50PrintRest("case 5, CPU 22 ms", c22, run22);
    CHECK(e50InTier(c22, 30.0f));
    CHECK(c22.Scale() >= 0.99f);
    CHECK_EQ(run22.probes, 0);

    DynamicResolution c40(e50Config(1.0f, 800));
    c40.EnterScene("menu", 768);
    const E50Run run40 = e50Drive(c40, e50Device(1.072, 2.0, 10.0, 40.0), E50Drive{}, rng);
    e50PrintRest("case 5, CPU 40 ms", c40, run40);
    CHECK(run40.secondsBelowStart <= (1.0 - kCpuBound40ShareAtStart) * run40.seconds);
    CHECK(run40.resizes <= kCpuBound40MaxResizes);
    CHECK_EQ(run40.probes, 0);
}

// 6. A loop the display paces (FIFO): a GPU at 20 ms at the quality floor runs at 30 there, goes to the lower tier and stays (a
// paced window cannot show room, and no wait is trusted for a prediction); a fast one never changes tier; and a heavy menu's
// size under its quality floor does not become a light level's for the session.
static void testE50APacedLoopChangesTierOnlyWhenItMust() {
    E50Lcg rng{6u};
    DynamicResolution slow(e50Config(1.0f, 800));
    slow.SetLoopPaced(true);
    slow.EnterScene("menu", 768);
    E50Device slowGpu = e50Device(1.072, 2.0, 36.3, 8.0);
    slowGpu.fifo = true;
    const E50Run slowRun = e50Drive(slow, slowGpu, E50Drive{}, rng);
    e50PrintRest("case 6, FIFO 20 ms at Q", slow, slowRun);
    CHECK(e50InTier(slow, 30.0f));
    CHECK_EQ(slowRun.downs, 1);
    CHECK_EQ(slowRun.tierChanges, 1);

    DynamicResolution fast(e50Config(1.0f, 800));
    fast.SetLoopPaced(true);
    fast.EnterScene("menu", 768);
    E50Device fastGpu = e50Device(1.072, 2.0, 5.0, 5.0);
    fastGpu.fifo = true;
    const E50Run fastRun = e50Drive(fast, fastGpu, E50Drive{}, rng);
    CHECK_EQ(fastRun.tierChanges, 0);
    CHECK_EQ(fastRun.resizes, 0);
    CHECK(e50InTier(fast, 58.0f));

    // A heavy menu (the GE8320's, at rest in the lower tier at about 0.36, under its quality floor) and then a light level:
    // the level's first visit does not inherit a size it could never leave on a paced loop. It starts at its quality floor,
    // sees 60 there and probes back to the upper tier.
    DynamicResolution heavyFirst(e50Config(1.0f, 800));
    heavyFirst.SetLoopPaced(true);
    heavyFirst.EnterScene("menu", 768);
    E50Device heavyMenu = e50Device(1.072, 2.0, 162.0, 10.0);
    heavyMenu.fifo = true;
    const E50Run heavyRun = e50Drive(heavyFirst, heavyMenu, E50Drive{}, rng);
    e50PrintRest("case 6, FIFO heavy menu", heavyFirst, heavyRun);
    CHECK_MSG(e50InTier(heavyFirst, 30.0f) && heavyFirst.Scale() < heavyFirst.QualityScale() - 0.001f,
              "the menu rests in the lower tier under its quality floor");
    E50Device lightLevel = e50Device(1.072, 2.0, 10.0, 5.0);
    lightLevel.fifo = true;
    heavyFirst.EnterScene("level", 768);
    const E50Run lightRun = e50Drive(heavyFirst, lightLevel, E50Drive{}, rng);
    e50PrintRest("case 6, FIFO light level after it", heavyFirst, lightRun);
    CHECK_MSG(e50InTier(heavyFirst, 58.0f), "the light level holds 58 at its quality floor, and is there");
    CHECK(heavyFirst.Scale() >= heavyFirst.QualityScale() - 0.001f);
}

// 7. A GPU whose prediction misleads (a large fixed part: the lower tier at full size predicts 58 at the quality floor, which
// then runs at 46-53) with its cost swinging 8% over a minute: each probe fails, the next waits 60 s, and after the second
// failure that scene is not probed again. (The plan's "60, 120 and 240 s" assumed no cap on failures: two is the cap.)
constexpr double kTierHoldSlackSeconds = 10.0;

static void testE50AFailedProbeIsHeldOffAndTheSecondEndsTheProbing() {
    E50Lcg rng{7u};
    DynamicResolution controller(e50Config(1.0f, 800));
    controller.EnterScene("level", 768);
    E50Device misleading = e50Device(1.072, 15.0, 11.19, 5.0);
    misleading.thermal = 0.08;
    E50Drive quarterHour;
    quarterHour.seconds = 900.0;
    quarterHour.restFrom = 900.0;
    const E50Run run = e50Drive(controller, misleading, quarterHour, rng);
    e50PrintRest("case 7, thermal, failed probes", controller, run);
    CHECK_MSG(run.tierChanges <= 1 + 2 * DynamicResolution::kMaxProbeFailures, "a down and two probes that failed, at most");
    CHECK_EQ(run.probes, 2);
    CHECK_EQ(run.probeFailures, 2);
    CHECK_EQ(controller.ProbeFailures(), 2);
    CHECK(e50InTier(controller, 30.0f));
    if (run.failTimes.size() == 2 && run.probeTimes.size() == 2 && run.holdsAtFailure.size() == 2) {
        CHECK_NEAR(static_cast<float>(run.holdsAtFailure[0]), 60.0f);
        CHECK_NEAR(static_cast<float>(run.holdsAtFailure[1]), 120.0f);
        const double gap = run.probeTimes[1] - run.failTimes[0];
        CHECK_MSG(gap >= DynamicResolution::kFirstTierHoldSeconds - 1.0 &&
                      gap <= DynamicResolution::kFirstTierHoldSeconds + DynamicResolution::kTierUpAfterSeconds + kTierHoldSlackSeconds,
                  "the second probe came " + std::to_string(gap) + " s after the first failed");
    }
}

// 8. Each scene keeps its own tier and size: a level after a heavy menu is not held at the menu's size, the menu comes back at
// its own at once with nothing to fall through, a scene's other count of lines moves only its quality floor, a window of
// another height forgets every scene, and a pinned picture never moves.
static void testE50EachSceneKeepsItsOwnTierAndSize() {
    E50Lcg rng{8u};
    const E50Device menu = e50Device(1.152, 2.0, 82.0, 10.0);
    const E50Device level = e50Device(1.152, 2.0, 24.0, 10.0);
    DynamicResolution controller(e50Config(1.0f, 720));
    controller.EnterScene("menu", 768);
    E50Drive twoMinutes;
    twoMinutes.seconds = 120.0;
    e50Drive(controller, menu, twoMinutes, rng);
    const float menuScale = controller.Scale();
    const float menuTier = controller.TierFps();
    controller.EnterScene("level", 439);
    CHECK_MSG(!controller.LastSceneRemembered(), "a first visit");
    e50Drive(controller, level, twoMinutes, rng);
    const float levelScale = controller.Scale();
    CHECK_MSG(levelScale > menuScale + 0.03f, "the level is not held at the heavy menu's size");

    const bool moved = controller.EnterScene("menu", 768);
    CHECK(controller.LastSceneRemembered());
    CHECK_NEAR(controller.Scale(), menuScale);
    CHECK_NEAR(controller.TierFps(), menuTier);
    CHECK(moved);
    E50Drive minute;
    minute.seconds = 60.0;
    minute.restFrom = 0.0;
    const E50Run back = e50Drive(controller, menu, minute, rng);
    CHECK_MSG(back.resizes == 0 && back.tierChanges == 0, "back at the menu's own size and tier: nothing to fall through");

    // Another count of lines in the same scene: the quality floor moves, and nothing else (a level zoomed out to 768 lines asks
    // for 540 of them, 0.75 of this window; the upper tier lifts its picture to it).
    controller.EnterScene("level", 439);
    CHECK(controller.LastSceneRemembered());
    CHECK(e50InTier(controller, 58.0f));
    CHECK_NEAR(controller.QualityScale(), 0.62f);
    // (The re-entry's settle first: frames that it counts and does not measure.)
    for (int i = 0; i < 1000 && controller.SceneSettling(); ++i) controller.ObserveFrame(0.016f, 0.0f);
    const double holdBeforeLines = controller.TierHoldSeconds();
    const int failuresBeforeLines = controller.ProbeFailures();
    const std::uint64_t windowsBeforeLines = controller.WindowsEnded();
    controller.EnterScene("level", 768);
    CHECK_NEAR(controller.QualityScale(), 0.75f);
    CHECK(e50InTier(controller, 58.0f));
    CHECK_MSG(controller.TierHoldSeconds() == holdBeforeLines && controller.ProbeFailures() == failuresBeforeLines &&
                  controller.WindowsEnded() == windowsBeforeLines && !controller.SceneSettling(),
              "the same scene's other lines are no visit: no hold, failure or window touched, no settle");
    CHECK(controller.Scale() >= 0.75f - 0.001f);

    // A window of another height: every scene is forgotten, and the floor is the new window's.
    controller.Reconfigure(e50Config(1.0f, 1080));
    CHECK_NEAR(controller.QualityScale(), 0.5f);
    controller.EnterScene("menu", 768);
    CHECK_MSG(!controller.LastSceneRemembered(), "forgotten with the old window");

    // A pinned picture: no tiers, no memory, no move.
    DynamicResolution pinned(DynamicResolution::Pinned(e50Config(1.0f, 720), 0.6f));
    CHECK(!pinned.Tiered());
    pinned.EnterScene("menu", 768);
    CHECK(!pinned.EnterScene("level", 439));
    const E50Run held = e50Drive(pinned, menu, minute, rng);
    CHECK(!pinned.EnterScene("menu", 768));
    CHECK_EQ(held.resizes, 0);
    CHECK_NEAR(pinned.Scale(), 0.6f);
}

// 9. The floors: the upper tier's is the quality floor (AtTheFloor there means "at Q"); the lower tier's first fall stops at the
// 360-line floor before the emergency one (as E47's does); a pin has no tiers; and after a switch down the controller is still
// tiered and a probe can fire.
static void testE50TheUpperTiersFloorIsTheQualityFloor() {
    E50Lcg rng{9u};
    DynamicResolution controller(e50Config(1.0f, 800));
    controller.EnterScene("menu", 768);
    const E50Device heavy = e50Device(1.072, 2.0, 288.0, 10.0);
    E50Drive threeSeconds;
    threeSeconds.seconds = 3.0;
    e50Drive(controller, heavy, threeSeconds, rng);
    CHECK(e50InTier(controller, 58.0f));
    CHECK_NEAR(controller.Scale(), controller.QualityScale());
    CHECK_MSG(controller.AtTheFloor(), "at Q is at the floor of the upper tier");

    bool down = false;
    float firstFall = 0.0f;
    double t = 0.0;
    for (int i = 0; i < 20000 && firstFall <= 0.0f; ++i) {
        const E50Frame frame = e50Frame(heavy, controller.Scale(), t, rng);
        t += frame.frameSeconds;
        const bool changed = controller.ObserveFrame(frame.frameSeconds, frame.waitSeconds);
        const DynamicResolution::TierChange change = controller.TakeTierChange();
        if (changed && down && change == DynamicResolution::TierChange::None) firstFall = controller.Scale();
        if (change == DynamicResolution::TierChange::Down) down = true;
    }
    CHECK(down);
    CHECK_NEAR(firstFall, 0.45f);
    CHECK_MSG(!controller.AtTheFloor(), "the 360-line floor is not the floor the picture cannot go below");
    CHECK_NEAR(DynamicResolution::Pinned(e50Config(1.0f, 800), 0.7f).lowTierFps, 0.0f);

    CHECK_MSG(controller.Tiered(), "still tiered after a switch down");
    controller.EnterScene("light", 768);
    E50Drive minute;
    minute.seconds = 60.0;
    const E50Run light = e50Drive(controller, e50Device(1.072, 2.0, 10.0, 5.0), minute, rng);
    CHECK_MSG(light.probes >= 1 && e50InTier(controller, 58.0f), "and a light scene probes back up");
}

// 13. Prefer 60: the A05's level, entered from its menu in the lower tier, is back in the upper tier at its quality floor or
// above within seconds, in at most two resizes, and reaches its rest soon after. A CPU-bound copy of the same run never
// probes (its frame's work is the CPU's).
constexpr double kPrefer60WithinSeconds = 10.5;     // the prediction's probe at about 9.5 s (the direct one's about 12)
constexpr int kPrefer60MaxResizes = 2;
constexpr double kFirstVisitRestSeconds = 15.0;

static void testE50ALevelThatCanHoldSixtyGoesBackUpSoon() {
    for (const double cpuMs : {10.0, 22.0}) {
        E50Lcg rng{13u};
        DynamicResolution controller(e50Config(1.0f, 720));
        controller.EnterScene("menu", 768);
        E50Drive twoMinutes;
        twoMinutes.seconds = 120.0;
        e50Drive(controller, e50Device(1.152, 2.0, 82.0, cpuMs), twoMinutes, rng);
        CHECK(e50InTier(controller, 30.0f));
        controller.EnterScene("level", 439);
        const E50Run run = e50Drive(controller, e50Device(1.152, 2.0, 24.0, cpuMs), E50Drive{}, rng);
        e50PrintRest("case 13, A05 level after its menu, CPU " + std::to_string(static_cast<int>(cpuMs)) + " ms", controller, run);
        if (cpuMs < 20.0) {
            CHECK_MSG(!run.probeTimes.empty() && run.probeTimes.front() <= kPrefer60WithinSeconds, "a probe within seconds");
            // The prediction's probe, not the direct one's fallback: with the prediction holding, every window keeps the
            // lower tier from rising, so the first resize in the level is the probe; without it, a quicker first-visit rise
            // to the quality floor would come first (at about 3.5 s) and the direct probe at about 12 s.
            CHECK_MSG(!run.resizeTimes.empty() && !run.probeTimes.empty() && run.resizeTimes.front() == run.probeTimes.front(),
                      "the first resize in the level is the probe");
            CHECK_EQ(run.probes, 1);
            CHECK(e50InTier(controller, 58.0f));
            CHECK(controller.Scale() >= controller.QualityScale() - 0.001f);
            CHECK(run.ResizesWithin(kPrefer60WithinSeconds) <= kPrefer60MaxResizes);
            CHECK_MSG(!run.resizeTimes.empty() && run.resizeTimes.back() <= kFirstVisitRestSeconds, "case 8: at rest within 15 s");
        } else {
            CHECK_EQ(run.probes, 0);
            CHECK(e50InTier(controller, 30.0f));
        }
    }
}

// A burst of hitches (five 150 ms frames every 20 s in the middle of a scene) moves neither the size nor the tier: every
// decision is a window's median, not its mean. The same run without the hitches is the control.
static void testE50AHitchMovesNeitherTheSizeNorTheTier() {
    struct Case {
        const char* name;
        std::uint32_t height;
        E50Device first;
        std::uint32_t firstLines;
        E50Device second;
        std::uint32_t secondLines;
    };
    const Case cases[] = {
        {"GE8320 menu (lower tier)", 800u, e50Device(1.072, 2.0, 162.0, 10.0), 768u, e50Device(1.072, 2.0, 162.0, 10.0), 768u},
        {"A05 level (upper tier)", 720u, e50Device(1.152, 2.0, 82.0, 10.0), 768u, e50Device(1.152, 2.0, 24.0, 10.0), 439u},
    };
    for (const Case& c : cases) {
        DynamicResolution withHitches(e50Config(1.0f, c.height));
        DynamicResolution without(e50Config(1.0f, c.height));
        E50Lcg rngA{14u};
        E50Lcg rngB{14u};
        E50Drive minute;
        minute.seconds = 60.0;
        for (DynamicResolution* controller : {&withHitches, &without}) {
            E50Lcg& rng = controller == &withHitches ? rngA : rngB;
            controller->EnterScene("first", c.firstLines);
            e50Drive(*controller, c.first, minute, rng);
            controller->EnterScene("second", c.secondLines);
            e50Drive(*controller, c.second, minute, rng);
        }
        // Five frames of 150 ms in a row, each almost all of it a wait for the GPU: however a burst falls across two windows,
        // one of them holds three or more, which would put a window's mean (and a mean of its waits) across a line in both
        // cases (the GE8320's 32 fps to about 23, under 28.2; the A05's 69 to about 46, under 54.5). The median does not move.
        E50Drive hitches;
        hitches.seconds = 120.0;
        hitches.restFrom = 0.0;
        hitches.hitchEverySeconds = 20.0;
        hitches.hitchFrames = 5;
        hitches.hitchWaitSeconds = 0.14f;
        E50Drive plain = hitches;
        plain.hitchEverySeconds = 0.0;
        const E50Run hit = e50Drive(withHitches, c.second, hitches, rngA);
        const E50Run calm = e50Drive(without, c.second, plain, rngB);
        e50PrintRest(std::string("hitches, ") + c.name, withHitches, hit);
        CHECK_MSG(calm.resizes == 0 && calm.tierChanges == 0, std::string(c.name) + ": at rest without the hitches");
        CHECK_MSG(hit.resizes == 0 && hit.tierChanges == 0, std::string(c.name) + ": and with them");
    }
}

// A failed probe's hold belongs to its scene for the session: re-entering the scene five times (another between each) neither
// re-arms the probe nor runs the hold down while the other scene is current; and after the second failure the scene is never
// probed again, reloads included.
static void testE50AFailedProbesHoldSurvivesTheScenesReloads() {
    E50Lcg rng{15u};
    DynamicResolution controller(e50Config(1.0f, 800));
    controller.EnterScene("level", 768);
    const E50Device level = e50Device(1.072, 15.0, 11.19, 5.0);   // case 7's GPU without the heat: every probe fails
    const E50Device menu = e50Device(1.072, 2.0, 162.0, 10.0);    // the GE8320 menu: the lower tier, nothing to probe
    const E50Drive second = e50Stretch(1.0, 1.0);
    for (int i = 0; i < 400 && controller.ProbeFailures() == 0; ++i) e50Drive(controller, level, second, rng);
    CHECK_EQ(controller.ProbeFailures(), 1);
    CHECK(controller.TierHoldSeconds() > 55.0);

    // The menu's first visit is long enough for windows to end there (its 30-frame settle alone takes about 5 s at the
    // level's size), so its own tier rules run while the level's hold waits; later visits start at the menu's own smaller
    // size, where windows end within seconds.
    int probesDuringVisits = 0;
    int heldThroughTheMenu = 0;
    int menuStartedEmpty = 0;
    int menuVisitsWithWindows = 0;
    const E50Drive firstMenuVisit = e50Stretch(20.0, 20.0);
    const E50Drive menuVisit = e50Stretch(5.0, 5.0);
    const E50Drive levelVisit = e50Stretch(12.0, 12.0);   // without the hold, a probe would come at about 9.5 s of each
    for (int visit = 0; visit < 5; ++visit) {
        const double holdBefore = controller.TierHoldSeconds();
        controller.EnterScene("menu", 768);
        // The menu's own holds, not the level's: a hold kept for the controller, not per scene, would show 60 s and a failure.
        if (controller.TierHoldSeconds() == 0.0 && controller.ProbeFailures() == 0) ++menuStartedEmpty;
        const std::uint64_t windowsBefore = controller.WindowsEnded();
        e50Drive(controller, menu, visit == 0 ? firstMenuVisit : menuVisit, rng);
        if (controller.WindowsEnded() > windowsBefore) ++menuVisitsWithWindows;
        controller.EnterScene("level", 768);
        if (controller.LastSceneRemembered() && controller.ProbeFailures() == 1 && e50InTier(controller, 30.0f) &&
            std::fabs(controller.TierHoldSeconds() - holdBefore) < 1e-9) {
            ++heldThroughTheMenu;
        }
        probesDuringVisits += e50Drive(controller, level, levelVisit, rng).probes;
    }
    CHECK_EQ(menuStartedEmpty, 5);
    CHECK_MSG(menuVisitsWithWindows >= 1, "the menu's own windows ran while the level's hold waited");
    CHECK_EQ(heldThroughTheMenu, 5);
    CHECK_EQ(probesDuringVisits, 0);

    for (int i = 0; i < 400 && controller.ProbeFailures() < 2; ++i) e50Drive(controller, level, second, rng);
    CHECK_EQ(controller.ProbeFailures(), 2);
    E50Drive fiveMinutes;
    CHECK_EQ(e50Drive(controller, level, fiveMinutes, rng).probes, 0);
    controller.EnterScene("menu", 768);
    CHECK_MSG(controller.TierHoldSeconds() == 0.0 && controller.ProbeFailures() == 0, "the menu's own holds again");
    e50Drive(controller, menu, menuVisit, rng);
    controller.EnterScene("level", 768);
    CHECK_EQ(controller.ProbeFailures(), 2);
    E50Drive twoMinutes;
    twoMinutes.seconds = 120.0;
    CHECK_EQ(e50Drive(controller, level, twoMinutes, rng).probes, 0);
}

// The load frame (taken with the scene's note, the frame after the tick that loaded it) is not the scene's speed: it counts as
// one frame and a tenth of a second towards a settle of 30 frames AND a second, and the first window after the settle is a
// whole window of the scene's own frames.
static void testE50ALoadFrameIsNotTheScenesSpeedAndTheSettleOutlastsIt() {
    DynamicResolution controller(e50Config(1.0f, 720));
    controller.EnterScene("menu", 768);
    CHECK_MSG(!controller.SceneSettling(), "the first scene is adopted with no settle");
    controller.EnterScene("level", 439);
    CHECK(controller.SceneSettling());
    const std::uint64_t windows = controller.WindowsEnded();
    CHECK(!controller.ObserveFrame(0.6f, -1.0f));
    int quick = 0;
    while (controller.SceneSettling() && quick < 1000) {
        controller.ObserveFrame(0.016f, 0.0f);
        ++quick;
    }
    CHECK_EQ(quick, 57);   // (1.0 - 0.1) / 0.016 = 56.25
    CHECK(controller.WindowsEnded() == windows);
    int frames = 0;
    while (controller.WindowsEnded() == windows && frames < 1000) {
        controller.ObserveFrame(0.016f, 0.0f);
        ++frames;
    }
    CHECK_MSG(frames >= 75, "a whole window: " + std::to_string(frames) + " frames");
    CHECK_NEAR(controller.WindowMedianMilliseconds(), 16.0f);

    // Slow frames: 30 of them, whatever a second says.
    controller.EnterScene("menu", 768);
    CHECK(!controller.ObserveFrame(0.6f, -1.0f));
    int slow = 0;
    while (controller.SceneSettling() && slow < 1000) {
        controller.ObserveFrame(0.1f, 0.0f);
        ++slow;
    }
    CHECK_EQ(slow, 29);
}

// A resume settles the controller as a scene's entry does and grants the same grace, with no tier change and nothing forgotten.
// (That the engine feeds a tiered controller no frame while the window has no focus is SupersonicApp's, not testable here.)
static void testE50AResumeSettlesAndForgetsNothing() {
    E50Lcg rng{16u};
    DynamicResolution controller(e50Config(1.0f, 720));
    controller.EnterScene("menu", 768);
    E50Drive minute;
    minute.seconds = 60.0;
    e50Drive(controller, e50Device(1.152, 2.0, 82.0, 10.0), minute, rng);
    controller.EnterScene("level", 439);
    e50Drive(controller, e50Device(1.152, 2.0, 24.0, 10.0), minute, rng);
    const float scale = controller.Scale();
    const float tier = controller.TierFps();
    controller.NoteResume();
    CHECK(controller.SceneSettling());
    CHECK_NEAR(static_cast<float>(controller.TierGraceSeconds()), static_cast<float>(DynamicResolution::kTierGraceSeconds));
    CHECK_NEAR(controller.Scale(), scale);
    CHECK_NEAR(controller.TierFps(), tier);
    CHECK(controller.TakeTierChange() == DynamicResolution::TierChange::None);
    int quick = 0;
    while (controller.SceneSettling() && quick < 1000) {
        controller.ObserveFrame(0.016f, 0.0f);
        ++quick;
    }
    CHECK_EQ(quick, 63);   // 1.0 / 0.016 = 62.5
    controller.EnterScene("menu", 768);
    CHECK_MSG(controller.LastSceneRemembered() && e50InTier(controller, 30.0f), "nothing forgotten");

    DynamicResolution e47(e50Config(1.0f, 720, false));
    e47.NoteResume();
    CHECK_MSG(!e47.SceneSettling(), "E47's controller has no such settle");
}

// Frames at a steady `milliseconds`, each with the given wait and game parts, until `seconds` have passed.
static void feedFrames(FramePerfWindow& window, double seconds, float milliseconds, float waitMs, float gameMs) {
    FramePerfSample sample;
    sample.frameSeconds = milliseconds / 1000.0f;
    sample.partMs[static_cast<std::size_t>(PerfPart::Wait)] = waitMs;
    sample.partMs[static_cast<std::size_t>(PerfPart::Game)] = gameMs;
    sample.ticks = 1;
    for (double t = 0.0; t < seconds; t += sample.frameSeconds) window.Add(sample);
}

static void testFramePerfSummarisesAWindowOfFrames() {
    FramePerfWindow window;
    CHECK(!window.Ready(1.0));
    feedFrames(window, 1.0, 16.7f, 2.0f, 5.0f);
    CHECK_MSG(window.Ready(1.0) && !window.Ready(5.0), "a window is ready when its seconds have been gathered");
    const int steadyFrames = window.Frames();

    // One 200 ms hitch among them: the median ignores it, the maximum and the counts show it.
    FramePerfSample hitch;
    hitch.frameSeconds = 0.2f;
    window.Add(hitch);
    window.Add(FramePerfSample{});   // a frame with no time is not a measurement
    const FramePerfSummary summary = window.Take();
    CHECK_EQ(summary.frames, steadyFrames + 1);
    CHECK_MSG(std::fabs(summary.medianMs - 16.7f) < 0.05f, "the median is the steady frame");
    CHECK_MSG(std::fabs(summary.p95Ms - 16.7f) < 0.05f && std::fabs(summary.maxMs - 200.0f) < 0.05f, "the hitch is in the max only");
    CHECK_EQ(summary.over33, 1);
    CHECK_EQ(summary.over100, 1);
    CHECK_MSG(summary.fps > 50.0f && summary.fps < 60.0f, "the rate is frames over seconds");
    CHECK_MSG(window.Frames() == 0 && window.Seconds() == 0.0, "taking a window starts the next");
    CHECK_NEAR(summary.meanPartMs[static_cast<std::size_t>(PerfPart::Wait)],
               2.0f * static_cast<float>(steadyFrames) / static_cast<float>(steadyFrames + 1));

    // The text: one log line short enough for a viewer that cuts at 170 characters, a readout of three short lines.
    const std::string line = FormatPerfLine(summary, "scale 0.50 800x360 of 1600x720, 4x, Mailbox");
    CHECK_MSG(line.rfind("Perf: ", 0) == 0 && line.find(" fps, frame 16.7 ms") != std::string::npos, "the line leads with the rate");
    CHECK_MSG(line.size() <= 200, "and stays short");
    const std::vector<std::string> readout = FormatPerfReadout(summary);
    CHECK_EQ(readout.size(), 3u);
    for (const std::string& text : readout) CHECK_MSG(text.size() <= 56, "a readout line fits the corner of a phone");
}

static void testPerfVerdictSaysWhereTheFrameWaited() {
    const auto verdictOf = [](float waitMs, float gameMs, float drainMs) {
        FramePerfWindow window;
        FramePerfSample sample;
        sample.frameSeconds = 0.020f;
        sample.partMs[static_cast<std::size_t>(PerfPart::Wait)] = waitMs;
        sample.partMs[static_cast<std::size_t>(PerfPart::Game)] = gameMs;
        sample.partMs[static_cast<std::size_t>(PerfPart::Drain)] = drainMs;
        for (int i = 0; i < 20; ++i) window.Add(sample);
        return PerfVerdict(window.Take());
    };
    CHECK(verdictOf(15.0f, 3.0f, 0.0f).find("GPU or display bound") == 0);
    CHECK(verdictOf(1.0f, 17.0f, 0.0f).find("CPU bound") == 0);
    CHECK(verdictOf(7.0f, 11.0f, 0.0f).find("mixed") == 0);
    CHECK_MSG(verdictOf(1.0f, 17.0f, 8.0f).find("stalls") == 0, "idle waits are their own verdict");
    // An idle wait is the CPU waiting for the GPU as well: a frame whose wait is mostly idle waits is not CPU bound.
    CHECK_MSG(verdictOf(1.0f, 17.0f, 4.0f).find("CPU bound") == std::string::npos, "and count as waiting");
    CHECK(PerfVerdict(FramePerfSummary{}) == "no frames yet");
}

static void testThePerfOverlayKeepsAReportOfEachSizeMeasured() {
    PerfOverlay::Environment environment;
    environment.device = "Test phone";
    environment.gpu = "Test GPU, Vulkan 1.1.0";
    environment.presentMode = "Fifo";
    environment.scene = "scenes/level1.esc";
    environment.windowWidth = 1600;
    environment.windowHeight = 720;
    environment.sceneWidth = 800;
    environment.sceneHeight = 360;
    environment.samples = 4;
    environment.scale = 0.5f;
    environment.hasController = true;

    // Off, nothing runs.
    PerfOverlay quiet{PerfOverlay::Settings{}};
    CHECK(!quiet.Active() && !quiet.Drawing());
    FramePerfSample sample;
    sample.frameSeconds = 0.020f;
    quiet.Observe(sample, environment);
    CHECK_MSG(quiet.BuildReport(environment).find("none yet") != std::string::npos, "an idle overlay measured nothing");

    PerfOverlay::Settings settings;
    settings.logSeconds = 3.0f;
    PerfOverlay overlay(settings);
    CHECK(overlay.Active() && !overlay.Drawing());
    sample.partMs[static_cast<std::size_t>(PerfPart::Wait)] = 14.0f;
    sample.partMs[static_cast<std::size_t>(PerfPart::Game)] = 4.0f;
    for (double t = 0.0; t < 4.0; t += sample.frameSeconds) overlay.Observe(sample, environment);
    CHECK_MSG(overlay.BuildReport(environment).find("none yet") != std::string::npos, "a size is measured after it has settled");
    for (double t = 0.0; t < 6.0; t += sample.frameSeconds) overlay.Observe(sample, environment);
    std::string report = overlay.BuildReport(environment);
    CHECK_MSG(report.find("Auto (scale 0.50): 50.0 fps") != std::string::npos, "the size in force is in the report");
    CHECK(report.find("Test phone") != std::string::npos && report.find("Test GPU, Vulkan 1.1.0") != std::string::npos);
    CHECK(report.find("scenes/level1.esc") != std::string::npos && report.find("4x MSAA") != std::string::npos);
    CHECK_MSG(report.find("GPU or display bound") != std::string::npos, "and what limited it");
    CHECK_MSG(report.find("Input:") == std::string::npos, "no input line where the platform has no touch layer to ask");
    CHECK_MSG(report.find("Perf: ") != std::string::npos, "with the lines it logged");

    // Where it has one, the touch layer's state is in the report and in the log lines, and a lost focus is shouted.
    environment.hasInput = true;
    environment.touches = 2;
    environment.secondsSinceTouch = 3.25;
    environment.focused = false;
    report = overlay.BuildReport(environment);
    CHECK_MSG(report.find("Input: touch 2 fingers, last event 3.2 s ago, focus NO, resumed yes") != std::string::npos
                  || report.find("Input: touch 2 fingers, last event 3.3 s ago, focus NO, resumed yes") != std::string::npos,
              "the report says what the touch layer knows");
    environment.hasInput = false;

    // Switching to another size keeps what the others measured, starts the new one empty and settles again.
    overlay.SetActivePreset(2);
    environment.scale = 0.6f;
    report = overlay.BuildReport(environment);
    CHECK_MSG(report.find("Auto (scale 0.50)") != std::string::npos, "the sizes already measured stay in the report");
    CHECK_MSG(report.find("0.6 (scale") == std::string::npos && report.find("Size in force: 0.6") != std::string::npos,
              "the new size starts empty");
    CHECK_EQ(overlay.TakePresetRequest(), -1);

    CHECK_MSG(PerfOverlay::PresetScale(0) < 0.0f && PerfOverlay::PresetScale(1) == 1.0f && PerfOverlay::PresetScale(4) == 0.3f,
              "Auto is no fixed scale, the others are");
    CHECK(std::string(PerfOverlay::PresetName(0)) == "Auto" && std::string(PerfOverlay::PresetName(4)) == "0.3");
}

static void testTheProfilerCountsAndTimesQueueDrains() {
    Profiler::BeginFrame();
    CHECK_EQ(Profiler::QueueDrains(), 0);
    { Profiler::DrainScope first; }
    { Profiler::DrainScope second; }
    CHECK_EQ(Profiler::QueueDrains(), 2);
    CHECK(Profiler::Milliseconds(ProfileZone::QueueDrain) >= 0.0);
    CHECK(std::string(Profiler::Name(ProfileZone::QueueDrain)) == "Queue Drain");
    Profiler::BeginFrame();
    CHECK_MSG(Profiler::QueueDrains() == 0 && Profiler::Milliseconds(ProfileZone::QueueDrain) == 0.0, "a frame starts at zero");
}

static void testTheSceneSampleCapIsOffByDefaultAndClampedToOneTwoOrFour() {
    GameManifest manifest;
    CHECK_EQ(manifest.sceneSamples, 0u);
    CHECK_MSG(GameRuntime::ResolveSceneSamples(manifest) == 4u, "off, the engine takes the device's best up to 4 as ever");
    const struct { uint32_t asked, got; } table[] = {{1u, 1u}, {2u, 2u}, {3u, 2u}, {4u, 4u}, {8u, 4u}, {4000000000u, 4u}};
    for (const auto& row : table) {
        manifest.sceneSamples = row.asked;
        CHECK_EQ(GameRuntime::ResolveSceneSamples(manifest), row.got);
    }
    // Never in the manifest's text.
    GameManifest asking;
    asking.isGame = true;
    asking.sceneSamples = 1;
    GameManifest quiet;
    quiet.isGame = true;
    CHECK_MSG(GameRuntime::Serialize(asking) == GameRuntime::Serialize(quiet), "the manifest's text carries no sample cap");
    CHECK_EQ(GameRuntime::Parse(GameRuntime::Serialize(asking)).sceneSamples, 0u);
}

static void testTheShadowMapResolutionIsOffByDefaultClampedAndNeverInTheManifestText() {
    GameManifest manifest;
    CHECK_EQ(manifest.shadowMapResolution, 0u);
    CHECK_MSG(GameRuntime::ResolveShadowMapResolution(manifest) == 0u, "off, the renderer keeps its default sizes");
    const struct { uint32_t asked, got; } table[] = {{1u, 16u}, {16u, 16u}, {64u, 64u}, {4096u, 4096u}, {8192u, 4096u}, {4000000000u, 4096u}};
    for (const auto& row : table) {
        manifest.shadowMapResolution = row.asked;
        CHECK_EQ(GameRuntime::ResolveShadowMapResolution(manifest), row.got);
    }
    GameManifest asking;
    asking.isGame = true;
    asking.shadowMapResolution = 16;
    GameManifest quiet;
    quiet.isGame = true;
    CHECK_MSG(GameRuntime::Serialize(asking) == GameRuntime::Serialize(quiet), "the manifest's text carries no shadow resolution");
    CHECK_EQ(GameRuntime::Parse(GameRuntime::Serialize(asking)).shadowMapResolution, 0u);
}

static void testThePerfAndWatcherFieldsAreOffByDefaultAndNeverInTheManifestText() {
    GameManifest plain;
    CHECK_MSG(!plain.perfOverlay && plain.perfLogSeconds == 0.0f && plain.assetWatching, "a game that never heard of them is unchanged");
    GameManifest asking;
    asking.isGame = true;
    asking.perfOverlay = true;
    asking.perfLogSeconds = 5.0f;
    asking.assetWatching = false;
    GameManifest quiet;
    quiet.isGame = true;
    CHECK_MSG(GameRuntime::Serialize(asking) == GameRuntime::Serialize(quiet), "the manifest's text carries none of them");
    const GameManifest reparsed = GameRuntime::Parse(GameRuntime::Serialize(asking));
    CHECK_MSG(!reparsed.perfOverlay && reparsed.perfLogSeconds == 0.0f && reparsed.assetWatching, "nor reads them back");
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

// WHERE THE SCREEN OVERLAY IS DRAWN (ScreenOverlay::Placement): into the picture by default, as it always was, and never
// in the manifest's text. And the perf log's size line names the window path only on a frame that took it.
static void testTheOverlayPlacementIsThePictureByDefaultAndNeverInTheManifestsText() {
    const GameManifest plain;
    CHECK_MSG(plain.screenOverlayPlacement == ScreenOverlay::Placement::Picture, "a game that never heard of it is unchanged");
    GameManifest asking;
    asking.isGame = true;
    asking.screenOverlayPlacement = ScreenOverlay::Placement::WindowWhenScaled;
    GameManifest quiet;
    quiet.isGame = true;
    CHECK_MSG(GameRuntime::Serialize(asking) == GameRuntime::Serialize(quiet), "the manifest's text carries no placement");
    CHECK_MSG(GameRuntime::Parse(GameRuntime::Serialize(asking)).screenOverlayPlacement == ScreenOverlay::Placement::Picture,
              "nor reads one back");
    asking.screenOverlayPlacement = ScreenOverlay::Placement::Window;
    CHECK_MSG(GameRuntime::Serialize(asking) == GameRuntime::Serialize(quiet), "whichever it is");

    PerfOverlay::Environment environment;
    environment.presentMode = "Mailbox";
    environment.windowWidth = 1340;
    environment.windowHeight = 800;
    environment.sceneWidth = 402;
    environment.sceneHeight = 240;
    environment.scale = 0.3f;
    PerfOverlay::Settings settings;
    settings.logSeconds = 1.0f;
    PerfOverlay overlay(settings);
    FramePerfSample sample;
    sample.frameSeconds = 0.020f;
    for (double t = 0.0; t < 1.5; t += sample.frameSeconds) overlay.Observe(sample, environment);
    const std::string before = overlay.BuildReport(environment);
    CHECK_MSG(before.find("Perf: ") != std::string::npos && before.find("hud window") == std::string::npos,
              "the size line as it always was while the overlay is in the picture");
    environment.overlayAtWindow = true;
    for (double t = 0.0; t < 1.5; t += sample.frameSeconds) overlay.Observe(sample, environment);
    CHECK_MSG(overlay.BuildReport(environment).find("Mailbox, hud window") != std::string::npos,
              "and it ends with the window path once the overlay is drawn there");
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
    testTheOptInsOfE47AreOffByDefault();
    testARiseNeedsNo110FpsWhenTheGameAsksAndTheLoopIsNotPaced();
    testARiseThatDoesNotHoldWaitsBeforeTheNextOne();
    testThePixelFloorIsTheScaleThatKeepsTheTargetTallEnough();
    testTheFloorGivesWayOnlyWhenTheGameWouldBeUnplayablySlow();
    testARiseHoldDoublesWithEachFailureAndIsCapped();
    testARiseThatHeldIsNotHeldAgainstTheNextFailureAndReconfigureForgets();
    testTheRiseMarginSetsWhereTheWayUpOpens();
    testAFallIsMeasuredAtTheComfortableFloorFirst();
    testTheEmergencyFloorIsReachedFromJustAboveTheFloor();
    testAFloorThatRoseLiftsAPictureThatHadFallenBelowIt();
    testAPinnedSizeHoldsWhateverTheGpuDoes();
    testTheDeclaredConfigIsWorkedOutForEachWindowHeight();
    testTheRenderScaleControlLatchesTheLastRequestAndDropsOneWithNothingToAsk();
    testE50WithNoTiersAndNoSceneIsE47FrameForFrame();
    testE50TheQualityFloorForEachWindowAndScene();
    testE50TheSceneNoteIsLatchedLikeARequest();
    testE50ARequestForACeilingIsLatchedAndClampedAsAFixedOneIs();
    testE50WhetherAPickIsAlreadyAppliedInEveryState();
    testE50EachPickIsAConfigurationForItsWindow();
    testE50ACeilingFallsWhereAPinIsHeldAndRisesNoHigherThanItself();
    testE50ACeilingAtItsFloorRemembersEachScene();
    testE50ACeilingIsNeverSlowerThanAutomaticAndNeverExceeded();
    testE50ASlowTabletKeepsThirtyAtTheLargestSizeThatHoldsIt();
    testE50APhoneKeepsThirtyInItsMenuAndSixtyInItsLevels();
    testE50AFastTabletStaysInTheUpperTierThroughItsSlowStarts();
    testE50ACpuBoundPhoneKeepsItsPictureAndNeverProbes();
    testE50APacedLoopChangesTierOnlyWhenItMust();
    testE50AFailedProbeIsHeldOffAndTheSecondEndsTheProbing();
    testE50EachSceneKeepsItsOwnTierAndSize();
    testE50TheUpperTiersFloorIsTheQualityFloor();
    testE50ALevelThatCanHoldSixtyGoesBackUpSoon();
    testE50AHitchMovesNeitherTheSizeNorTheTier();
    testE50AFailedProbesHoldSurvivesTheScenesReloads();
    testE50ALoadFrameIsNotTheScenesSpeedAndTheSettleOutlastsIt();
    testE50AResumeSettlesAndForgetsNothing();
    testFramePerfSummarisesAWindowOfFrames();
    testPerfVerdictSaysWhereTheFrameWaited();
    testThePerfOverlayKeepsAReportOfEachSizeMeasured();
    testTheProfilerCountsAndTimesQueueDrains();
    testThePerfAndWatcherFieldsAreOffByDefaultAndNeverInTheManifestText();
    testTheSceneSampleCapIsOffByDefaultAndClampedToOneTwoOrFour();
    testTheShadowMapResolutionIsOffByDefaultClampedAndNeverInTheManifestText();
    testTheVulkanMinimumIsOneTwoByDefaultAndClampedIntoOneToTwo();
    testTheOverlayPlacementIsThePictureByDefaultAndNeverInTheManifestsText();
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

TEST_MAIN("test_gameruntime", 404)
