#include "sim/UiLayer.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace MagicPortals::UiLayer {

namespace {

namespace Json = Supersonic::Json;

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
    Json::Value root;
    if (!Read::File(path, root, error)) return false;
    const Json::Value& layer = root["ui_layer"];
    if (!layer.IsObject() || !layer["sprite"].IsObject() || !layer["button"].IsObject()) {
        error = path + ": ui_layer, ui_layer.sprite and ui_layer.button are each an object";
        return false;
    }
    Rules read;
    std::string why;
    if (!Read::Positive(layer["sprite"], "appear_ms", read.spriteAppearMs, why, "ui_layer.sprite") ||
        !Read::Positive(layer["button"], "appear_ms", read.buttonAppearMs, why, "ui_layer.button") ||
        !Read::Positive(layer["button"], "dismiss_ms", read.buttonDismissMs, why, "ui_layer.button") ||
        !Read::Positive(layer["button"], "slide_units", read.buttonSlideUnits, why, "ui_layer.button")) {
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

namespace Read {

bool File(const std::string& path, Json::Value& root, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    Json::Parser parser(text);
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    return true;
}

bool Text(const Json::Value& block, const char* key, std::string& out, std::string& why, const std::string& where) {
    const Json::Value& value = block[key];
    if (!value.IsString() || value.AsString().empty()) {
        why = where + "." + key + " is missing or not a non-empty string";
        return false;
    }
    out = value.AsString();
    return true;
}

bool Pair(const Json::Value& block, const char* key, glm::dvec2& out, std::string& why, const std::string& where) {
    const Json::Value& value = block[key];
    if (!value.IsArray() || value.AsArray().size() != 2 || !value.AsArray()[0].IsNumber() ||
        !value.AsArray()[1].IsNumber()) {
        why = where + "." + key + " is not a pair of numbers";
        return false;
    }
    out = glm::dvec2(value.AsArray()[0].AsNumber(), value.AsArray()[1].AsNumber());
    return true;
}

bool Size(const Json::Value& block, const char* key, glm::dvec2& out, std::string& why, const std::string& where) {
    glm::dvec2 read(0.0);
    if (!Pair(block, key, read, why, where)) return false;
    if (!(read.x > 0.0) || !(read.y > 0.0)) {
        why = where + "." + key + " is not above zero";
        return false;
    }
    out = read;
    return true;
}

bool Fraction(const Json::Value& block, const char* key, glm::dvec2& out, std::string& why,
              const std::string& where) {
    glm::dvec2 read(0.0);
    if (!Pair(block, key, read, why, where)) return false;
    if (read.x < 0.0 || read.x > 1.0 || read.y < 0.0 || read.y > 1.0) {
        why = where + "." + key + " is not within 0..1";
        return false;
    }
    out = read;
    return true;
}

bool Positive(const Json::Value& block, const char* key, double& out, std::string& why, const std::string& where) {
    const Json::Value& value = block[key];
    if (!value.IsNumber() || !(value.AsNumber() > 0.0)) {
        why = where + "." + key + " is missing, not a number, or not above zero";
        return false;
    }
    out = value.AsNumber();
    return true;
}

bool Byte(const Json::Value& block, const char* key, int& out, std::string& why, const std::string& where) {
    const Json::Value& value = block[key];
    if (!value.IsNumber() || value.AsNumber() < 0.0 || value.AsNumber() > 255.0 ||
        value.AsNumber() != static_cast<double>(static_cast<int>(value.AsNumber()))) {
        why = where + "." + key + " is missing or not a whole number within 0..255";
        return false;
    }
    out = static_cast<int>(value.AsNumber());
    return true;
}

bool ReadPlaced(const Json::Value& block, const char* spriteKey, Placed& out, std::string& why,
                const std::string& where) {
    if (!block.IsObject()) {
        why = where + " is not an object";
        return false;
    }
    Placed read;
    if (!Text(block, spriteKey, read.sprite, why, where) || !Fraction(block, "at_screen", read.atScreen, why, where) ||
        !Fraction(block, "origin", read.origin, why, where) || !Size(block, "size_units", read.sizeUnits, why, where)) {
        return false;
    }
    out = std::move(read);
    return true;
}

} // namespace Read

} // namespace MagicPortals::UiLayer
