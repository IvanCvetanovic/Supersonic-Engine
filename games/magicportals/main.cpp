// Magic Portals on the Supersonic Engine: the Godot remake's levels, in the
// original's order, as the port plays them.
//
// The levels, their order, their art and the remake's data are read from
// outside this repository, from where the build was told they are
// (SUPERSONIC_MAGICPORTALS_LEVELS, _CHAPTERS and _DATA; the art beside the
// levels), or from --levels, --art and --data. Drawn with the levels' own art
// and a few lines of text. See docs/planning/2026-09-11-magic-portals-remaster.md.

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core/GameRuntime.hpp"
#include "core/LaunchOptions.hpp"
#include "platform/ExecutablePath.hpp"
#include "core/Log.hpp"
#include "core/SupersonicApp.hpp"
#include "renderer/VulkanContext.hpp"

#include "MagicPortalsLayer.hpp"

int main(int argc, char** argv) {
    // --level, --levels, --art and --data are the game's own flags. LaunchOptions
    // refuses anything it does not know, which is right for the engine, so the
    // game takes its flags out first and hands on the rest untouched.
    MagicPortals::MagicPortalsLayer::Paths paths;
    paths.prisms = std::filesystem::temp_directory_path() / "supersonic-magicportals";
    // No --level opens the menu, which is the game's own front door. A named
    // level is entered directly, and that is how the suites and every headless
    // render run: neither ever sees the menu.
    std::string start;
    std::vector<char*> engineArgs{argv[0]};
    bool artGiven = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--level" || arg == "--levels" || arg == "--art" || arg == "--data") {
            if (i + 1 >= argc) {
                std::cerr << arg << " needs a value\n";
                return EXIT_FAILURE;
            }
            const std::string value = argv[++i];
            if (arg == "--level") {
                start = value;
            } else if (arg == "--levels") {
                paths.levels = value;
            } else if (arg == "--art") {
                paths.art = value;
                artGiven = true;
            } else {
                paths.data = value;
            }
            continue;
        }
        engineArgs.push_back(argv[i]);
    }
    // The converter writes the art beside the levels, so levels somewhere else
    // bring their art with them unless --art says otherwise.
    if (!artGiven && paths.levels != MAGICPORTALS_LEVELS_DIR) paths.art = paths.levels + "/..";

    const auto options = Supersonic::LaunchOptions::Parse(static_cast<int>(engineArgs.size()), engineArgs.data());
    if (options.helpRequested) {
        std::cout << Supersonic::LaunchOptions::Usage()
                  << "  --level <name>    the level to start at, as chapters.json names it. Without it\n"
                  << "                    the game opens its menu.\n"
                  << "  --levels <dir>    the converted levels (default " << paths.levels << ")\n"
                  << "  --art <dir>       what their res:// stands for (default the directory above them)\n"
                  << "  --data <dir>      the remake's game/data directory (default " << paths.data << ")\n";
        return EXIT_SUCCESS;
    }
    if (!options.ok) {
        std::cerr << options.error << "\n";
        return EXIT_FAILURE;
    }

    // The diagnostics to a FILE as well as the console.
    //
    // Nothing else in the engine opens this sink, and Log.hpp says why it
    // matters: a shipped game gets WIN32_EXECUTABLE and has no console at all,
    // so without this its diagnostics go nowhere. That is no use at all when
    // the thing being chased only happens while somebody else is playing.
    const std::filesystem::path logPath = std::filesystem::temp_directory_path() / "magicportals.log";
    if (Supersonic::Log::SetFileSink(logPath.string())) {
        std::cout << "[Magic Portals] logging to " << logPath.string() << std::endl;
    }

    // Declared, not discovered, for the reason Wolf Brigade's main gives: a
    // game binary is a game whether or not a manifest sits beside it.
    Supersonic::GameManifest manifest;
    manifest.isGame = true;
    manifest.title = "Magic Portals";
    // No startup scene: the layer builds the level. Left at the manifest's
    // default, a run from the repository root opens the editor's demo scene
    // beside it, which the first headless render showed above level30.
    manifest.startupScene.clear();

    // WHERE THIS GAME MAY WRITE, resolved once, here, and handed down.
    //
    // The layer does not ask for itself, which is WolfBrigadeLayer's contract
    // and exists for its reason: "where may I write" is a question about the
    // machine rather than about this game, and a layer that resolved its own
    // path could not be built by a test without one. Eighteen suites build this
    // layer bare, and every one of them would otherwise be writing the same
    // scores.json into the ctest working directory and reading each other's.
    //
    // Empty when the platform will not say: the medals are then not kept, which
    // the game says out loud rather than writing somewhere unpredictable.
    const std::filesystem::path saveDir = Supersonic::UserDataDirectory(manifest.title);
    if (saveDir.empty()) {
        std::cerr << "[Magic Portals] no writable user directory; "
                     "medals earned this session will not be kept.\n";
    }
    paths.saveDir = saveDir.string();

    try {
        Supersonic::SupersonicApp app(options, &manifest);

        // SAID OUT LOUD, because the two cases are indistinguishable otherwise.
        //
        // A run with the validation layers absent produces exactly the output a
        // clean run does: none. This game was played through a Release build,
        // where SUPERSONIC_ENABLE_VALIDATION defaults to 0, and its silent log
        // was nearly read as evidence that the frames were clean - when in fact
        // nothing had been watching. VulkanContext.hpp makes the same point
        // about CI: "a green run that never loaded the layer proves nothing and
        // looks identical to one that did".
        SUPERSONIC_LOG_INFO("Magic Portals")
            << "Vulkan validation layers: "
            << (Supersonic::VulkanContext::ValidationLayersActive() ? "ACTIVE"
                                                                    : "NOT LOADED - nothing is checking this run")
            << std::endl;

        app.PushLayer(std::make_unique<MagicPortals::MagicPortalsLayer>(paths, start));
        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "[Magic Portals] fatal: " << e.what() << std::endl;
        Supersonic::Log::CloseFileSink();
        return EXIT_FAILURE;
    }

    const unsigned validationErrors = Supersonic::VulkanContext::ValidationErrorCount();
    if (validationErrors > 0) {
        std::cerr << "[Magic Portals] " << validationErrors << " Vulkan validation error(s); failing the run."
                  << std::endl;
        Supersonic::Log::CloseFileSink();
        return EXIT_FAILURE;
    }

    Supersonic::Log::CloseFileSink();
    return EXIT_SUCCESS;
}
