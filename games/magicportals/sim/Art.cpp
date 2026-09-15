#include "sim/Art.hpp"

#include "core/DetMath.hpp"
#include "core/Json.hpp"

#include <cmath>
#include <cstddef>
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

bool Pair(const Json::Value& value, glm::dvec2& out) {
    if (!value.IsArray() || value.AsArray().size() != 2 || !value.AsArray()[0].IsNumber() ||
        !value.AsArray()[1].IsNumber()) {
        return false;
    }
    out = glm::dvec2(value.AsArray()[0].AsNumber(), value.AsArray()[1].AsNumber());
    return true;
}

// Three numbers, none below zero: an .ent's <EmissiveColor> r, g and b.
bool Emissive(const Json::Value& value, glm::dvec3& out) {
    if (!value.IsArray() || value.AsArray().size() != 3) return false;
    for (std::size_t i = 0; i < 3; ++i) {
        const Json::Value& channel = value.AsArray()[i];
        if (!channel.IsNumber() || !(channel.AsNumber() >= 0.0)) return false;
        out[static_cast<glm::length_t>(i)] = channel.AsNumber();
    }
    return true;
}

// `plays` false for a sheet whose frames are chosen rather than played, which
// then need not say how fast.
bool ReadPicture(const Json::Value& entry, const std::string& name, Picture& out, std::string& error,
                 bool plays = true) {
    if (!entry.IsObject() || !entry.Has("sprite") || !entry["sprite"].IsString() || !entry.Has("additive") ||
        !entry["additive"].IsBool()) {
        error = name + " needs sprite and additive";
        return false;
    }
    Picture read;
    read.sprite = entry["sprite"].AsString("");
    read.additive = entry["additive"].AsBool();
    // Required: a picture without one would be drawn at the ambient alone, which
    // for the player on a dark level is black - a default nobody decoded.
    if (!Emissive(entry["emissive"], read.emissive)) {
        error = name + "'s emissive is not three numbers, none below zero";
        return false;
    }
    if ((entry.Has("columns") && !WholeAtLeastOne(entry["columns"], read.columns)) ||
        (entry.Has("rows") && !WholeAtLeastOne(entry["rows"], read.rows))) {
        error = name + "'s columns and rows are whole numbers from 1";
        return false;
    }
    // A sheet of more than one frame plays, so it has to say how fast.
    if (plays && read.Frames() > 1) {
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

bool ReadPulse(const Json::Value& entry, Pulse& out) {
    Pulse read;
    if (!entry.IsObject() || !Pair(entry["from"], read.fromScale) || !Pair(entry["to"], read.toScale) ||
        !entry["stride_ms"].IsNumber() || entry["stride_ms"].AsNumber() <= 0.0) {
        return false;
    }
    read.strideMs = entry["stride_ms"].AsNumber();
    out = read;
    return true;
}

} // namespace

glm::dvec2 Pulse::ScaleAt(double elapsedMs) const {
    // bounce(): the count of strides so far says which way it is going, and how
    // far through this one it is, eased by smoothEnd, says where.
    const double strides = std::floor(elapsedMs / strideMs);
    double bias = (elapsedMs - strides * strideMs) / strideMs;
    if (std::fmod(strides, 2.0) == 1.0) bias = 1.0 - bias;
    const double eased = Supersonic::DetMath::sin(static_cast<float>(bias) * 1.570796327f);
    return fromScale + (toScale - fromScale) * eased;
}

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

    // Chapter 1's boss and its spikes, whose frames are chosen, not played.
    if (!root.Has("beholder") || !root.Has("spike")) {
        error = path + ": beholder and spike are each an object";
        return false;
    }
    if (!ReadPicture(root["beholder"], "beholder", static_cast<Picture&>(read.beholder), why, false) ||
        !ReadPicture(root["spike"], "spike", static_cast<Picture&>(read.spike), why, false)) {
        error = path + ": " + why;
        return false;
    }
    const Json::Value& pulse = root["beholder"]["pulse"];
    if (!pulse.IsObject() || !ReadPulse(pulse["seeking"], read.beholder.seeking) ||
        !ReadPulse(pulse["hurt"], read.beholder.hurt) || !ReadPulse(pulse["dead"], read.beholder.dead)) {
        error = path + ": beholder.pulse needs seeking, hurt and dead, each with from, to and a stride_ms above 0";
        return false;
    }
    glm::dvec2 spikePivot(0.0);
    if (!Pair(root["spike"]["pivot_px"], spikePivot)) {
        error = path + ": spike.pivot_px is not two numbers";
        return false;
    }
    read.spike.pivotXPx = spikePivot.x;
    read.spike.pivotYPx = spikePivot.y;
    out = std::move(read);
    return true;
}

} // namespace MagicPortals::Art
