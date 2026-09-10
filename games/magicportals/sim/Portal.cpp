#include "sim/Portal.hpp"

#include "core/DetMath.hpp"
#include "core/Json.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

namespace MagicPortals::Portal {

bool LoadTransit(const std::string& path, Transit& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    Supersonic::Json::Parser parser(text);
    Supersonic::Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    if (!root.IsObject() || !root.Has("transit") || !root["transit"].IsObject()) {
        error = path + ": no \"transit\" object";
        return false;
    }
    const Supersonic::Json::Value& transit = root["transit"];

    if (!transit.Has("momentum_mode") || !transit["momentum_mode"].IsString()) {
        error = path + ": transit.momentum_mode is missing or not a string";
        return false;
    }
    const auto number = [&](const char* key, double& field) {
        if (!transit.Has(key) || !transit[key].IsNumber()) {
            error = path + ": transit." + key + " is missing or not a number";
            return false;
        }
        field = transit[key].AsNumber();
        return true;
    };

    Transit read;
    read.momentumMode = transit["momentum_mode"].AsString();
    if (!number("exit_speed_scale", read.exitSpeedScale) || !number("exit_offset_px", read.exitOffsetPx) ||
        !number("reentry_lockout_s", read.reentryLockoutS))
        return false;
    out = read;
    return true;
}

glm::dvec2 ExitVelocity(const glm::dvec2& velocity, double entryRotation, double exitRotation,
                        const Transit& transit) {
    glm::dvec2 out = velocity;
    if (transit.momentumMode == "reset") {
        out = glm::dvec2(0.0);
    } else if (transit.momentumMode == "rotate_to_exit") {
        // Godot's Vector2.rotated: x cos - y sin, x sin + y cos.
        float s = 0.0f, c = 1.0f;
        Supersonic::DetMath::sincos(static_cast<float>(exitRotation - entryRotation), s, c);
        out = glm::dvec2(velocity.x * c - velocity.y * s, velocity.x * s + velocity.y * c);
    }
    return out * transit.exitSpeedScale;
}

glm::dvec2 ExitPosition(const glm::dvec2& exitPortal, const glm::dvec2& exitVelocity,
                        const Transit& transit) {
    // Godot's is_zero_approx: each component under CMP_EPSILON, 0.00001.
    const bool still = std::fabs(exitVelocity.x) < 1e-5 && std::fabs(exitVelocity.y) < 1e-5;
    glm::dvec2 direction(0.0, 1.0); // Vector2.DOWN
    if (!still) {
        const double length = std::sqrt(exitVelocity.x * exitVelocity.x + exitVelocity.y * exitVelocity.y);
        direction = exitVelocity / length;
    }
    return exitPortal + direction * transit.exitOffsetPx;
}

} // namespace MagicPortals::Portal
