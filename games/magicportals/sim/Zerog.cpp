#include "sim/Zerog.hpp"

#include "core/Json.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace MagicPortals::Zerog {

namespace {

namespace Json = Supersonic::Json;

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot read";
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();

    // Named, not a temporary: the parser keeps a reference to what it reads.
    const std::string content = text.str();
    Json::Parser parser(content);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }

    const auto number = [&](const char* group, const char* key, double& into) -> bool {
        const Json::Value& block = root[group];
        if (!block.Has(key) || !block[key].IsNumber()) {
            error = path + ": " + std::string(group) + "." + key + " is missing or not a number";
            return false;
        }
        into = block[key].AsNumber();
        return true;
    };
    const auto string = [&](const char* group, const char* key, std::string& into) -> bool {
        const Json::Value& block = root[group];
        if (!block.Has(key) || !block[key].IsString()) {
            error = path + ": " + std::string(group) + "." + key + " is missing or not a string";
            return false;
        }
        into = block[key].AsString();
        return true;
    };

    Rules read;
    if (!string("level_property", "flag_name", read.flagName)) return false;
    if (!number("recoil", "metres_per_second", read.recoilMetresPerSecond)) return false;
    if (!number("world", "gravity_px_s2", read.worldGravityPx)) return false;

    if (read.flagName.empty()) {
        error = path + ": level_property.flag_name names the property that turns the mode on";
        return false;
    }
    // No recoil is the whole mode gone: gravity off, walking off, and nothing
    // left that can move the player at all. Every one of these levels would
    // start, and none of them could be finished.
    if (read.recoilMetresPerSecond <= 0.0) {
        error = path + ": recoil.metres_per_second is above zero, or nothing can move the player";
        return false;
    }
    // The mode IS zero gravity (setGravity V2_ZERO). A file that says otherwise
    // is describing a different mode, so it is refused rather than obeyed.
    if (read.worldGravityPx != 0.0) {
        error = path + ": world.gravity_px_s2 is zero in this mode - the original sets V2_ZERO";
        return false;
    }

    out = std::move(read);
    return true;
}

glm::vec3 Impulse(const glm::dvec2& playerPx, const glm::dvec2& aimedPx, double metresPerSecond) {
    const glm::dvec2 away = playerPx - aimedPx;
    const double length = std::sqrt(away.x * away.x + away.y * away.y);
    if (length <= 0.0) return glm::vec3(0.0f);
    const glm::dvec2 unit = away / length;
    return glm::vec3(static_cast<float>(unit.x * metresPerSecond), static_cast<float>(-unit.y * metresPerSecond),
                     0.0f);
}

} // namespace MagicPortals::Zerog
