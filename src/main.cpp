#include "core/SupersonicApp.hpp"
#include "core/LaunchOptions.hpp"
#include "core/Log.hpp"
#include "renderer/VulkanContext.hpp"
#include "platform/ExecutablePath.hpp"

#include <filesystem>
#include <iostream>
#include <exception>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

// Beside the executable, not in the working directory: a packaged game is
// launched from wherever its shortcut points, and a log written somewhere the
// player cannot find is a log nobody will ever send you.
std::string logFilePath() {
    const std::filesystem::path exe = Supersonic::ExecutablePath();
    if (exe.empty()) return "supersonic.log";
    return (exe.parent_path() / "supersonic.log").string();
}

// A fatal error during init happens before there is a window, and on Windows a
// GUI build has no console for the message to land in. Printing it and exiting
// looks, to the person running it, exactly like nothing happening at all.
void reportFatal(const std::string& what) {
    std::cerr << "[Engine Fatal Exception]: " << what << std::endl;
#if defined(_WIN32)
    const std::string body = what + "\n\nFull details are in:\n" + logFilePath();
    MessageBoxA(nullptr, body.c_str(), "Supersonic Engine - fatal error",
                MB_OK | MB_ICONERROR);
#endif
}

} // namespace

int main(int argc, char** argv) {
    const auto options = Supersonic::LaunchOptions::Parse(argc, argv);

    // Program output, not diagnostics: usage text is what the program was asked
    // to produce, so it goes to stdout whether or not anyone is filtering logs.
    if (options.helpRequested) {
        std::cout << Supersonic::LaunchOptions::Usage();
        return EXIT_SUCCESS;
    }
    if (!options.ok) {
        std::cerr << "[Engine] " << options.error << "\n\n"
                  << Supersonic::LaunchOptions::Usage();
        return EXIT_FAILURE;
    }

    const std::string logPath = logFilePath();
    if (!Supersonic::Log::SetFileSink(logPath)) {
        std::cerr << "[Engine] Could not open " << logPath
                  << " for writing; continuing without a log file." << std::endl;
    }

    try {
        Supersonic::SupersonicApp app(options);
        app.Run();
    } catch (const std::exception& e) {
        reportFatal(e.what());
        Supersonic::Log::CloseFileSink();
        return EXIT_FAILURE;
    } catch (...) {
        reportFatal("unknown exception");
        Supersonic::Log::CloseFileSink();
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
        Supersonic::Log::CloseFileSink();
        return EXIT_FAILURE;
    }

    // Said out loud, because a green run that never loaded the layer proves
    // nothing and looks identical to one that did.
    if (Supersonic::VulkanContext::ValidationLayersActive()) {
        std::cout << "[Engine] Clean exit with validation active." << std::endl;
    }

    Supersonic::Log::CloseFileSink();
    return EXIT_SUCCESS;
}
