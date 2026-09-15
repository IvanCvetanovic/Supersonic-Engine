#include "sim/UiLayer.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace MagicPortals::UiLayer {

namespace {

namespace Json = Supersonic::Json;

bool Positive(const Json::Value& block, const char* key, double& out, std::string& why, const std::string& where) {
    const Json::Value& value = block[key];
    if (!value.IsNumber() || !(value.AsNumber() > 0.0)) {
        why = where + "." + key + " is missing, not a number, or not above zero";
        return false;
    }
    out = value.AsNumber();
    return true;
}

// fTOu then iTOb: the byte a float fraction of 255 becomes in the original's
// colours, truncated.
int ByteOf(double fraction, int ofByte) {
    return static_cast<int>(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(ofByte));
}

// InterpolationTimer's bias: min(max(elapsed / time, 0), 1).
double Bias(double ms, double strideMs) {
    return std::clamp(ms / strideMs, 0.0, 1.0);
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
    const Json::Value& layer = root["ui_layer"];
    if (!layer.IsObject() || !layer["sprite"].IsObject() || !layer["button"].IsObject()) {
        error = path + ": ui_layer, ui_layer.sprite and ui_layer.button are each an object";
        return false;
    }
    Rules read;
    std::string why;
    if (!Positive(layer["sprite"], "appear_ms", read.spriteAppearMs, why, "ui_layer.sprite") ||
        !Positive(layer["button"], "appear_ms", read.buttonAppearMs, why, "ui_layer.button") ||
        !Positive(layer["button"], "dismiss_ms", read.buttonDismissMs, why, "ui_layer.button") ||
        !Positive(layer["button"], "slide_units", read.buttonSlideUnits, why, "ui_layer.button")) {
        error = path + ": " + why;
        return false;
    }
    out = read;
    return true;
}

glm::dvec2 Anchor(const Placed& placed, const glm::dvec2& viewUnits) {
    return placed.atScreen * viewUnits;
}

Hud::Rect RectAt(const Placed& placed, const glm::dvec2& anchor) {
    return Hud::Rect{anchor - placed.sizeUnits * placed.origin, placed.sizeUnits};
}

int SpriteAlphaByte(const Rules& rules, int tintAlphaByte, double ms) {
    return ByteOf(Hud::SmoothEnd(Bias(ms, rules.spriteAppearMs)), tintAlphaByte);
}

int ButtonAlphaByte(const Rules& rules, double ms) {
    return ByteOf(Bias(ms, rules.buttonAppearMs), 255);
}

glm::dvec2 ButtonStart(const Rules& rules, const glm::dvec2& anchor, const glm::dvec2& viewUnits) {
    const glm::dvec2 ray = anchor - viewUnits * 0.5;
    const double length = glm::length(ray);
    if (length <= 0.0) return anchor;
    return anchor + ray / length * rules.buttonSlideUnits;
}

glm::dvec2 ButtonAnchorAt(const Rules& rules, const glm::dvec2& anchor, const glm::dvec2& viewUnits, double ms) {
    // PositionInterpolator::getCurrentPos: the end itself once elapsed passes
    // the stride (a strict compare), and smoothEnd of the clamped bias before.
    if (ms > rules.buttonAppearMs) return anchor;
    const glm::dvec2 start = ButtonStart(rules, anchor, viewUnits);
    return start + (anchor - start) * Hud::SmoothEnd(Bias(ms, rules.buttonAppearMs));
}

int ButtonDismissAlphaByte(const Rules& rules, double ms) {
    if (ms >= rules.buttonDismissMs) return 0;
    return ByteOf(1.0 - Hud::SmoothEnd(Bias(ms, rules.buttonDismissMs)), 255);
}

glm::dvec2 ButtonDismissAnchorAt(const Rules& rules, const glm::dvec2& anchor, const glm::dvec2& viewUnits,
                                 double ms) {
    const glm::dvec2 start = ButtonStart(rules, anchor, viewUnits);
    if (ms > rules.buttonDismissMs) return start;
    return anchor + (start - anchor) * Hud::SmoothEnd(Bias(ms, rules.buttonDismissMs));
}

} // namespace MagicPortals::UiLayer
