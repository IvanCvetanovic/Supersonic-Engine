#include "sim/Hud.hpp"

#include "core/BitmapFont.hpp"
#include "core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace MagicPortals::Hud {

namespace {

namespace Json = Supersonic::Json;

constexpr double kHalfPi = 1.5707963267948966;

// A number that must be there, and at or above `atLeast`.
bool Number(const Json::Value& block, const char* key, double atLeast, double& out, std::string& why,
            const std::string& where) {
    const Json::Value& value = block[key];
    if (!value.IsNumber() || value.AsNumber() < atLeast) {
        why = where + "." + key + " is missing, not a number, or below " + std::to_string(atLeast);
        return false;
    }
    out = value.AsNumber();
    return true;
}

bool Whole(const Json::Value& block, const char* key, int atLeast, int& out, std::string& why,
           const std::string& where) {
    double n = 0.0;
    if (!Number(block, key, static_cast<double>(atLeast), n, why, where)) return false;
    if (n != std::floor(n)) {
        why = where + "." + key + " is not a whole number";
        return false;
    }
    out = static_cast<int>(n);
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

bool ReadAnchor(const Json::Value& block, Anchor& out, std::string& why, const std::string& where) {
    const std::string name = block["anchor"].AsString("");
    if (name == "top_left") {
        out = Anchor::TopLeft;
    } else if (name == "top_right") {
        out = Anchor::TopRight;
    } else if (name == "bottom_left") {
        out = Anchor::BottomLeft;
    } else if (name == "bottom_right") {
        out = Anchor::BottomRight;
    } else {
        why = where + ".anchor is not one of top_left, top_right, bottom_left, bottom_right";
        return false;
    }
    return true;
}

bool ReadPlacement(const Json::Value& block, Placement& out, std::string& why, const std::string& where) {
    if (!block.IsObject()) {
        why = where + " is not an object";
        return false;
    }
    Placement read;
    if (!Text(block, "sprite", read.sprite, why, where) || !ReadAnchor(block, read.anchor, why, where) ||
        !Pair(block, "inset_units", read.insetUnits, why, where) ||
        !Pair(block, "size_units", read.sizeUnits, why, where)) {
        return false;
    }
    if (read.insetUnits.x < 0.0 || read.insetUnits.y < 0.0 || read.sizeUnits.x <= 0.0 || read.sizeUnits.y <= 0.0) {
        why = where + " has a negative inset or a size that is not above zero";
        return false;
    }
    out = read;
    return true;
}

// uint(bias * 255) and back: the original builds every one of these colours
// through ARGB with a byte, so an alpha is only ever a 255th.
double Byte(double fraction) {
    return static_cast<double>(static_cast<int>(std::clamp(fraction, 0.0, 1.0) * 255.0)) / 255.0;
}

} // namespace

bool Rect::Contains(const glm::dvec2& point) const {
    return point.x >= min.x && point.y >= min.y && point.x <= min.x + size.x && point.y <= min.y + size.y;
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
    const Json::Value& hud = root["hud"];
    const Json::Value& start = root["level_start"];
    if (!hud.IsObject() || !start.IsObject()) {
        error = path + ": hud and level_start are each an object";
        return false;
    }

    Rules read;
    std::string why;
    const auto fail = [&error, &path, &why]() {
        error = path + ": " + why;
        return false;
    };

    if (!Whole(hud["opacity"], "alpha_byte", 0, read.alphaByte, why, "hud.opacity") || read.alphaByte > 255) {
        if (why.empty()) why = "hud.opacity.alpha_byte is above 255";
        return fail();
    }
    if (!ReadPlacement(hud["restart"], read.restart, why, "hud.restart") ||
        !ReadPlacement(hud["pause"], read.pause, why, "hud.pause") ||
        !ReadPlacement(hud["clear_portals"], read.clearPortals, why, "hud.clear_portals")) {
        return fail();
    }

    const Json::Value& pads = hud["pads"];
    Rules::Pads& p = read.pads;
    if (!pads.IsObject() || !Text(pads, "left_sprite", p.leftSprite, why, "hud.pads") ||
        !Text(pads, "right_sprite", p.rightSprite, why, "hud.pads") ||
        !Number(pads, "size_units", 1e-9, p.sizeUnits, why, "hud.pads") ||
        !Number(pads["hit"], "radius_units", 1e-9, p.hitRadiusUnits, why, "hud.pads.hit") ||
        !Number(pads["slide_in"], "from_units", 0.0, p.slideFromUnits, why, "hud.pads.slide_in") ||
        !Number(pads["slide_in"], "ms", 1e-9, p.slideMs, why, "hud.pads.slide_in")) {
        if (why.empty()) why = "hud.pads is not an object";
        return fail();
    }
    const Json::Value& pulse = pads["pulse"];
    if (!Number(pulse, "stride_ms", 1e-9, p.strideMs, why, "hud.pads.pulse") ||
        !Whole(pulse, "strides", 0, p.strides, why, "hud.pads.pulse") ||
        !Whole(pulse, "variation_byte", 0, p.variationByte, why, "hud.pads.pulse") ||
        !Text(pulse, "tutorial_level", p.tutorialLevel, why, "hud.pads.pulse") ||
        !Whole(pulse, "tutorial_strides", 0, p.tutorialStrides, why, "hud.pads.pulse") ||
        !Whole(pulse, "tutorial_variation_byte", 0, p.tutorialVariationByte, why, "hud.pads.pulse")) {
        return fail();
    }
    // A pulse that carried the alpha past a byte would wrap in the original's
    // iTOb and flash dark; refused here rather than reproduced.
    if (read.alphaByte + std::max(p.variationByte, p.tutorialVariationByte) > 255) {
        why = "hud.pads.pulse carries the alpha above 255";
        return fail();
    }
    const Json::Value& ring = pads["ring"];
    if (!Text(ring, "sprite", p.ringSprite, why, "hud.pads.ring") ||
        !Number(ring, "stride_ms", 1e-9, p.ringStrideMs, why, "hud.pads.ring") ||
        !Number(ring, "size_units", 1e-9, p.ringSizeUnits, why, "hud.pads.ring") ||
        !Number(ring, "min_bias", 0.0, p.ringMinBias, why, "hud.pads.ring")) {
        return fail();
    }

    const Json::Value& overlay = start["overlay"];
    if (!Number(overlay, "fade_ms", 1e-9, read.overlay.fadeMs, why, "level_start.overlay") ||
        !Whole(overlay, "layers", 1, read.overlay.layers, why, "level_start.overlay") ||
        !Whole(overlay, "layers_over_pads", 0, read.overlay.layersOverPads, why, "level_start.overlay") ||
        !Number(overlay, "start_after_ms", 0.0, read.overlay.startAfterMs, why, "level_start.overlay")) {
        return fail();
    }
    if (read.overlay.layersOverPads > read.overlay.layers) {
        why = "level_start.overlay.layers_over_pads is more than its layers";
        return fail();
    }

    const Json::Value& caption = start["caption"];
    Rules::Caption& c = read.caption;
    if (!Text(caption, "font", c.font, why, "level_start.caption") ||
        !Text(caption, "prefix", c.prefix, why, "level_start.caption") ||
        !Number(caption, "units_per_font_px", 1e-9, c.unitsPerFontPx, why, "level_start.caption") ||
        !Pair(caption, "centre_of_view", c.centreOfView, why, "level_start.caption") ||
        !Number(caption, "fade_ms", 1e-9, c.fadeMs, why, "level_start.caption")) {
        return fail();
    }

    const Json::Value& sign = hud["no_portal_sign"];
    if (!sign.IsObject() || !Text(sign, "entity", read.sign.entity, why, "hud.no_portal_sign") ||
        !Number(sign, "follow_ms", 1e-9, read.sign.followMs, why, "hud.no_portal_sign") ||
        !Number(sign, "retarget_ms", 0.0, read.sign.retargetMs, why, "hud.no_portal_sign") ||
        !Number(sign, "centre_in_by_size", 0.0, read.sign.centreInBySize, why, "hud.no_portal_sign")) {
        if (why.empty()) why = "hud.no_portal_sign is not an object";
        return fail();
    }

    const Json::Value& plaque = start["plaque"];
    Rules::Plaque& q = read.plaque;
    if (!Text(plaque, "sprite", q.sprite, why, "level_start.plaque") ||
        !Pair(plaque, "centre_units", q.centreUnits, why, "level_start.plaque") ||
        !Pair(plaque, "size_units", q.sizeUnits, why, "level_start.plaque") ||
        !Pair(plaque, "medal_centre_units", q.medalCentreUnits, why, "level_start.plaque") ||
        !Pair(plaque, "medal_size_units", q.medalSizeUnits, why, "level_start.plaque") ||
        !Text(plaque, "medal_bronze", q.medalBronze, why, "level_start.plaque") ||
        !Text(plaque, "medal_silver", q.medalSilver, why, "level_start.plaque") ||
        !Text(plaque, "medal_gold", q.medalGold, why, "level_start.plaque") ||
        !Number(plaque, "appear_ms", 1e-9, q.appearMs, why, "level_start.plaque") ||
        !Number(plaque, "dismiss_after_ms", 0.0, q.dismissAfterMs, why, "level_start.plaque") ||
        !Number(plaque, "dismiss_ms", 1e-9, q.dismissMs, why, "level_start.plaque")) {
        return fail();
    }

    out = std::move(read);
    return true;
}

double SmoothEnd(double v) {
    return std::sin(v * kHalfPi);
}

double Opacity(const Rules& rules) {
    return static_cast<double>(rules.alphaByte) / 255.0;
}

Rect Place(const Placement& placement, const glm::dvec2& viewUnits) {
    const bool right = placement.anchor == Anchor::TopRight || placement.anchor == Anchor::BottomRight;
    const bool bottom = placement.anchor == Anchor::BottomLeft || placement.anchor == Anchor::BottomRight;
    Rect rect;
    rect.size = placement.sizeUnits;
    rect.min.x = right ? viewUnits.x - placement.insetUnits.x - placement.sizeUnits.x : placement.insetUnits.x;
    rect.min.y = bottom ? viewUnits.y - placement.insetUnits.y - placement.sizeUnits.y : placement.insetUnits.y;
    return rect;
}

double PadSlideUnits(const Rules& rules, double ageMs) {
    const double bias = std::clamp(ageMs / rules.pads.slideMs, 0.0, 1.0);
    return rules.pads.slideFromUnits * (1.0 - SmoothEnd(bias));
}

glm::dvec2 PadCorner(const Rules& rules, Side side, const glm::dvec2& viewUnits, double ageMs) {
    const double slide = PadSlideUnits(rules, ageMs);
    return side == Side::Left ? glm::dvec2(-slide, viewUnits.y) : glm::dvec2(viewUnits.x + slide, viewUnits.y);
}

Rect PadRect(const Rules& rules, Side side, const glm::dvec2& viewUnits, double ageMs) {
    const glm::dvec2 corner = PadCorner(rules, side, viewUnits, ageMs);
    Rect rect;
    rect.size = glm::dvec2(rules.pads.sizeUnits);
    rect.min = glm::dvec2(side == Side::Left ? corner.x : corner.x - rules.pads.sizeUnits,
                          corner.y - rules.pads.sizeUnits);
    return rect;
}

bool OnPad(const Rules& rules, Side side, const glm::dvec2& viewUnits, double ageMs, const glm::dvec2& point) {
    const glm::dvec2 d = point - PadCorner(rules, side, viewUnits, ageMs);
    // isPointInSphere: strictly inside.
    return glm::dot(d, d) < rules.pads.hitRadiusUnits * rules.pads.hitRadiusUnits;
}

double PadOpacity(const Rules& rules, double ageMs, bool tutorial) {
    return static_cast<double>(PadAlphaByte(rules, ageMs, tutorial)) / 255.0;
}

int PadAlphaByte(const Rules& rules, double ageMs, bool tutorial) {
    const Rules::Pads& pads = rules.pads;
    const int strides = tutorial ? pads.tutorialStrides : pads.strides;
    const int variation = tutorial ? pads.tutorialVariationByte : pads.variationByte;
    int alpha = rules.alphaByte;
    if (ageMs >= 0.0 && ageMs < pads.strideMs * static_cast<double>(strides)) {
        const double stride = std::floor(ageMs / pads.strideMs);
        double bias = (ageMs - stride * pads.strideMs) / pads.strideMs;
        // Every odd stride runs back down: a triangle two strides long.
        if (std::fmod(stride, 2.0) == 1.0) bias = 1.0 - bias;
        alpha += static_cast<int>(bias * static_cast<double>(variation));
    }
    return alpha;
}

Ring RingAt(const Rules& rules, double ageMs, bool tutorial) {
    const Rules::Pads& pads = rules.pads;
    Ring ring;
    // Drawn from inside the pulse's own branch, so it stops when the pulse does.
    if (!tutorial || ageMs < 0.0 || ageMs >= pads.strideMs * static_cast<double>(pads.tutorialStrides)) return ring;
    const double bias = std::fmod(ageMs, pads.ringStrideMs) / pads.ringStrideMs;
    ring.shown = true;
    ring.sizeUnits = pads.ringSizeUnits * std::max(bias, pads.ringMinBias);
    ring.alpha = Byte(1.0 - bias);
    return ring;
}

double OverlayLayersAlpha(const Rules& rules, double ageMs, int count) {
    // The blacks' own age: GetTime() since they were built, which in the
    // original is after the load the level's frame-time clock has already
    // counted. Before it they are whole - there is no earlier picture of them.
    const double blackMs = ageMs - rules.overlay.startAfterMs;
    if (blackMs >= rules.overlay.fadeMs || count <= 0) return 0.0;
    // Each FadeInController draws its own black at this; stacked, what shows
    // through is the product of what each lets through.
    const double each = Byte(1.0 - std::max(blackMs, 0.0) / rules.overlay.fadeMs);
    return 1.0 - std::pow(1.0 - each, count);
}

double OverlayAlpha(const Rules& rules, double ageMs) {
    return OverlayLayersAlpha(rules, ageMs, rules.overlay.layers);
}

glm::dvec2 Follow::Tick(const Rules& rules, const glm::dvec2& target, double dtMs) {
    const Rules::Sign& sign = rules.sign;
    // PositionInterpolator::getCurrentPos: the end itself once the time is past
    // (a strict compare), and smoothEnd of the clamped fraction before it.
    const auto current = [this, &sign]() {
        if (elapsedMs > sign.followMs) return to;
        const double bias = std::clamp(elapsedMs / sign.followMs, 0.0, 1.0);
        return from + (to - from) * SmoothEnd(bias);
    };
    // followUp's first call makes the interpolator from where the entity stands.
    if (!started) {
        from = at;
        to = target;
        elapsedMs = 0.0;
        sinceAimedMs = 0.0;
        started = true;
    }
    // Aimed again only when it is not there yet, and only strictly after the
    // retarget time: `switchTime > updateRate`.
    if (target != at && sinceAimedMs > sign.retargetMs) {
        from = current();
        to = target;
        elapsedMs = 0.0;
        sinceAimedMs = 0.0;
    }
    // The frame's time goes to both clocks, then the entity is put where the
    // interpolator now says (AddToUInt, InterpolationTimer::update, SetPositionXY).
    sinceAimedMs += dtMs;
    elapsedMs += dtMs;
    at = current();
    return at;
}

double HandOver(const Rules& rules, double ageMs, double dtMs, double& heldMs) {
    heldMs += std::max(dtMs, 0.0);
    // Whole at startAfterMs itself, as OverlayLayersAlpha has it.
    if (ageMs <= rules.overlay.startAfterMs) return 0.0;
    const double owed = heldMs;
    heldMs = 0.0;
    return owed;
}

glm::dvec2 SignTarget(const Rules& rules, const glm::dvec2& cameraCorner, const glm::dvec2& signSize) {
    return cameraCorner + signSize * rules.sign.centreInBySize;
}

double CaptionAlpha(const Rules& rules, double ageMs) {
    if (ageMs >= rules.caption.fadeMs) return 0.0;
    return Byte(1.0 - std::max(ageMs, 0.0) / rules.caption.fadeMs);
}

std::string CaptionText(const Rules& rules, int index) {
    return rules.caption.prefix + std::to_string(index + 1);
}

double PlaqueAlpha(const Rules& rules, double ageMs) {
    const Rules::Plaque& plaque = rules.plaque;
    if (ageMs < 0.0) return 0.0;
    // getUiTime() > 2000: strictly past.
    if (ageMs > plaque.dismissAfterMs) {
        const double since = ageMs - plaque.dismissAfterMs;
        if (since >= plaque.dismissMs) return 0.0; // removeDismissedSprites
        return Byte(1.0 - SmoothEnd(since / plaque.dismissMs));
    }
    return Byte(SmoothEnd(std::min(ageMs / plaque.appearMs, 1.0)));
}

std::string MedalSprite(const Rules& rules, int medal) {
    // getLargeSpriteMedalName: 3 gold, 2 silver, anything else bronze - and the
    // plaque is only added where the score is not 0 at all.
    if (medal <= 0) return {};
    if (medal >= 3) return rules.plaque.medalGold;
    if (medal == 2) return rules.plaque.medalSilver;
    return rules.plaque.medalBronze;
}

std::vector<Glyph> LayOutCaption(const Rules& rules, const Supersonic::BitmapFont& font, const std::string& text,
                                 const glm::dvec2& viewUnits) {
    return LayOutText(font, text, rules.caption.centreOfView * viewUnits, rules.caption.unitsPerFontPx);
}

std::vector<Glyph> LayOutText(const Supersonic::BitmapFont& font, const std::string& text, const glm::dvec2& centre,
                              double unitsPerFontPx) {
    if (!font.IsLoaded()) return {};
    // ComputeTextBoxSize: the widest line's summed advances, by lineHeight a line.
    const glm::dvec2 box = glm::dvec2(font.Measure(text)) * unitsPerFontPx;
    return LayOutTextFrom(font, text, centre - box * 0.5, unitsPerFontPx);
}

std::vector<Glyph> LayOutTextFrom(const Supersonic::BitmapFont& font, const std::string& text,
                                  const glm::dvec2& topLeft, double unitsPerFontPx) {
    std::vector<Glyph> glyphs;
    if (!font.IsLoaded()) return glyphs;
    const double scale = unitsPerFontPx;
    const glm::dvec2 page(font.PageSize());
    if (page.x <= 0.0 || page.y <= 0.0) return glyphs;

    double pen = 0.0;
    double lineTop = 0.0;
    for (const char c : text) {
        if (c == '\n') {
            pen = 0.0;
            lineTop += static_cast<double>(font.LineHeight());
            continue;
        }
        const Supersonic::FontGlyph* found = font.Find(static_cast<uint32_t>(static_cast<unsigned char>(c)));
        if (found == nullptr) continue;
        if (found->width > 0 && found->height > 0) {
            Glyph glyph;
            glyph.rect.min = topLeft + glm::dvec2(pen + found->xoffset, lineTop + found->yoffset) * scale;
            glyph.rect.size = glm::dvec2(found->width, found->height) * scale;
            glyph.page = found->page;
            glyph.uvScale = glm::dvec2(found->width, found->height) / page;
            glyph.uvOffset = glm::dvec2(found->x, found->y) / page;
            glyphs.push_back(glyph);
        }
        pen += static_cast<double>(found->xadvance);
    }
    return glyphs;
}

} // namespace MagicPortals::Hud
