#pragma once

#include <filesystem>
#include <functional>

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

// Where a game's engine files were found, which is also what the log says.
enum class AssetRootSource {
    // assets/shaders beside the executable: a packaged game.
    PackagedFolder,
    // assets/shaders in the directory it was launched from: the engine's own
    // root, which is how every game ran while the games lived in its tree.
    WorkingDirectory,
    // assets/shaders under the root the build named: a game built in its own
    // repository with the engine as a subproject, launched from anywhere.
    ConfiguredRoot,
    // None of them. The working directory is kept and the first .spv will not
    // open - the failure a run with no engine files has always had.
    Unresolved,
};

struct AssetRootChoice {
    AssetRootSource source = AssetRootSource::Unresolved;
    std::filesystem::path root;
};

// Which directory a GAME's relative paths resolve from. First match wins:
//
//   1. the executable's directory, when it holds the engine's files - a
//      packaged folder, which is true however the game was launched;
//   2. the working directory, when it holds them - a run from the engine's
//      root, and every run made before a game could live outside it;
//   3. `configuredRoot`, when it holds them - the engine checkout a game's own
//      repository builds against, named by the build (ConfiguredAssetRoot);
//   4. otherwise the working directory, unchanged.
//
// ONE ANSWER FOR EVERY PATH. The engine opens its files by relative path from
// many places - seventeen .spv in two renderers, the pipeline cache, the scene
// and prefab folders, the asset database's scan - and a game passes relative
// paths of its own. Moving the working directory once keeps them all agreeing,
// which is what the packaged-game anchoring already did; resolving each call
// site against a root instead would be one new rule per call site, and the
// first one missed would read a file from the wrong tree without a word.
//
// The packaged folder outranks the configured root on purpose. A build bakes
// the root as an absolute path on the machine that built it; a packaged copy
// of that game has to ignore it and use its own folder, or it would go looking
// for its shaders in someone's source checkout. And the working directory
// outranks it because that keeps every existing run exactly as it was: a run
// from the engine's root never looks at the configured root at all.
//
// Pure: `holdsEngineAssets` is the only question asked of the filesystem, so
// the rule is tested without touching one. The program asks HoldsEngineAssets.
AssetRootChoice ChooseAssetRoot(
    const std::filesystem::path& executableDirectory,
    const std::filesystem::path& workingDirectory,
    const std::filesystem::path& configuredRoot,
    const std::function<bool(const std::filesystem::path&)>& holdsEngineAssets);

// The root the BUILD named for rule 3: SUPERSONIC_ASSET_ROOT. CMake sets it to
// the engine's own checkout when the engine is built as part of another
// project, and leaves it empty when the engine is built for itself, so the
// engine's own build carries no path and behaves as it always did. Empty means
// there is no third rule.
std::filesystem::path ConfiguredAssetRoot();

// Makes the working directory the one every relative asset path is meant to be
// relative to, and reports where that ended up. For a GAME only.
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
// Whichever anchor wins, both have to use it; ChooseAssetRoot decides which.
// For the editor the working directory is left alone: it is launched from the
// project root on purpose, and the build tree is not laid out like a packaged
// folder.
//
// The directory moves for rules 1 and 3 only. Every RELATIVE path resolves
// from the new one afterwards, including a relative --screenshot, --record or
// --replay - as it always has for a packaged game.
AssetRootChoice AnchorAssetRoot();

// Whether `directory` holds the engine's runtime files. The question
// ChooseAssetRoot asks of each candidate: assets/shaders, and why that and not
// assets alone is in LooksLikeAPackagedFolder, which is the same question.
bool HoldsEngineAssets(const std::filesystem::path& directory);

// Whether `directory` is laid out the way a PACKAGED game is, rather than the
// way a build tree is. The judgement behind rule 1 above.
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
