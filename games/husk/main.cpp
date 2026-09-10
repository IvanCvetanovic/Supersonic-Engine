// HUSK on the Supersonic Engine.
//
// The simulation is the Rust game's, ported bit for bit and held to its per-tick
// hashes (games/husk/sim, tests/test_husk_*). This is the first thing that puts
// it on a screen, and it is deliberately ugly: boxes, no fog, no glow. See
// docs/planning/2026-09-10-husk-port.md.

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core/GameRuntime.hpp"
#include "core/LaunchOptions.hpp"
#include "core/Log.hpp"
#include "core/SupersonicApp.hpp"
#include "renderer/VulkanContext.hpp"

#include "HuskLayer.hpp"

int main(int argc, char** argv) {
    // --mission is the game's own flag. LaunchOptions refuses anything it does
    // not know, which is right for the engine, so the game takes its flag out
    // first and hands on the rest untouched.
    std::string mission = husk::HuskLayer::kDefaultMission;
    std::vector<char*> engineArgs{argv[0]};
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--mission") {
            if (i + 1 >= argc) {
                std::cerr << "--mission needs a name\n";
                return EXIT_FAILURE;
            }
            mission = argv[++i];
            continue;
        }
        engineArgs.push_back(argv[i]);
    }

    const auto options = Supersonic::LaunchOptions::Parse(static_cast<int>(engineArgs.size()),
                                                          engineArgs.data());
    if (options.helpRequested) {
        std::cout << Supersonic::LaunchOptions::Usage()
                  << "  --mission <name>  a mission from data/missions, or '"
                  << husk::HuskLayer::kSandbox << "' for the M2 macro sandbox (default "
                  << husk::HuskLayer::kDefaultMission << ")\n";
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
    manifest.title = "HUSK";

    try {
        Supersonic::SupersonicApp app(options, &manifest);
        app.PushLayer(std::make_unique<husk::HuskLayer>(mission));
        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "[HUSK] fatal: " << e.what() << std::endl;
        Supersonic::Log::CloseFileSink();
        return EXIT_FAILURE;
    }

    const unsigned validationErrors = Supersonic::VulkanContext::ValidationErrorCount();
    if (validationErrors > 0) {
        std::cerr << "[HUSK] " << validationErrors << " Vulkan validation error(s); failing the run."
                  << std::endl;
        Supersonic::Log::CloseFileSink();
        return EXIT_FAILURE;
    }

    Supersonic::Log::CloseFileSink();
    return EXIT_SUCCESS;
}
