#include "sim/MenuState.hpp"

#include "core/Json.hpp"
#include "sim/Hud.hpp"

#include <algorithm>
#include <cmath>

namespace MagicPortals::MenuState {

namespace {

namespace Json = Supersonic::Json;

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    Json::Value root;
    if (!UiLayer::Read::File(path, root, error)) return false;
    const Json::Value& block = root["menu_state"];
    if (!block.IsObject() || !block["fade"].IsObject() || !block["press"].IsObject()) {
        error = path + ": menu_state, menu_state.fade and menu_state.press are each an object";
        return false;
    }
    Rules read;
    std::string why;
    if (!UiLayer::Read::Positive(block["fade"], "fade_ms", read.fadeMs, why, "menu_state.fade") ||
        !UiLayer::Read::Byte(block["press"], "tint_byte", read.pressTintByte, why, "menu_state.press") ||
        !UiLayer::Read::Positive(block["press"], "tile_travel_units", read.tileTravelUnits, why,
                                 "menu_state.press")) {
        error = path + ": " + why;
        return false;
    }
    out = read;
    return true;
}

int FadeAlphaByte(const Rules& rules, double ms) {
    // FadeInController::draw draws only while not finished: elapsed < time.
    if (ms >= rules.fadeMs) return 0;
    const double bias = 1.0 - std::max(ms, 0.0) / rules.fadeMs;
    return static_cast<int>(bias * 255.0);
}

double Triangle(double ms, double strideMs) {
    if (!(strideMs > 0.0)) return 0.0;
    const double t = std::max(ms, 0.0);
    const double strides = std::floor(t / strideMs);
    const double bias = (t - strides * strideMs) / strideMs;
    return std::fmod(strides, 2.0) == 1.0 ? 1.0 - bias : bias;
}

glm::dvec2 BounceScale(const Bounce& bounce, double ms) {
    // Button::bounce does nothing for a button with no bounce set.
    if (bounce.scaleA == glm::dvec2(1.0) && bounce.scaleB == glm::dvec2(1.0)) return glm::dvec2(1.0);
    const double eased = Hud::SmoothEnd(Triangle(ms, bounce.strideMs));
    return bounce.scaleA + (bounce.scaleB - bounce.scaleA) * eased;
}

BlinkValue BlinkAt(const Blink& blink, double ms) {
    BlinkValue value;
    // Button::blinkColor leaves colour and alpha whole for a button with no blink.
    if (blink.colourA == 1.0 && blink.colourB == 1.0 && blink.alphaA == 1.0 && blink.alphaB == 1.0) return value;
    const double bias = Triangle(ms, blink.strideMs);
    value.colour = blink.colourA + (blink.colourB - blink.colourA) * bias;
    value.alpha = blink.alphaA + (blink.alphaB - blink.alphaA) * bias;
    return value;
}

int ChannelByte(int tintByte, int customByte, double blink) {
    const double product = (static_cast<double>(tintByte) / 255.0) * (static_cast<double>(customByte) / 255.0) *
                           std::clamp(blink, 0.0, 1.0);
    return static_cast<int>(std::clamp(product, 0.0, 1.0) * 255.0);
}

Hud::Rect ScaledAbout(const Hud::Rect& rect, const glm::dvec2& origin, const glm::dvec2& scale) {
    const glm::dvec2 pivot = rect.min + rect.size * origin;
    Hud::Rect scaled;
    scaled.size = rect.size * scale;
    scaled.min = pivot - scaled.size * origin;
    return scaled;
}

void TouchDown(Touch& touch, const glm::dvec2& at) {
    touch.down = true;
    touch.downAt = at;
    touch.maxTravelUnits = 0.0;
}

void TouchHeld(Touch& touch, const glm::dvec2& at) {
    if (!touch.down) return;
    touch.maxTravelUnits = std::max(touch.maxTravelUnits, glm::length(at - touch.downAt));
}

bool TileTakes(const Rules& rules, const Touch& touch) {
    // Page::update: setPressed(false) once getLastMaxTouchGap > scale(48).
    return !(touch.maxTravelUnits > rules.tileTravelUnits);
}

} // namespace MagicPortals::MenuState
