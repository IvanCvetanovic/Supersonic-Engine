#include "core/SupersonicApp.hpp"
#include "core/LaunchOptions.hpp"
#include "renderer/VulkanContext.hpp"

#include <iostream>
#include <exception>
#include <cstdlib>

int main(int argc, char** argv) {
    const auto options = Supersonic::LaunchOptions::Parse(argc, argv);

    if (options.helpRequested) {
        std::cout << Supersonic::LaunchOptions::Usage();
        return EXIT_SUCCESS;
    }
    if (!options.ok) {
        std::cerr << "[Engine] " << options.error << "\n\n"
                  << Supersonic::LaunchOptions::Usage();
        return EXIT_FAILURE;
    }

    try {
        Supersonic::SupersonicApp app(options);
        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "[Engine Fatal Exception]: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    // A clean shutdown is not the same as a clean run. The validation layers
    // report through a callback that cannot fail a process on its own, so
    // before this the engine could emit any number of errors - a null sampler
    // in a descriptor, a framebuffer freed mid-recording - and still exit 0.
    // Two showstoppers survived six commits behind precisely that gap.
    const unsigned validationErrors = Supersonic::VulkanContext::ValidationErrorCount();
    if (validationErrors > 0) {
        std::cerr << "[Engine] " << validationErrors
                  << " Vulkan validation error(s) were reported; failing the run."
                  << std::endl;
        return EXIT_FAILURE;
    }

    // Said out loud, because a green run that never loaded the layer proves
    // nothing and looks identical to one that did.
    if (Supersonic::VulkanContext::ValidationLayersActive()) {
        std::cout << "[Engine] Clean exit with validation active." << std::endl;
    }

    return EXIT_SUCCESS;
}
