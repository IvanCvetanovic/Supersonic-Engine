#pragma once

#include <cstdint>
#include <string>

namespace Supersonic {

// Tells the engine whether it was launched as the editor or as a shipped game.
//
// "Package Standalone Game" produced a folder whose executable started the
// editor: menu bar, dockspace, inspector, content browser, and the hardcoded
// demo scene rather than the packaged one. The binary is the same either way -
// that is the design, and it is what makes packaging a copy rather than a
// build - so it needs something in the folder to tell it apart.
//
// That something is game.manifest, written by the packager next to the
// executable. Beside the executable rather than in the working directory,
// because a game is usually launched by double-clicking it from somewhere else
// entirely, and because the editor's own build folder must never contain one.
struct GameManifest {
    bool isGame{false};

    // Scene to load at startup, relative to the game folder.
    std::string startupScene{"assets/scenes/MainScene.scene"};

    // Shown in the window title bar.
    std::string title{"Supersonic Game"};

    // The window it opens at, in pixels.
    //
    // A game had no way to say this at all: the size was one hardcoded literal
    // in SupersonicApp, so every game this engine ships opens at 1280x720
    // whatever it was designed for. Wolf Brigade's original is authored at
    // 1920x1080 and its own project file says so.
    //
    // It is the RENDER resolution too, and not incidentally: in game mode the
    // offscreen target is resized to the window every frame, so this is the
    // number the whole scene pass costs at.
    //
    // Zero means "not stated", which is what every manifest written before this
    // says by having no such key - and what the engine's own default then
    // answers. Nothing migrates.
    uint32_t width{0};
    uint32_t height{0};

    // Whether it opens covering the monitor rather than in a window.
    //
    // At the monitor's current mode, so the render resolution is then the
    // monitor's, and width and height are the size it returns to when the
    // player asks for a window - see WindowControl, which is how a game changes
    // it once running. False is what every manifest written before this says
    // by having no such key.
    bool fullscreen{false};

    // Opt-in, for a game whose window should match whatever monitor it opens
    // on: a fraction in (0, 1] opens a windowed start fitted to the monitor
    // (WindowControl::FitWindowToMonitor, that fraction of its work area, the
    // monitor's shape, centred) instead of at width x height at the place the
    // system picks - fitted before the first swapchain, so the window does not
    // appear at one size and jump to another. `--window` wins over it, as it
    // wins over width and height, and a fullscreen start ignores it. Zero, the
    // default, is the window as it always opened. Set by a game's main; not
    // read from or written to game.manifest's text.
    float fitWindowToMonitor{0.0f};

    // What the engine opens at when nothing asks for anything, which is the
    // literal that used to be the only answer.
    static constexpr uint32_t kDefaultWidth = 1280;
    static constexpr uint32_t kDefaultHeight = 720;

    // A window nobody could use is refused rather than clamped silently: zero
    // is "not stated" and belongs to the caller, but a manifest asking for
    // eight pixels or for sixty thousand has a typo in it, and a game that
    // opens 1x1 looks like the engine failing rather than like the mistake it
    // is.
    //
    // The ceiling is deliberately generous - larger than any display sold - so
    // that it catches nonsense rather than ambition.
    static constexpr uint32_t kMinimumExtent = 64;
    static constexpr uint32_t kMaximumExtent = 16384;
};

namespace GameRuntime {

// Reads game.manifest from the executable's directory. Returns a manifest with
// isGame false when there is none, which is the editor case and by far the
// common one.
GameManifest Load();

// Parses manifest text directly. Separate from the file read so the format is
// testable without a packaged folder on disk.
GameManifest Parse(const std::string& text);

// The text the packager writes. Round-trips through Parse.
std::string Serialize(const GameManifest& manifest);

// Name of the marker file, so the packager and the loader cannot disagree.
inline constexpr const char* kManifestFilename = "game.manifest";

// Which of the three possible answers decides the window size.
//
// THE ORDER IS THE POINT: the flag is for one run, the manifest is what the
// game ships as, and the default is what an engine with neither opens at. A
// developer overriding a game's declared size for one measurement is the case
// that stops a resolution experiment being a source edit somebody has to
// remember to revert.
//
// Static and pure, and split out for the reason SortOpaqueDraws is: the only
// caller is SupersonicApp, which no test can construct - it needs a device, a
// window and a swapchain - so a rule left inline there is a rule nothing can
// check. This is the whole of the decision, and it needs none of those things.
//
// Zero from either source means "not stated". Both extents must be stated
// together; half a size is not a size.
void ResolveWindowSize(const GameManifest& manifest, uint32_t optionWidth,
                       uint32_t optionHeight, uint32_t& outWidth, uint32_t& outHeight);

// Whether the window opens fullscreen: `--windowed`, then `--fullscreen`, then
// the manifest.
//
// The same order as the size and for the same reason - the flag is for one
// run, and the one run that most needs it is a headless capture of a game that
// ships fullscreen, which would otherwise cover the desk it is being run on.
// The two flags together are refused by the parser, so their order here only
// decides a case that cannot arrive.
bool ResolveFullscreen(const GameManifest& manifest, bool optionFullscreen, bool optionWindowed);

} // namespace GameRuntime

} // namespace Supersonic
