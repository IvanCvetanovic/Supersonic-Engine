#include "core/GameRuntime.hpp"
#include "core/Log.hpp"

#include "core/Json.hpp"
#include "platform/ExecutablePath.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace Supersonic {

namespace GameRuntime {

GameManifest Parse(const std::string& text) {
    GameManifest manifest;

    Json::Value root;
    std::string error;
    if (!Json::Parse(text, root, error) || !root.IsObject()) {
        // A manifest that exists but cannot be read is a packaging bug, and
        // starting the editor instead would hide it behind a working-looking
        // window. Say so and carry on as the editor, which is the only thing
        // that can be done without a scene to load.
        SUPERSONIC_LOG_ERROR("GameRuntime") << "game.manifest is not readable JSON: " << error << std::endl;
        return manifest;
    }

    // The key is what marks a game, not the file's mere existence: a stray
    // game.manifest in a build tree should not turn the editor into a game.
    if (!root["Game"].AsBool(false)) return manifest;

    manifest.isGame = true;
    manifest.startupScene = root["StartupScene"].AsString(manifest.startupScene);
    manifest.title = root["Title"].AsString(manifest.title);
    return manifest;
}

std::string Serialize(const GameManifest& manifest) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"Game\": " << (manifest.isGame ? "true" : "false") << ",\n";
    out << "  \"Title\": \"" << Json::Escape(manifest.title) << "\",\n";
    out << "  \"StartupScene\": \"" << Json::Escape(manifest.startupScene) << "\"\n";
    out << "}\n";
    return out.str();
}

GameManifest Load() {
    const std::filesystem::path directory = ExecutableDirectory();
    if (directory.empty()) return {};

    const std::filesystem::path path = directory / kManifestFilename;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return {};

    std::ifstream file(path);
    if (!file.is_open()) return {};

    std::stringstream ss;
    ss << file.rdbuf();

    GameManifest manifest = Parse(ss.str());
    if (manifest.isGame) {
        SUPERSONIC_LOG_INFO("GameRuntime") << "Running as a packaged game: " << manifest.title
                  << " (" << manifest.startupScene << ")." << std::endl;
    }
    return manifest;
}

} // namespace GameRuntime

} // namespace Supersonic
