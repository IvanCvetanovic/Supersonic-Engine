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

// Where a game writes what belongs to the PLAYER rather than to the
// installation: saves, settings, screenshots.
//
// The engine had no answer to this, and both obvious answers ship a broken
// game. Beside the executable fails the moment the game is installed where a
// user cannot write, which on Windows is the default install location and is
// not something the player did wrong. The working directory is wherever the
// shortcut happened to point, so two launches of the same game can disagree
// about where its save is.
//
// `application` names the game's own folder inside the location the PLATFORM
// nominates:
//
//   Windows   %APPDATA%\<application>
//   macOS     ~/Library/Application Support/<application>
//   Linux     $XDG_DATA_HOME/<application>, else ~/.local/share/<application>
//
// Godot spells this `user://` and a game reaches for it before almost anything
// else. It is one function because the alternative is every game inventing its
// own, which is how the three executable-path copies above came to disagree.
//
// Read from the environment rather than through SHGetKnownFolderPath, and the
// reason is the cross-toolchain build: the whole of SupersonicCore compiles
// under MinGW-w64 today with no Windows-SDK-only link dependency, and that
// property is worth more here than the marginal robustness of the shell API.
// %APPDATA% is what the shell API would return anyway.
//
// CREATES the directory. Every caller would have to, and forgetting is silent -
// a save that returns false on the first launch and on every launch after.
//
// The name is SANITISED, not trusted: it becomes a path component, and a title
// carrying a separator or a `..` would otherwise write outside the folder it
// names. Returns empty for a name with nothing usable left in it, and for a
// platform that will not say where home is - a game that cannot persist is
// still playable, and that is a better answer than writing somewhere
// unpredictable.
std::filesystem::path UserDataDirectory(const std::string& application);

// The sanitisation UserDataDirectory applies, on its own.
//
// Split out for the same reason LooksLikeAPackagedFolder is: the function
// around it touches the real filesystem and the real environment, and the
// question "what does this name become" is pure.
std::string SanitiseForPathComponent(const std::string& name);

} // namespace Supersonic
