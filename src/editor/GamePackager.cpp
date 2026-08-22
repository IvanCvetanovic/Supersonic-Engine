#include "editor/GamePackager.hpp"

#include "core/GameRuntime.hpp"
#include "platform/ExecutablePath.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace Supersonic {

namespace {

bool copyTreeIfPresent(const fs::path& from, const fs::path& to, std::string& note) {
    std::error_code ec;
    if (!fs::exists(from, ec)) {
        note += " (skipped missing " + from.string() + ")";
        return true; // not fatal
    }
    fs::create_directories(to, ec);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        note += " (failed copying " + from.string() + ": " + ec.message() + ")";
        return false;
    }
    return true;
}

} // namespace

SerializationResult GamePackager::PackageStandaloneGame(const std::string& outputFolder,
                                                        const std::string& startupScene) {
    std::error_code ec;

    const fs::path exe = ExecutablePath();
    if (exe.empty() || !fs::exists(exe, ec)) {
        return { false, "Could not locate the running executable; nothing was packaged." };
    }

    const fs::path out(outputFolder);
    fs::create_directories(out, ec);
    if (ec) {
        return { false, "Could not create " + out.string() + ": " + ec.message() };
    }

    // Binary. Without this the output folder is unrunnable, which is exactly
    // what the old unconditional "SUCCESS!" concealed.
    fs::copy_file(exe, out / exe.filename(), fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return { false, "Failed to copy " + exe.filename().string() + ": " + ec.message() };
    }

    std::string note;
    bool ok = true;

    // Any runtime libraries sitting next to the executable.
    for (const auto& entry : fs::directory_iterator(exe.parent_path(), ec)) {
        if (ec) break;
        const auto& p = entry.path();
        if (!entry.is_regular_file()) continue;
        const std::string ext = p.extension().string();
        if (ext != ".dll" && ext != ".so" && ext != ".dylib") continue;

        // Hot reload works by loading a numbered copy of the script plugin, so
        // a build tree accumulates GameScripts.loaded1.dll and friends. Those
        // are scratch files; shipping them puts a stale copy of the game's
        // scripts in the release folder.
        if (p.stem().string().find(".loaded") != std::string::npos) continue;

        std::error_code copyError;
        fs::copy_file(p, out / p.filename(), fs::copy_options::overwrite_existing, copyError);
        if (copyError) {
            // Reported, not fatal, and no longer silent. This used to call
            // ec.clear() and move on, so a game whose script plugin failed to
            // copy - the one library it cannot run without - was announced as
            // packaged successfully, and the folder crashed on launch for
            // somebody else. A partial package is a result the caller can act
            // on; a successful-looking one is not.
            ok = false;
            note += " (failed copying " + p.filename().string() + ": " + copyError.message() + ")";
        }
    }

    // Cleared because directory_iterator writes `ec` from its CONSTRUCTOR - if
    // the executable's own directory cannot be enumerated, the range is empty
    // and `ec` stays set with nothing having gone wrong for packaging. Every
    // check below tests the same `ec`, so leaving it would fail the package for
    // a directory listing it does not need.
    ec.clear();

    // Everything a scene can reference, not just shaders and scenes. A
    // packaged game used to start with no textures, no models, no audio, no
    // materials and no prefabs - every one of which a scene file names by
    // path and expects to find.
    for (const char* directory : { "shaders", "scenes", "textures", "models",
                                   "materials", "audio", "prefabs", "branding" }) {
        ok &= copyTreeIfPresent(fs::path("assets") / directory,
                                out / "assets" / directory, note);
    }

    // Textures extracted out of .glb files. These are not under assets/ - they
    // are generated, so they live in the gitignored cache - but a scene that
    // uses an embedded texture serialises a cache/gltf/... path into its
    // material, and the game will look for exactly that path beside its own
    // executable.
    //
    // The game can regenerate them: the .glb ships, and the mesh resolves
    // before the texture does. But only if the install directory is WRITABLE,
    // and a game installed under Program Files is not - at which point the
    // extraction fails and TextureRegistry caches the failure as a
    // checkerboard, permanently. Shipping them costs a few kilobytes and makes
    // the packaged folder work without needing to write to itself.
    ok &= copyTreeIfPresent(fs::path("cache") / "gltf", out / "cache" / "gltf", note);

    // The marker that stops the copied binary from starting the editor. Without
    // it the packaged folder was an editor that happened to have a game's
    // assets next to it.
    GameManifest manifest;
    manifest.isGame = true;
    manifest.title = out.filename().empty() ? std::string("Supersonic Game")
                                            : out.filename().string();
    manifest.startupScene = startupScene;

    {
        std::ofstream file(out / GameRuntime::kManifestFilename);
        if (!file.is_open()) {
            return { false, "Could not write " + std::string(GameRuntime::kManifestFilename) +
                            " to " + out.string() + "; the copy would have started the editor." };
        }
        file << GameRuntime::Serialize(manifest);
        if (!file) {
            return { false, "Failed writing " + std::string(GameRuntime::kManifestFilename) + "." };
        }
    }

    if (!ok) {
        return { false, "Packaged partially to " + fs::absolute(out, ec).string() + note };
    }

    return { true, "Packaged " + exe.filename().string() + " to " + fs::absolute(out, ec).string() + note };
}

} // namespace Supersonic
