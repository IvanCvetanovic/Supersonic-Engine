#include "sim/Loading.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MagicPortals::Loading {

namespace {

namespace Json = Supersonic::Json;
using UiLayer::Read::Byte;
using UiLayer::Read::Fraction;
using UiLayer::Read::Positive;
using UiLayer::Read::ReadPlaced;
using UiLayer::Read::Size;
using UiLayer::Read::Text;

// A whole number of at least `least`.
bool Whole(const Json::Value& block, const char* key, int least, int& out, std::string& why,
           const std::string& where) {
    const Json::Value& value = block[key];
    if (!value.IsNumber() || value.AsNumber() != std::floor(value.AsNumber()) ||
        value.AsNumber() < static_cast<double>(least) || value.AsNumber() > 1.0e6) {
        why = where + "." + key + " is missing or not a whole number of at least " + std::to_string(least);
        return false;
    }
    out = static_cast<int>(value.AsNumber());
    return true;
}

// A frame's worth of milliseconds summed `ticks` times, to the microsecond: 60
// ticks of 1000 / 60 are 1000 ms, whether the tick is the double 1000 / 60 or the
// float 1 / 60 the engine's clock holds (16.666667461 ms), not a hair over it.
double TicksMs(int ticks, double tickMs) {
    return std::round(static_cast<double>(ticks) * tickMs * 1.0e3) / 1.0e3;
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    Json::Value root;
    if (!UiLayer::Read::File(path, root, error)) return false;
    const Json::Value& loading = root["loading"];
    if (!loading.IsObject()) {
        error = path + ": loading is not an object";
        return false;
    }
    const Json::Value& background = loading["background"];
    const Json::Value& character = loading["character"];
    const Json::Value& portal = loading["portal"];
    const Json::Value& load = loading["load"];
    const Json::Value& dots = loading["dots"];
    const Json::Value& vanish = loading["vanish"];
    if (!background.IsObject() || !character.IsObject() || !portal.IsObject() || !load.IsObject() ||
        !dots.IsObject() || !vanish.IsObject()) {
        error = path + ": loading.background, character, portal, vanish, load and dots are each an object";
        return false;
    }
    Rules read;
    std::string why;
    if (!Text(background, "sprite", read.background.sprite, why, "loading.background") ||
        !Size(background, "size_units", read.background.sizeUnits, why, "loading.background") ||
        !Fraction(background, "centre_of_screen", read.background.centreOfScreen, why, "loading.background") ||
        !Fraction(character, "start_of_screen", read.characterStartOfScreen, why, "loading.character") ||
        !Whole(character, "first_frame", 0, read.firstFrame, why, "loading.character") ||
        !Whole(character, "last_frame", 0, read.lastFrame, why, "loading.character") ||
        !Positive(character, "stride_ms", read.strideMs, why, "loading.character") ||
        !Fraction(portal, "at_screen", read.portalAtScreen, why, "loading.portal") ||
        !Text(portal, "entity", read.portalEntity, why, "loading.portal") ||
        !Text(portal, "halo_sprite", read.haloSprite, why, "loading.portal") ||
        !Positive(portal, "halo_scale", read.haloScale, why, "loading.portal") ||
        !Text(vanish, "suck_entity", read.vanish.suckEntity, why, "loading.vanish") ||
        !Positive(vanish, "suck_offset_units", read.vanish.suckOffsetUnits, why, "loading.vanish") ||
        !Text(vanish, "sparkles_entity", read.vanish.sparklesEntity, why, "loading.vanish") ||
        !Whole(load, "resources", 1, read.resources, why, "loading.load") ||
        !Whole(load, "resources_per_frame", 1, read.resourcesPerFrame, why, "loading.load") ||
        !Positive(load, "hold_ms", read.holdMs, why, "loading.load") ||
        !ReadPlaced(loading["logo"], "sprite", read.logo, why, "loading.logo") ||
        !Text(dots, "font", read.dots.font, why, "loading.dots") ||
        !Positive(dots, "units_per_font_px", read.dots.unitsPerFontPx, why, "loading.dots") ||
        !Fraction(dots, "centre_of_screen", read.dots.centreOfScreen, why, "loading.dots") ||
        !Whole(dots, "columns", 1, read.dots.columns, why, "loading.dots") ||
        !Whole(dots, "run", 1, read.dots.run, why, "loading.dots") ||
        !Whole(dots, "strings_per_pattern", 1, read.dots.stringsPerPattern, why, "loading.dots") ||
        !Whole(dots, "track_dots", 0, read.dots.trackDots, why, "loading.dots") ||
        !Byte(dots, "track_alpha_byte", read.dots.trackAlphaByte, why, "loading.dots")) {
        error = path + ": " + why;
        return false;
    }
    const Json::Value& angle = vanish["suck_angle_offset_deg"];
    if (!angle.IsNumber()) {
        error = path + ": loading.vanish.suck_angle_offset_deg is missing or not a number";
        return false;
    }
    read.vanish.suckAngleOffsetDeg = angle.AsNumber();
    if (read.lastFrame < read.firstFrame) {
        error = path + ": loading.character.last_frame is before first_frame";
        return false;
    }
    out = std::move(read);
    return true;
}

int LoadedBefore(const Rules& rules, int frame) {
    const long long loaded = static_cast<long long>(std::max(frame - 1, 0)) * rules.resourcesPerFrame;
    return static_cast<int>(std::min<long long>(loaded, rules.resources));
}

int LastLoadingFrame(const Rules& rules) {
    return (rules.resources + rules.resourcesPerFrame - 1) / rules.resourcesPerFrame;
}

bool IsLoading(const Rules& rules, int frame) {
    return frame >= 1 && LoadedBefore(rules, frame) < rules.resources;
}

bool CharacterShown(const Rules& rules, int frame) {
    return frame < LastLoadingFrame(rules);
}

glm::dvec2 PortalAt(const Rules& rules, const glm::dvec2& viewUnits) {
    return rules.portalAtScreen * viewUnits;
}

glm::dvec2 CharacterAt(const Rules& rules, const glm::dvec2& viewUnits, int frame) {
    // The last loading frame is the last that moves it.
    const int moved = std::clamp(frame, 1, LastLoadingFrame(rules));
    const double bias = static_cast<double>(LoadedBefore(rules, moved)) / static_cast<double>(rules.resources);
    const glm::dvec2 start = rules.characterStartOfScreen * viewUnits;
    return start + (PortalAt(rules, viewUnits) - start) * bias;
}

namespace {

// vanishEffect's direction: from the portal to where the character stands.
glm::dvec2 VanishDirection(const Rules& rules, const glm::dvec2& viewUnits) {
    const glm::dvec2 d = CharacterAt(rules, viewUnits, LastLoadingFrame(rules)) - PortalAt(rules, viewUnits);
    const double length = glm::length(d);
    return length > 0.0 ? d / length : glm::dvec2(0.0);
}

} // namespace

glm::dvec2 SuckAt(const Rules& rules, const glm::dvec2& viewUnits) {
    return PortalAt(rules, viewUnits) + VanishDirection(rules, viewUnits) * rules.vanish.suckOffsetUnits;
}

double SuckAngleDeg(const Rules& rules, const glm::dvec2& viewUnits) {
    const glm::dvec2 d = VanishDirection(rules, viewUnits);
    constexpr double kPi = 3.14159265358979323846;
    double radians = std::atan2(d.x, d.y);
    if (radians < 0.0) radians += 2.0 * kPi;
    return radians * 180.0 / kPi + rules.vanish.suckAngleOffsetDeg;
}

glm::dvec2 SparklesAt(const Rules& rules, const glm::dvec2& viewUnits) {
    return CharacterAt(rules, viewUnits, LastLoadingFrame(rules));
}

int CharacterFrame(const Rules& rules, int frame, double tickMs) {
    // The first call resets the timer to 0; every later one adds a frame and
    // steps once past the stride. A frame is shorter than the stride, so never
    // more than one step a call, and the steps are the stride's multiples.
    const int calls = std::clamp(frame, 1, LastLoadingFrame(rules)) - 1;
    const long long steps = static_cast<long long>(std::floor(TicksMs(calls, tickMs) / rules.strideMs));
    const int count = rules.lastFrame - rules.firstFrame + 1;
    return rules.firstFrame + static_cast<int>(steps % count);
}

std::string DotsText(const Rules& rules, int loaded) {
    const Rules::Dots& d = rules.dots;
    const int patterns = d.columns + d.run;
    const int strings = patterns * d.stringsPerPattern;
    const int pattern = (std::max(loaded, 0) % strings) / d.stringsPerPattern;
    std::string text(static_cast<std::size_t>(d.columns), ' ');
    for (int c = std::max(pattern - d.run, 0); c <= pattern - 1 && c < d.columns; ++c) {
        text[static_cast<std::size_t>(c)] = '.';
    }
    return text;
}

std::string TrackText(const Rules& rules) {
    return std::string(static_cast<std::size_t>(rules.dots.trackDots), '.');
}

bool HoldOver(const Rules& rules, int frame, double tickMs) {
    const int held = frame - LastLoadingFrame(rules);
    return held >= 1 && TicksMs(held, tickMs) > rules.holdMs;
}

int MenuFrame(const Rules& rules, double tickMs) {
    int frame = LastLoadingFrame(rules) + 1;
    while (!HoldOver(rules, frame, tickMs)) ++frame;
    return frame;
}

} // namespace MagicPortals::Loading
