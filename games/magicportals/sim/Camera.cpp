#include "sim/Camera.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace MagicPortals::Camera {

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    namespace Json = Supersonic::Json;
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
    if (!root.IsObject() || !root.Has("camera") || !root["camera"].IsObject()) {
        error = path + ": no \"camera\" object";
        return false;
    }
    const Json::Value& camera = root["camera"];
    const auto number = [&](const char* key, double& to) {
        if (!camera.Has(key) || !camera[key].IsNumber()) {
            error = path + ": camera." + key + " is missing or not a number";
            return false;
        }
        to = camera[key].AsNumber();
        return true;
    };
    Rules read;
    if (!number("follow_lag_s", read.followLagS) || !number("hold_time_s", read.holdTimeS)) return false;
    out = read;
    return true;
}

glm::dvec2 Clamp(const glm::dvec2& wantPx, const glm::dvec2& viewPx, const glm::dvec2& boundsPx) {
    glm::dvec2 centre(0.0);
    for (int axis = 0; axis < 2; ++axis) {
        const double half = viewPx[axis] * 0.5;
        centre[axis] = viewPx[axis] >= boundsPx[axis] ? boundsPx[axis] * 0.5
                                                      : std::clamp(wantPx[axis], half, boundsPx[axis] - half);
    }
    return centre;
}

void Follow::Start(const Rules& rules, const glm::dvec2& cameraStartPx, const glm::dvec2& viewPx,
                   const glm::dvec2& boundsPx) {
    centrePx = Clamp(cameraStartPx, viewPx, boundsPx);
    holdLeftS = rules.holdTimeS;
}

void Follow::Tick(const Rules& rules, const glm::dvec2& playerPx, const glm::dvec2& viewPx,
                  const glm::dvec2& boundsPx, double dt) {
    centrePx = Clamp(centrePx, viewPx, boundsPx);
    if (holdLeftS > 0.0) {
        holdLeftS = std::max(0.0, holdLeftS - dt);
        return;
    }
    // main.gd:134-137, per tick. Eased toward where the view may go, so the
    // camera never leaves the level on its way.
    const double share = std::clamp(dt / std::max(rules.followLagS, 0.001), 0.0, 1.0);
    centrePx += (Clamp(playerPx, viewPx, boundsPx) - centrePx) * share;
}

} // namespace MagicPortals::Camera
