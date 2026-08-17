#pragma once

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

} // namespace GameRuntime

} // namespace Supersonic
