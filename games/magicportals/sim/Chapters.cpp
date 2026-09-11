#include "sim/Chapters.hpp"

#include "core/Json.hpp"

#include <fstream>
#include <sstream>
#include <utility>

namespace MagicPortals::Chapters {

int Table::Find(const std::string& name) const {
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (levels[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

int Table::Next(int at) const {
    const std::size_t next = static_cast<std::size_t>(at) + 1;
    if (at < 0 || next >= levels.size()) return -1;
    return levels[next].world == levels[static_cast<std::size_t>(at)].world ? static_cast<int>(next) : -1;
}

std::string Label(const Level& level) {
    return std::to_string(level.world + 1) + "-" + std::to_string(level.index + 1);
}

bool Load(const std::string& path, Table& out, std::string& error) {
    namespace Json = Supersonic::Json;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    // Named, not a temporary: the parser keeps a reference to what it reads.
    const std::string text = buffer.str();
    Json::Parser parser(text);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    if (!root.IsObject() || !root.Has("format") || !root["format"].IsNumber() || root["format"].AsNumber() != 1.0) {
        error = path + ": not the converter's format 1";
        return false;
    }
    if (!root.Has("worlds") || !root["worlds"].IsArray()) {
        error = path + ": no \"worlds\" array";
        return false;
    }

    Table read;
    for (const Json::Value& world : root["worlds"].AsArray()) {
        if (!world.IsObject() || !world.Has("index") || !world["index"].IsNumber() || !world.Has("levels") ||
            !world["levels"].IsArray()) {
            error = path + ": a world without an index or a \"levels\" array";
            return false;
        }
        const int worldIndex = static_cast<int>(world["index"].AsNumber());
        for (const Json::Value& level : world["levels"].AsArray()) {
            if (!level.IsObject() || !level.Has("index") || !level["index"].IsNumber() || !level.Has("name") ||
                !level["name"].IsString() || !level.Has("golden_score") || !level["golden_score"].IsNumber()) {
                error = path + ": world " + std::to_string(worldIndex) +
                        " has a level without an index, a name or a golden_score";
                return false;
            }
            Level entry;
            entry.world = worldIndex;
            entry.index = static_cast<int>(level["index"].AsNumber());
            entry.name = level["name"].AsString();
            entry.goldenScore = static_cast<int>(level["golden_score"].AsNumber());
            read.levels.push_back(std::move(entry));
        }
    }
    out = std::move(read);
    return true;
}

} // namespace MagicPortals::Chapters
