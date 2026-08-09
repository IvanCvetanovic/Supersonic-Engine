#include "editor/GamePackager.hpp"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace Engine {

bool GamePackager::PackageStandaloneGame(const std::string& outputFolder) {
    std::cout << "[GamePackager] Packaging standalone game release into " << outputFolder << "..." << std::endl;

    try {
        fs::create_directories(outputFolder);
        fs::create_directories(fs::path(outputFolder) / "assets" / "shaders");
        fs::create_directories(fs::path(outputFolder) / "assets" / "scenes");

        // Copy Binary Executable
        if (fs::exists("build/GameEngine.exe")) {
            fs::copy_file("build/GameEngine.exe", fs::path(outputFolder) / "GameEngine.exe", fs::copy_options::overwrite_existing);
        }

        // Copy Shaders
        if (fs::exists("assets/shaders")) {
            fs::copy("assets/shaders", fs::path(outputFolder) / "assets" / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }

        // Copy Scenes
        if (fs::exists("assets/scenes")) {
            fs::copy("assets/scenes", fs::path(outputFolder) / "assets" / "scenes", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }

        std::cout << "[GamePackager] SUCCESS! Standalone game packaged cleanly at: " << fs::absolute(outputFolder) << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[GamePackager] ERROR during packaging: " << e.what() << std::endl;
        return false;
    }
}

} // namespace Engine
