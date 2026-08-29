#pragma once

#include <filesystem>

namespace Supersonic {

// Absolute path of the running binary, or empty when the platform has no cheap
// way to ask.
//
// There were two copies of this before, in SupersonicApp and GamePackager, and
// they had already diverged: the packager handled macOS and paths longer than
// MAX_PATH, the app handled neither. A third copy was about to be written for
// the packaged-game check, which is one more than enough.
//
// Everything that has to work both from a build tree and from a packaged folder
// hangs off this: the script plugin sits next to the executable, and so does
// the manifest that says this is a game rather than the editor.
std::filesystem::path ExecutablePath();

// Directory containing the running binary. Empty if the path is unknown.
std::filesystem::path ExecutableDirectory();

// Makes the working directory the one every relative asset path is meant to be
// relative to, and reports where that ended up.
//
// The engine had two anchors that did not agree. GameRuntime::Load finds
// game.manifest relative to the EXECUTABLE, because a game is normally launched
// from somewhere other than its own folder. Everything the manifest then names
// - the startup scene, every shader, every texture and model and material and
// audio path inside that scene - is opened relative to the WORKING DIRECTORY.
//
// The two coincide only while the working directory happens to be the game
// folder. Start the same executable from anywhere else and it finds its
// manifest, opens a window under the right title, and then dies during pipeline
// creation on a .spv it cannot open - before initECS, before the scene is even
// reached, so none of the other broken paths get far enough to be blamed.
//
// Whichever anchor wins, both have to use it. For a packaged game the
// executable's directory wins, because that is the one that is true no matter
// how the game was launched. For the editor the working directory is left
// alone: it is launched from the project root on purpose, and the build tree is
// not laid out like a packaged folder.
bool AnchorAssetRootToExecutable();

// Whether `directory` is laid out the way a PACKAGED game is, rather than the
// way a build tree is. The judgement behind the anchoring above.
//
// Split out so it can be tested. Anchoring is an action with a process-wide
// effect - it moves the working directory - and a test that called it would
// change where every later test in the same binary resolves its fixtures. The
// decision is a pure question about a path, so it is asked separately, the same
// way RenderSystem::SortOpaqueDraws is reachable without a GPU.
bool LooksLikeAPackagedFolder(const std::filesystem::path& directory);

// Where relative asset paths resolve from. The working directory, named so that
// code reads as though it means it.
std::filesystem::path AssetRoot();

} // namespace Supersonic
