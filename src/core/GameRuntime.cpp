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

    // Absent, zero, or nonsense all mean "not stated" and leave the engine's
    // own default to answer. A refused size is LOGGED, because unlike an absent
    // one it is somebody's mistake and silence would leave them looking at a
    // window they did not ask for with nothing to explain it.
    const auto extent = [](const Json::Value& value, const char* name) -> uint32_t {
        const double asked = value.AsNumber(0.0);
        if (asked <= 0.0) return 0;

        const uint32_t rounded = static_cast<uint32_t>(asked);
        if (rounded < GameManifest::kMinimumExtent ||
            rounded > GameManifest::kMaximumExtent) {
            SUPERSONIC_LOG_ERROR("GameRuntime")
                << "game.manifest asks for a " << name << " of " << rounded
                << ", which is outside " << GameManifest::kMinimumExtent << ".."
                << GameManifest::kMaximumExtent << "; using the default instead."
                << std::endl;
            return 0;
        }
        return rounded;
    };

    manifest.width = extent(root["Width"], "width");
    manifest.height = extent(root["Height"], "height");

    // BOTH OR NEITHER. Half a size is not a size: a manifest naming only a
    // width would otherwise get that width against the default height, which is
    // an aspect ratio nobody chose.
    if (manifest.width == 0 || manifest.height == 0) {
        manifest.width = 0;
        manifest.height = 0;
    }

    manifest.fullscreen = root["Fullscreen"].AsBool(false);

    return manifest;
}

std::string Serialize(const GameManifest& manifest) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"Game\": " << (manifest.isGame ? "true" : "false") << ",\n";
    out << "  \"Title\": \"" << Json::Escape(manifest.title) << "\",\n";
    out << "  \"StartupScene\": \"" << Json::Escape(manifest.startupScene) << "\"";

    // Written only when the game asked for one, so a packaged game that never
    // stated a size produces the manifest it always did.
    if (manifest.width != 0 && manifest.height != 0) {
        out << ",\n  \"Width\": " << manifest.width
            << ",\n  \"Height\": " << manifest.height;
    }

    // The same: written only when asked for, so a windowed game's manifest is
    // byte for byte the one it always was.
    if (manifest.fullscreen) {
        out << ",\n  \"Fullscreen\": true";
    }

    out << "\n}\n";
    return out.str();
}

void ResolveWindowSize(const GameManifest& manifest, uint32_t optionWidth,
                       uint32_t optionHeight, uint32_t& outWidth, uint32_t& outHeight) {
    outWidth = GameManifest::kDefaultWidth;
    outHeight = GameManifest::kDefaultHeight;

    if (manifest.width != 0 && manifest.height != 0) {
        outWidth = manifest.width;
        outHeight = manifest.height;
    }

    // LAST, so it wins. See the header.
    if (optionWidth != 0 && optionHeight != 0) {
        outWidth = optionWidth;
        outHeight = optionHeight;
    }
}

bool ResolveFullscreen(const GameManifest& manifest, bool optionFullscreen, bool optionWindowed) {
    if (optionWindowed) return false;
    if (optionFullscreen) return true;
    return manifest.fullscreen;
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
