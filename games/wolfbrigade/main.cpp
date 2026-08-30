// Wolf Brigade, as a game that LINKS the engine rather than one compiled into
// it.
//
// This file is the point of the Phase 0 spike. The engine's own main.cpp builds
// a SupersonicApp and runs it; so does this one, with three lines in the middle
// that the engine knows nothing about. If a game cannot be written this way then
// the layer seam does not work, whatever EngineLayer.hpp says about it.
//
// See docs/planning/2026-08-25-wolf-brigade-port.md.

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core/LaunchOptions.hpp"
#include "core/Log.hpp"
#include "core/GameRuntime.hpp"
#include "core/SupersonicApp.hpp"
#include "platform/ExecutablePath.hpp"
#include "renderer/VulkanContext.hpp"

#include "WolfBrigadeLayer.hpp"

int main(int argc, char** argv) {
    // Nothing to filter any more.
    //
    // The spike took --drawables and --sun, because what it built was a lane of
    // invented quads whose COUNT was the measurement and whose lighting was the
    // thing being proved. The layer runs the real match now, so how many
    // drawables there are is the game's answer rather than the argument's, and
    // every flag the engine understands passes straight through.

    const auto options = Supersonic::LaunchOptions::Parse(argc, argv);

    if (options.helpRequested) {
        std::cout << Supersonic::LaunchOptions::Usage()
                  << "  --drawables <n>  how many quads the lane spawns (default 400)\n"
                  << "  --sun <f>        directional light intensity (default 1.4)\n";
        return EXIT_SUCCESS;
    }
    if (!options.ok) {
        std::cerr << options.error << "\n";
        return EXIT_FAILURE;
    }

    // DECLARED, not discovered. This binary is a game whether or not somebody
    // remembered to drop a game.manifest beside it, and saying so is what stops
    // the engine handing it the editor's demo scene and leaving it in edit mode
    // with its layer never ticked.
    Supersonic::GameManifest manifest;
    manifest.isGame = true;
    manifest.title = "Wolf Brigade";
    // No startup scene: the board is built by the simulation, not placed.

    // WHERE THIS GAME MAY WRITE, resolved once, here, and handed down.
    //
    // The layer does not ask for itself: "where may I write" is a question
    // about the machine rather than about this game, and a layer that resolved
    // its own path could not be built by a test without one. Empty when the
    // platform will not say - the game is still playable, it just does not
    // persist, and it says so rather than writing somewhere unpredictable.
    const std::filesystem::path saveDir =
        Supersonic::UserDataDirectory(manifest.title);
    if (saveDir.empty()) {
        std::cerr << "[Wolf Brigade] no writable user directory; "
                     "this session will not be saved.\n";
    }

    try {
        Supersonic::SupersonicApp app(options, &manifest);

        // The whole claim, in one line.
        app.PushLayer(std::make_unique<WolfBrigade::WolfBrigadeLayer>(saveDir.string()));

        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "[Wolf Brigade] fatal: " << e.what() << std::endl;
        Supersonic::Log::CloseFileSink();
        return EXIT_FAILURE;
    }

    // Same standard the engine holds itself to: a clean shutdown is not a clean
    // run, and a validation error that only logs is a bug that ships.
    const unsigned validationErrors = Supersonic::VulkanContext::ValidationErrorCount();
    if (validationErrors > 0) {
        std::cerr << "[Wolf Brigade] " << validationErrors
                  << " Vulkan validation error(s); failing the run." << std::endl;
        Supersonic::Log::CloseFileSink();
        return EXIT_FAILURE;
    }

    Supersonic::Log::CloseFileSink();
    return EXIT_SUCCESS;
}
