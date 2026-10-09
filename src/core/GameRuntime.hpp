#pragma once

#include <cstdint>
#include <string>

#include "core/DynamicResolution.hpp"
#include "core/LaunchOptions.hpp"

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

    // Opt-in, for a game whose start takes seconds on a slow machine: the
    // window is shown the moment it is created, and nothing reads its messages
    // until the first frame - so a start of five seconds or more makes Windows
    // call the window "Not responding" and offer to close it, and the player
    // sees a blank window that looks like a crash. True pumps the window's
    // events between the startup stages (the device, the swapchain, each
    // pipeline compiled, the layer's attach), so the longest a window goes
    // unread is the longest single stage. A desktop window only: a phone's is
    // the platform's. False, the default, is the start as it always was. Set by
    // a game's main; not read from or written to game.manifest's text.
    bool pumpEventsDuringStartup{false};

    // The lowest Vulkan 1.x minor version a GPU must report to be used: 2, the
    // default, is Vulkan 1.2 as the engine has always required; 1 accepts a GPU
    // that reports Vulkan 1.1. The engine's own renderer and shaders use nothing
    // newer than Vulkan 1.1 core (and its allocator needs only that), and the
    // stock drivers of most phones on Android 11 and 12 report 1.1: with the
    // default they have no GPU the engine will take, and the start ends before
    // its first frame. Anything else is clamped into 1 to 2 (ResolveVulkanMinor).
    // Set by a game's main; not read from or written to game.manifest's text.
    uint32_t minimumVulkanMinor{2};

    // Every draw this game puts through the scene pipelines is UNLIT - a 2D sprite, or a plain unlit quad such as a particle,
    // a halo or a shadow blob (a draw flagged kUnlit) - so those pipelines are built from the scene fragment shader with
    // "every draw is unlit" folded in (specialisation constant 0): the driver compiles away the physically based surface, the
    // clustered lights, the cascades and the probes, none of which an unlit draw reaches. The result of every such draw is the
    // same bytes; the shader the GPU schedules is much smaller (on an Adreno 640 a lit 2D menu drew in 75 ms instead of 117,
    // and a level in 25 ms instead of 33). False, the default, is the pipelines as they were: a game that draws a lit mesh, a
    // tilemap, a skinned model or an engine particle emitter through them must not set it, for it would come out unlit. Set by
    // a game's main; not read from or written to game.manifest's text.
    bool spritesOnlyScenePipelines{false};

    // The scene target's size chosen by how fast the GPU draws it (DynamicResolution.hpp). Off, the
    // default, is the engine as it was. Never in effect for a capture or a fixed-step run, whose
    // pictures must reproduce. Set by a game's main; not read from or written to game.manifest's text.
    DynamicResolutionConfig dynamicResolution;

    // A readout of the frame rate and of where the frame's time went, over the game's picture, with a button that shares a
    // report and buttons that fix the scene target's scale for a measurement (PerfOverlay.hpp); and a log line of the same
    // numbers every perfLogSeconds (0: none). Off, the default, nothing is measured beyond what always was. For a game whose
    // players run it on hardware its developer does not own. Set by a game's main; not read from or written to
    // game.manifest's text.
    bool perfOverlay{false};
    float perfLogSeconds{0.0f};

    // Whether the engine watches the files a scene names for changes on disk (to reload a texture or a mesh a developer
    // saved): a stat of every watched path every frame, and a hash of every material's paths. True, the default, is the
    // engine as it was. A shipped game whose assets cannot change under it - and a phone game, whose virtual texture keys are
    // not files at all, so every stat fails - sets it false and saves that work on every frame. Set by a game's main; not
    // read from or written to game.manifest's text.
    bool assetWatching{true};

    // The most samples per pixel the scene target may use: 0, the default, is the engine as it was (the device's best, at most
    // 4); 1, 2 and 4 cap it there (ResolveSceneSamples). Multisampling smooths the edges of geometry, which a sprite game of
    // axis-aligned quads and alpha-tested edges has almost none of, while a 4x target of RGBA16F colour and depth is 55 MB at
    // 1600x720 on a tile-based mobile GPU, whose tiles shrink as the sample count rises. Fixed when the target is made, so a
    // change applies at the next start. Set by a game's main; not read from or written to game.manifest's text.
    uint32_t sceneSamples{0};

    // The resolution of the shadow maps the renderer allocates whatever a game draws: 0, the default, is the engine as it was
    // (the cascades 2048x2048 x4 layers, the point lights' cubes 1024 and the spot lights' 1024, all D32: 120 MiB); any other
    // number sets all three to it, clamped (ResolveShadowMapResolution). A game that casts no shadow - every draw unlit, as the
    // sprite pipelines of spritesOnlyScenePipelines are - never samples them, and on a phone, whose GPU memory is the system's,
    // 120 MiB is a tenth of what a low-memory manager looks at. Set by a game's main; not read from or written to game.manifest's
    // text.
    uint32_t shadowMapResolution{0};

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
//
// It does not know about `--hidden`, which is not one of its arguments. The
// answer SupersonicApp reaches is ResolveStartWindow's, below: a caller that
// needs to agree with the window the engine really opened asks that instead.
bool ResolveFullscreen(const GameManifest& manifest, bool optionFullscreen, bool optionWindowed);

