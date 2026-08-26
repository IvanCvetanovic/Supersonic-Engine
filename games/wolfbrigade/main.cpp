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
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core/LaunchOptions.hpp"
#include "core/Log.hpp"
#include "core/SupersonicApp.hpp"
#include "renderer/VulkanContext.hpp"

#include "WolfBrigadeLayer.hpp"

int main(int argc, char** argv) {
    // How many drawables to build. The plan's busy campaign frame is 350-400;
    // endless has no ceiling in the data, so the number is an argument.
    int drawables = 400;
    float sunIntensity = 1.4f;
    std::string filtered;
    std::vector<char*> passthrough;
    passthrough.push_back(argv[0]);

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--drawables" && i + 1 < argc) {
            drawables = std::atoi(argv[++i]);
            if (drawables < 5) drawables = 5;
            continue;
        }
        // Varies the sun so the unlit path can be proved: the unlit quads must
        // not move by one bit between two runs, and the lit ground must.
        if (arg == "--sun" && i + 1 < argc) {
            sunIntensity = static_cast<float>(std::atof(argv[++i]));
            continue;
        }
        passthrough.push_back(argv[i]);
    }

    const auto options = Supersonic::LaunchOptions::Parse(
        static_cast<int>(passthrough.size()), passthrough.data());

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

    try {
        Supersonic::SupersonicApp app(options);

        // The whole claim, in one line.
        app.PushLayer(std::make_unique<WolfBrigade::WolfBrigadeLayer>(drawables, sunIntensity));

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
