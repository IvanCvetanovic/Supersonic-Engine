#include "sim/Art.hpp"

#include "core/Json.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

namespace MagicPortals::Art {

namespace {

namespace Json = Supersonic::Json;

bool WholeBelow(const Json::Value& value, int below, int& out) {
    if (!value.IsNumber()) return false;
    const double n = value.AsNumber();
    if (n < 0.0 || n != std::floor(n) || n >= below) return false;
    out = static_cast<int>(n);
    return true;
}

bool WholeAtLeastOne(const Json::Value& value, int& out) {
    if (!value.IsNumber()) return false;
    const double n = value.AsNumber();
    if (n < 1.0 || n != std::floor(n) || n > 64.0) return false;
    out = static_cast<int>(n);
    return true;
}

bool ReadPicture(const Json::Value& entry, const std::string& name, Picture& out, std::string& error) {
    if (!entry.IsObject() || !entry.Has("sprite") || !entry["sprite"].IsString() || !entry.Has("additive") ||
        !entry["additive"].IsBool()) {
        error = name + " needs sprite and additive";
        return false;
    }
    Picture read;
    read.sprite = entry["sprite"].AsString("");
    read.additive = entry["additive"].AsBool();
    if ((entry.Has("columns") && !WholeAtLeastOne(entry["columns"], read.columns)) ||
        (entry.Has("rows") && !WholeAtLeastOne(entry["rows"], read.rows))) {
        error = name + "'s columns and rows are whole numbers from 1";
        return false;
    }
    // A sheet of more than one frame plays, so it has to say how fast.
    if (read.Frames() > 1) {
        const Json::Value& animation = entry["animation"];
        if (!animation.IsObject() || !animation.Has("frames_per_second") ||
            !animation["frames_per_second"].IsNumber() || animation["frames_per_second"].AsNumber() <= 0.0) {
            error = name + " has " + std::to_string(read.Frames()) +
                    " frames and no animation.frames_per_second above zero";
            return false;
        }
        read.framesPerSecond = animation["frames_per_second"].AsNumber();
    }
    out = std::move(read);
    return true;
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    Json::Parser parser(text);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    if (!root.IsObject() || !root.Has("portal") || !root.Has("shot") || !root.Has("character")) {
        error = path + ": portal, shot and character are each an object";
        return false;
    }
    Rules read;
    std::string why;
    if (!ReadPicture(root["portal"], "portal", read.portal, why) ||
        !ReadPicture(root["shot"], "shot", read.shot, why) ||
        !ReadPicture(root["character"], "character", static_cast<Picture&>(read.character), why)) {
        error = path + ": " + why;
        return false;
    }
    // What the player's sheet adds: where it starts, where it stands, which row
    // walks which way, and which column it stands on.
    const Json::Value& character = root["character"];
    Character& mage = read.character;
    const Json::Value& pivot = character["pivot_px"];
    const Json::Value& rowsBy = character["rows_by_direction"];
    if (!character.Has("start_frame") || !WholeBelow(character["start_frame"], mage.Frames(), mage.startFrame) ||
        !pivot.IsArray() || pivot.AsArray().size() != 2 || !pivot.AsArray()[0].IsNumber() ||
        !pivot.AsArray()[1].IsNumber() || !rowsBy.IsObject() || !rowsBy.Has("left") ||
        !WholeBelow(rowsBy["left"], mage.rows, mage.leftRow) || !rowsBy.Has("right") ||
        !WholeBelow(rowsBy["right"], mage.rows, mage.rightRow) || !character["animation"].Has("idle_column") ||
        !WholeBelow(character["animation"]["idle_column"], mage.columns, mage.idleColumn)) {
        error = path + ": character needs start_frame, pivot_px, rows_by_direction.left and .right, and "
                       "animation.idle_column, each inside its sheet";
        return false;
    }
    mage.pivotXPx = pivot.AsArray()[0].AsNumber();
    mage.pivotYPx = pivot.AsArray()[1].AsNumber();
    out = std::move(read);
    return true;
}

} // namespace MagicPortals::Art