// How the window starts, decided before the device and the first swapchain.
struct StartWindow {
    // Covering the monitor from the first frame.
    bool fullscreen{false};

    // Above zero: a windowed start fitted to its monitor at this fraction of
    // the work area (WindowControl::FitWindowToMonitor). Zero: the window
    // stays at the size ResolveWindowSize gave it.
    float fitFraction{0.0f};
};

// The whole of that decision, as SupersonicApp makes it: ResolveFullscreen,
// then the manifest's fitted window when the start is windowed and `--window`
// named no size.
//
// `--hidden` counts as `--windowed` here, and it fits nothing either. A hidden
// window that went fullscreen would switch a monitor somebody is using, and a
// hidden run is a capture whose size must come from its command line rather
// than from whatever monitor the machine it ran on has.
//
// Pure and split out for ResolveWindowSize's reason: this was written inline
// in SupersonicApp, which no suite can construct, and the `--hidden` terms in
// it had then been checked by nothing at all. A game that repeats the engine's
// decision for its own bookkeeping (a menu that must start from the mode the
// window really has) asks this, and gets the same answer under every flag.
StartWindow ResolveStartWindow(const GameManifest& manifest, const LaunchOptions& options);

// The Vulkan 1.x minor version SupersonicApp hands the device selection:
// GameManifest::minimumVulkanMinor clamped into 1 to 2 (Vulkan 1.0 lacks the
// core functions the allocator binds; nothing in the engine asks for 1.3).
uint32_t ResolveVulkanMinor(const GameManifest& manifest);

// The sample count SupersonicApp caps the scene target at: GameManifest::sceneSamples 0 is 4 (as the engine always took),
// 1 and 2 are themselves, 3 is 2, and anything above 4 is 4.
uint32_t ResolveSceneSamples(const GameManifest& manifest);

// The resolution SupersonicApp hands the renderer for its shadow maps: GameManifest::shadowMapResolution 0 stays 0 (the default
// sizes), anything else is clamped into 16 to 4096.
uint32_t ResolveShadowMapResolution(const GameManifest& manifest);

// The Vulkan 1.x minor version the memory allocator (VMA) is told it may use on a
// device that reports `deviceMinor`: the device's own, capped at 2, the version
// the allocator was written and tested for. A 1.2 or newer device gets 2, as it
// always did; a 1.1 device gets 1, whose core entry points are all VMA binds.
uint32_t AllocatorVulkanMinor(uint32_t deviceMinor);

} // namespace GameRuntime

} // namespace Supersonic
