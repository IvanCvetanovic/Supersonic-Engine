#include "core/AssetDatabase.hpp"
#include "core/MaterialLibrary.hpp"
#include "core/SceneSerializer.hpp"

#include <entt/entt.hpp>
#include "core/SupersonicApp.hpp"
#include "core/LaunchOptions.hpp"
#include "core/Log.hpp"
#include "renderer/VulkanContext.hpp"
#include "platform/ExecutablePath.hpp"

#include <filesystem>
#include <fstream>
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

    // Before any window, any Vulkan device and any scene: this writes sidecars
    // and then exits, so a project can be given identities from a script or a
    // build step without a GPU anywhere in sight.
    if (options.importAssets) {
        const auto result = Supersonic::AssetDatabase::Instance().Import("assets");
        if (!result.ok) {
            std::cerr << "--import-assets: no 'assets' folder here.\n";
            return EXIT_FAILURE;
        }
        std::cout << "Imported assets: " << result.minted << " new, "
                  << result.adopted << " recovered from a rename, "
                  << result.refreshed << " re-hashed.\n";

        // Minting alone changes nothing for the files that do the REFERENCING.
        // A `.material` names three textures by path and goes on naming them by
        // path until it is written again, so the import writes them again -
        // otherwise "give this project identities" leaves half of it without
        // any.
        //
        // Scenes have to be restamped too, and they are where it matters most:
        // MainScene alone names eighteen textures. Leaving them to be re-saved
        // in the editor would mean the project's own scene was the one thing
        // the feature did not protect.
        //
        // The risk is real and worth guarding rather than reasoning about: a
        // scene has to be loaded into a registry and written back out, so a
        // component the codec does not carry would be silently dropped.
        //
        // So each scene is written to a STRING, read back into a second
        // registry, and written again - and only committed to disk when the two
        // strings are IDENTICAL. That is a fixed point, which is a much
        // stronger statement than "the same number of entities came back": it
        // says every field the codec writes survives being read, for every
        // entity, or the file is left exactly as it was.
        size_t restamped = 0;
        std::error_code ec;
        const std::filesystem::path materials("assets/materials");
        if (std::filesystem::is_directory(materials, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(materials, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                if (entry.path().extension() != ".material") continue;

                const std::string path = entry.path().generic_string();
                std::ifstream in(path, std::ios::binary);
                if (!in.is_open()) continue;
                const std::string text((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
                in.close();

                Supersonic::MaterialAsset asset;
                std::string error;
                if (!Supersonic::MaterialLibrary::Deserialize(text, asset, error)) {
                    std::cerr << "  could not re-stamp " << path << ": " << error << "\n";
                    continue;
                }

                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                if (!out.is_open()) continue;
                out << Supersonic::MaterialLibrary::Serialize(asset);
                if (out.good()) ++restamped;
            }
        }
        size_t scenes = 0;
        size_t refused = 0;
        const std::filesystem::path sceneDir("assets/scenes");
        if (std::filesystem::is_directory(sceneDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(sceneDir, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                if (entry.path().extension() != ".scene") continue;
                const std::string path = entry.path().generic_string();

                entt::registry loaded;
                const auto read = Supersonic::SceneSerializer::Deserialize(loaded, path);
                if (!read.ok) {
                    std::cerr << "  could not re-stamp " << path << ": " << read.message << "\n";
                    continue;
                }
                const std::string rewritten =
                    Supersonic::SceneSerializer::SerializeToString(loaded);

                entt::registry check;
                const auto verified =
                    Supersonic::SceneSerializer::DeserializeFromString(check, rewritten);
                if (!verified.ok ||
                    Supersonic::SceneSerializer::SerializeToString(check) != rewritten) {
                    std::cerr << "  REFUSING to re-stamp " << path
                              << ": the round trip did not come back whole.\n";
                    ++refused;
                    continue;
                }

                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                if (!out.is_open()) continue;
                out << rewritten;
                if (out.good()) ++scenes;
            }
        }

        std::cout << "Re-stamped " << restamped << " material asset(s) and " << scenes
                  << " scene(s).\n";
        if (refused > 0) {
            std::cerr << refused << " scene(s) were left alone; see above.\n";
            return EXIT_FAILURE;
        }
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
