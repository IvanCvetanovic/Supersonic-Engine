// Magic Portals on the Supersonic Engine: level30 of the Godot remake, as the
// port plays it.
//
// The level and the remake's data are read from outside this repository, from
// where the build was told they are (SUPERSONIC_MAGICPORTALS_LEVELS and _DATA),
// or from --level and --data. Deliberately plain: boxes and a line of text. See
// docs/planning/2026-09-10-magic-portals-port.md.

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core/GameRuntime.hpp"
#include "core/LaunchOptions.hpp"
#include "core/Log.hpp"
#include "core/SupersonicApp.hpp"
#include "renderer/VulkanContext.hpp"

#include "MagicPortalsLayer.hpp"

int main(int argc, char** argv) {
    // --level and --data are the game's own flags. LaunchOptions refuses
    // anything it does not know, which is right for the engine, so the game
    // takes its flags out first and hands on the rest untouched.
    std::string level = std::string(MAGICPORTALS_LEVELS_DIR) + "/level30.tscn";
    std::string data = MAGICPORTALS_DATA_DIR;
    std::vector<char*> engineArgs{argv[0]};
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--level" || arg == "--data") {
            if (i + 1 >= argc) {
                std::cerr << arg << " needs a path\n";
                return EXIT_FAILURE;
            }
            (arg == "--level" ? level : data) = argv[++i];
            continue;
        }
        engineArgs.push_back(argv[i]);
    }

    const auto options = Supersonic::LaunchOptions::Parse(static_cast<int>(engineArgs.size()), engineArgs.data());
    if (options.helpRequested) {
        std::cout << Supersonic::LaunchOptions::Usage()
                  << "  --level <path>    a converted level (default " << level << ")\n"
                  << "  --data <dir>      the remake's game/data directory (default " << data << ")\n";
        return EXIT_SUCCESS;
    }
    if (!options.ok) {
        std::cerr << options.error << "\n";
        return EXIT_FAILURE;
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

    try {
        Supersonic::SupersonicApp app(options, &manifest);
        app.PushLayer(std::make_unique<MagicPortals::MagicPortalsLayer>(
            level, data, std::filesystem::temp_directory_path() / "supersonic-magicportals"));
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
