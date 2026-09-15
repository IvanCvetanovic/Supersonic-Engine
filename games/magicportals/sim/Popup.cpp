#include "sim/Popup.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace MagicPortals::Popup {

namespace {

namespace Json = Supersonic::Json;
using UiLayer::Read::Byte;
using UiLayer::Read::Pair;
using UiLayer::Read::Positive;
using UiLayer::Read::ReadPlaced;
using UiLayer::Read::Size;
using UiLayer::Read::Text;

bool Number(const Json::Value& block, const char* key, double& out, std::string& why, const std::string& where) {
    const Json::Value& value = block[key];
    if (!value.IsNumber()) {
        why = where + "." + key + " is missing or not a number";
        return false;
    }
    out = value.AsNumber();
    return true;
}

bool NotNegative(const Json::Value& block, const char* key, double& out, std::string& why,
                 const std::string& where) {
    double read = 0.0;
    if (!Number(block, key, read, why, where)) return false;
    if (read < 0.0) {
        why = where + "." + key + " is below zero";
        return false;
    }
    out = read;
    return true;
}

bool WholeAtLeast(const Json::Value& block, const char* key, int least, int& out, std::string& why,
                  const std::string& where) {
    double read = 0.0;
    if (!Number(block, key, read, why, where)) return false;
    if (read < static_cast<double>(least) || read != std::floor(read)) {
        why = where + "." + key + " is not a whole number of at least " + std::to_string(least);
        return false;
    }
    out = static_cast<int>(read);
    return true;
}

bool ReadFilter(const Json::Value& block, Filter& out, std::string& why, const std::string& where) {
    std::string name;
    if (!Text(block, "filter", name, why, where)) return false;
    if (name == "linear") {
        out = Filter::Linear;
    } else if (name == "smooth_end") {
        out = Filter::SmoothEnd;
    } else if (name == "smooth_beginning") {
        out = Filter::SmoothBeginning;
    } else {
        why = where + ".filter is not linear, smooth_end or smooth_beginning";
        return false;
    }
    return true;
}

bool ReadSheet(const Json::Value& block, Sheet& out, std::string& why, const std::string& where) {
    const Json::Value& sheet = block["sheet"];
    if (!sheet.IsObject()) {
        out = Sheet{};
        return true; // a single picture
    }
    Sheet read;
    const std::string at = where + ".sheet";
    if (!WholeAtLeast(sheet, "columns", 1, read.columns, why, at) ||
        !WholeAtLeast(sheet, "rows", 1, read.rows, why, at)) {
        return false;
    }
    if (sheet.Has("frame") && !WholeAtLeast(sheet, "frame", 0, read.frame, why, at)) return false;
    if (read.frame >= read.columns * read.rows) {
        why = at + ".frame is past the sheet's last frame";
        return false;
    }
    if (sheet.Has("frame_ms") && !Positive(sheet, "frame_ms", read.frameMs, why, at)) return false;
    out = read;
    return true;
}

bool ReadItem(const Json::Value& block, Item& out, std::string& why, const std::string& where) {
    if (!block.IsObject()) {
        why = where + " is not an object";
        return false;
    }
    Item read;
    std::string kind;
    if (!Text(block, "kind", kind, why, where) || !Text(block, "name", read.name, why, where) ||
        !Text(block, "sprite", read.sprite, why, where) || !Size(block, "size_units", read.sizeUnits, why, where) ||
        // An origin may lie off the sprite: the hand's (0.5, 1.3) puts its point at the fingertip.
        !Pair(block, "origin", read.origin, why, where) || !ReadSheet(block, read.sheet, why, where)) {
        return false;
    }
    if (kind == "static") {
        read.kind = Item::Kind::Static;
        if (!Pair(block, "at", read.at, why, where)) return false;
        out = std::move(read);
        return true;
    }
    if (kind != "track") {
        why = where + ".kind is not static or track";
        return false;
    }
    read.kind = Item::Kind::Track;
    if (block.Has("tint_rgb")) {
        const Json::Value& rgb = block["tint_rgb"];
        const auto& parts = rgb.AsArray();
        if (!rgb.IsArray() || parts.size() != 3 || !parts[0].IsNumber() || !parts[1].IsNumber() ||
            !parts[2].IsNumber()) {
            why = where + ".tint_rgb is not three bytes";
            return false;
        }
        for (int i = 0; i < 3; ++i) {
            const double byte = parts[static_cast<std::size_t>(i)].AsNumber();
            if (byte < 0.0 || byte > 255.0) {
                why = where + ".tint_rgb is not three bytes";
                return false;
            }
            read.tintRgb[i] = byte / 255.0;
        }
    }
    const Json::Value& waypoints = block["waypoints"];
    if (!waypoints.IsArray() || waypoints.AsArray().size() < 2) {
        why = where + ".waypoints is not a list of at least two";
        return false;
    }
    for (std::size_t i = 0; i < waypoints.AsArray().size(); ++i) {
        const Json::Value& one = waypoints.AsArray()[i];
        const std::string at = where + ".waypoints[" + std::to_string(i) + "]";
        Waypoint waypoint;
        if (!one.IsObject() || !Pair(one, "at", waypoint.at, why, at) ||
            !Byte(one, "alpha", waypoint.alphaByte, why, at) || !Number(one, "angle", waypoint.angleDeg, why, at) ||
            !ReadFilter(one, waypoint.filter, why, at) || !NotNegative(one, "ms", waypoint.ms, why, at)) {
            if (!one.IsObject()) why = at + " is not an object";
            return false;
        }
        read.waypoints.push_back(waypoint);
    }
    out = std::move(read);
    return true;
}

bool ReadOpeners(const Json::Value& block, std::vector<Opener>& out, std::string& why, const std::string& where) {
    const Json::Value& list = block["levels"];
    if (!list.IsArray() || list.AsArray().empty()) {
        why = where + ".levels is not a list";
        return false;
    }
    std::vector<Opener> read;
    for (std::size_t i = 0; i < list.AsArray().size(); ++i) {
        const Json::Value& one = list.AsArray()[i];
        const std::string at = where + ".levels[" + std::to_string(i) + "]";
        Opener opener;
        if (!Text(one, "scene", opener.scene, why, at) || !Text(one, "label", opener.label, why, at)) return false;
        const Json::Value& popup = one["popup"];
        if (popup.IsString()) {
            opener.popup = popup.AsString();
        } else if (popup.GetType() != Json::Type::Null || !one.Has("popup")) {
            why = at + ".popup is not a class's name or null";
            return false;
        }
        read.push_back(std::move(opener));
    }
    out = std::move(read);
    return true;
}

// sin(pi/2 * v), clamped: every Interpolator's default filter.
double SmoothEnd(double v) {
    return std::sin(std::numbers::pi / 2.0 * std::clamp(v, 0.0, 1.0));
}

// A triangle bias over strides, as Button::bounce and Button::blinkColor read
// the button's elapsed time: the stride's own fraction, reversed on odd strides.
double StrideBias(double elapsedMs, double strideMs) {
    if (!(strideMs > 0.0)) return 0.0;
    const double stride = std::floor(elapsedMs / strideMs);
    const double bias = (elapsedMs - stride * strideMs) / strideMs;
    const bool invert = static_cast<long long>(stride) % 2 == 1;
    return invert ? 1.0 - bias : bias;
}

int AlphaByteOf(double alpha) {
    return static_cast<int>(std::clamp(alpha, 0.0, 1.0) * 255.0);
}

// A counter-clockwise turn on a +y-down view, in square units.
glm::dvec2 Turned(const glm::dvec2& v, double degrees) {
    const double r = degrees * std::numbers::pi / 180.0;
    const double c = std::cos(r);
    const double s = std::sin(r);
    return glm::dvec2(v.x * c + v.y * s, -v.x * s + v.y * c);
}

glm::dvec2 FrameUvMin(const Sheet& sheet, int frame) {
    return glm::dvec2(static_cast<double>(frame % sheet.columns) / sheet.columns,
                      static_cast<double>(frame / sheet.columns) / sheet.rows);
}

} // namespace

double Filtered(Filter filter, double bias) {
    const double b = std::clamp(bias, 0.0, 1.0);
    switch (filter) {
    case Filter::Linear:
        return b;
    case Filter::SmoothEnd:
        return std::sin(std::numbers::pi / 2.0 * b);
    case Filter::SmoothBeginning:
        return 1.0 - std::sin(std::numbers::pi / 2.0 * (1.0 - b));
    }
    return b;
}

const Class* Rules::FindClass(const std::string& name) const {
    for (const Class& one : classes) {
        if (one.name == name) return &one;
    }
    return nullptr;
}

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    Rules read;
    if (!UiLayer::LoadRules(path, read.layer, error)) return false;
    Json::Value root;
    if (!UiLayer::Read::File(path, root, error)) return false;
    const Json::Value& popups = root["popups"];
    if (!popups.IsObject()) {
        error = path + ": popups is not an object";
        return false;
    }
    std::string why;
    const auto fail = [&error, &path, &why]() {
        error = path + ": " + why;
        return false;
    };

    const Json::Value& highlight = popups["highlight"];
    if (!Byte(popups["dim"], "alpha_byte", read.dimAlphaByte, why, "popups.dim") ||
        !ReadPlaced(popups["card"], "sprite", read.card, why, "popups.card") ||
        !ReadPlaced(popups["close_button"], "sprite", read.closeButton, why, "popups.close_button") ||
        !Pair(highlight, "bounce_scale_a", read.highlight.bounceA, why, "popups.highlight") ||
        !Pair(highlight, "bounce_scale_b", read.highlight.bounceB, why, "popups.highlight") ||
        !Positive(highlight, "bounce_stride_ms", read.highlight.bounceStrideMs, why, "popups.highlight") ||
        !Positive(highlight, "blink_brightness_a", read.highlight.blinkA, why, "popups.highlight") ||
        !Positive(highlight, "blink_brightness_b", read.highlight.blinkB, why, "popups.highlight") ||
        !Positive(highlight, "blink_stride_ms", read.highlight.blinkStrideMs, why, "popups.highlight") ||
        !ReadOpeners(popups["level_start"], read.levelStart, why, "popups.level_start")) {
        return fail();
    }
    const Json::Value& block = popups["help_block"];
    if (!Text(block, "entity", read.helpBlock.entity, why, "popups.help_block") ||
        !Positive(block, "box_scale", read.helpBlock.boxScale, why, "popups.help_block") ||
        !Positive(block, "max_move_px", read.helpBlock.maxMovePx, why, "popups.help_block") ||
        !ReadOpeners(block, read.helpBlock.levels, why, "popups.help_block")) {
        return fail();
    }

    const Json::Value& classes = popups["classes"];
    if (!classes.IsObject() || classes.AsObject().empty()) {
        why = "popups.classes is not an object of classes";
        return fail();
    }
    for (const auto& [name, body] : classes.AsObject()) {
        if (name.starts_with("_")) continue;
        const std::string where = "popups.classes." + name;
        Class one;
        one.name = name;
        one.card = read.card;
        if (!body.IsObject()) {
            why = where + " is not an object";
            return fail();
        }
        // A class with no track has no loop to state.
        if (body.Has("loop_ms") && !Positive(body, "loop_ms", one.loopMs, why, where)) return fail();
        if (body.Has("card") && !ReadPlaced(body["card"], "sprite", one.card, why, where + ".card")) return fail();
        const Json::Value& items = body["draw"];
        if (!items.IsArray() || items.AsArray().empty()) {
            why = where + ".draw is not a list";
            return fail();
        }
        for (std::size_t i = 0; i < items.AsArray().size(); ++i) {
            Item item;
            if (!ReadItem(items.AsArray()[i], item, why, where + ".draw[" + std::to_string(i) + "]")) return fail();
            if (item.kind == Item::Kind::Track && !(one.loopMs > 0.0)) {
                why = where + ".loop_ms is missing, not a number, or not above zero, and the class has a track";
                return fail();
            }
            if (item.kind == Item::Kind::Track) {
                // Every loop of a class runs on the same period; a waypoint mistyped
                // would put one out of step with the rest, and the period is what the
                // recordings measured.
                double total = 0.0;
                for (const Waypoint& waypoint : item.waypoints) total += waypoint.ms;
                if (std::fabs(total - one.loopMs) > 1e-6) {
                    why = where + ".draw[" + std::to_string(i) + "] (" + item.name + ") adds up to " +
                          std::to_string(total) + " ms, not the class's loop_ms";
                    return fail();
                }
            }
            one.items.push_back(std::move(item));
        }
        read.classes.push_back(std::move(one));
    }
    for (const std::vector<Opener>* openers : {&read.levelStart, &read.helpBlock.levels}) {
        for (const Opener& opener : *openers) {
            if (!opener.popup.empty() && read.FindClass(opener.popup) == nullptr) {
                why = "popups: " + opener.label + " opens " + opener.popup + ", which popups.classes does not describe";
                return fail();
            }
        }
    }
    out = std::move(read);
    return true;
}

const Class* LevelStartClass(const Rules& rules, const std::string& scene) {
    for (const Opener& opener : rules.levelStart) {
        if (opener.scene == scene) return rules.FindClass(opener.popup);
    }
    return nullptr;
}

const Class* HelpBlockClass(const Rules& rules, const std::string& scene, bool& hasBlock) {
    hasBlock = false;
    for (const Opener& opener : rules.helpBlock.levels) {
        if (opener.scene != scene) continue;
        hasBlock = true;
        return opener.popup.empty() ? nullptr : rules.FindClass(opener.popup);
    }
    return nullptr;
}

void Loop::Tick(const std::vector<double>& strides, double dtMs) {
    if (strides.empty()) return;
    // FrameTimer::set: the time goes on first, and ONE frame at most is stepped,
    // the stride taken off and the remainder kept.
    timeMs += dtMs;
    const double stride = strides[static_cast<std::size_t>(frame)];
    if (timeMs >= stride) {
        ++frame;
        timeMs -= stride;
        if (frame >= static_cast<int>(strides.size())) frame = 0; // repeat
    }
}

std::vector<double> TrackStrides(const Item& track) {
    std::vector<double> strides;
    strides.reserve(track.waypoints.size());
    for (const Waypoint& waypoint : track.waypoints) strides.push_back(waypoint.ms);
    return strides;
}

std::vector<double> SheetStrides(const Item& item) {
    if (!(item.sheet.frameMs > 0.0)) return {};
    return std::vector<double>(static_cast<std::size_t>(item.sheet.columns * item.sheet.rows), item.sheet.frameMs);
}

Point PointOf(const Item& track, const Loop& loop) {
    Point point;
    if (track.waypoints.empty()) return point;
    const std::size_t count = track.waypoints.size();
    const std::size_t index = static_cast<std::size_t>(std::clamp(loop.frame, 0, static_cast<int>(count) - 1));
    const Waypoint& current = track.waypoints[index];
    const Waypoint& next = track.waypoints[(index + 1) % count];
    // FrameTimer::getBias: min(time, stride) / max(stride, 1).
    const double bias = std::min(loop.timeMs, current.ms) / std::max(current.ms, 1.0);
    const double eased = Filtered(current.filter, bias);
    point.at = glm::mix(current.at, next.at, eased);
    point.alpha = glm::mix(current.alphaByte / 255.0, next.alphaByte / 255.0, eased);
    point.angleDeg = glm::mix(current.angleDeg, next.angleDeg, eased);
    return point;
}

Open Start(const Class& cls) {
    Open open;
    open.className = cls.name;
    open.tracks.assign(cls.items.size(), Loop{});
    open.sheets.assign(cls.items.size(), Loop{});
    return open;
}

void Tick(const Class& cls, Open& open, double dtMs) {
    open.clockMs += dtMs;
    const std::size_t count = std::min(cls.items.size(), open.tracks.size());
    for (std::size_t i = 0; i < count; ++i) {
        const Item& item = cls.items[i];
        // The class's update steps every WaypointManager whatever the button is
        // doing; its draw() sets the sheet's frame, and only while the button is not
        // dismissed.
        if (item.kind == Item::Kind::Track) open.tracks[i].Tick(TrackStrides(item), dtMs);
        if (!Closing(open) && i < open.sheets.size()) open.sheets[i].Tick(SheetStrides(item), dtMs);
    }
}

void Close(Open& open) {
    if (open.closedAtMs < 0.0) open.closedAtMs = open.clockMs;
}

bool Closing(const Open& open) {
    return open.closedAtMs >= 0.0;
}

bool Gone(const Rules& rules, const Open& open) {
    // A micro-millisecond of slack: sixty ticks of 1000 / 60 are 1000 ms to within
    // the doubles' rounding, and a second that reads 999.9999999 is still over.
    return Closing(open) &&
           open.clockMs - open.closedAtMs >= std::max(rules.layer.spriteAppearMs, rules.layer.buttonDismissMs) - 1e-6;
}

CloseButtonState CloseButtonAt(const Rules& rules, const Open& open, const glm::dvec2& viewUnits) {
    CloseButtonState state;
    const glm::dvec2 home = UiLayer::Anchor(rules.closeButton, viewUnits);
    double elapsed = open.clockMs;
    if (!Closing(open)) {
        state.alphaByte = UiLayer::ButtonAlphaByte(rules.layer, open.clockMs);
        state.anchor = UiLayer::ButtonAnchorAt(rules.layer, home, viewUnits, open.clockMs);
    } else {
        const double since = open.clockMs - open.closedAtMs;
        state.alphaByte = UiLayer::ButtonDismissAlphaByte(rules.layer, since);
        state.anchor = UiLayer::ButtonDismissAnchorAt(rules.layer, home, viewUnits, since);
        // UIButton::update calls Button::update only while not dismissed, so the
        // time the bounce and the blink read stops where the close found it.
        elapsed = open.closedAtMs;
    }
    const Rules::Highlight& h = rules.highlight;
    state.scale = glm::mix(h.bounceA, h.bounceB, SmoothEnd(StrideBias(elapsed, h.bounceStrideMs)));
    state.brightness = glm::mix(h.blinkA, h.blinkB, StrideBias(elapsed, h.blinkStrideMs));
    return state;
}

std::vector<Piece> Pieces(const Rules& rules, const Class& cls, const Open& open, const glm::dvec2& viewUnits) {
    std::vector<Piece> pieces;
    const bool closing = Closing(open);
    const double since = open.clockMs - open.closedAtMs;
    const auto spriteAlpha = [&](int tintAlphaByte) {
        return closing ? UiLayer::SpriteDismissAlphaByte(rules.layer, tintAlphaByte, since)
                       : UiLayer::SpriteAlphaByte(rules.layer, tintAlphaByte, open.clockMs);
    };
    const auto push = [&pieces](Piece piece) {
        if (piece.alphaByte > 0) pieces.push_back(std::move(piece));
    };

    // UILayer::draw: the sprites in the order Popup::Popup added them, then the
    // button.
    Piece dim;
    dim.element = Element::Dim;
    dim.name = "dim";
    dim.rect = Hud::Rect{glm::dvec2(0.0), viewUnits};
    dim.rgb = glm::dvec3(0.0);
    dim.alphaByte = spriteAlpha(rules.dimAlphaByte);
    push(dim);

    Piece card;
    card.element = Element::Card;
    card.name = "card";
    card.sprite = cls.card.sprite;
    card.rect = UiLayer::RectAt(cls.card, UiLayer::Anchor(cls.card, viewUnits));
    card.alphaByte = spriteAlpha(255);
    push(card);

    const CloseButtonState button = CloseButtonAt(rules, open, viewUnits);
    Piece close;
    close.element = Element::CloseButton;
    close.name = "close";
    close.sprite = rules.closeButton.sprite;
    UiLayer::Placed scaled = rules.closeButton;
    scaled.sizeUnits = rules.closeButton.sizeUnits * button.scale;
    close.rect = UiLayer::RectAt(scaled, button.anchor);
    close.rgb = glm::dvec3(button.brightness);
    close.alphaByte = button.alphaByte;
    push(close);

    // The class's draw(): stillValid while the button is not dismissed. The
    // statics take the button's colour with its rgb set white, so they come in
    // with it and are drawn at nothing once it is dismissed; the tracks are drawn
    // in their own colours, and not at all once it is.
    const bool stillValid = !closing;
    const std::size_t count = cls.items.size();
    for (std::size_t i = 0; i < count; ++i) {
        const Item& item = cls.items[i];
        Piece piece;
        piece.name = item.name;
        piece.sprite = item.sprite;
        int frame = item.sheet.frame;
        if (item.sheet.frameMs > 0.0 && i < open.sheets.size()) frame = open.sheets[i].frame;
        piece.uvMin = FrameUvMin(item.sheet, frame);
        piece.uvMax = piece.uvMin + glm::dvec2(1.0 / item.sheet.columns, 1.0 / item.sheet.rows);
        glm::dvec2 anchor(0.0);
        if (item.kind == Item::Kind::Static) {
            piece.element = Element::Static;
            anchor = viewUnits * (glm::dvec2(0.5) + item.at);
            piece.alphaByte = stillValid ? button.alphaByte : 0;
        } else {
            if (!stillValid || i >= open.tracks.size()) continue;
            piece.element = Element::Track;
            const Point point = PointOf(item, open.tracks[i]);
            anchor = viewUnits * (glm::dvec2(0.5) + point.at);
            piece.angleDeg = point.angleDeg;
            piece.rgb = item.tintRgb;
            piece.alphaByte = AlphaByteOf(point.alpha);
        }
        piece.rect = Hud::Rect{anchor - item.sizeUnits * item.origin, item.sizeUnits};
        // The engine turns a sprite about its origin; the overlay turns a quad
        // about its centre. Where the two differ, the centre is carried round the
        // origin first.
        if (piece.angleDeg != 0.0 && item.origin != glm::dvec2(0.5)) {
            const glm::dvec2 centre = anchor + Turned(piece.rect.Centre() - anchor, piece.angleDeg);
            piece.rect.min = centre - item.sizeUnits * 0.5;
        }
        push(std::move(piece));
    }
    return pieces;
}

Hud::Rect HelpBlockRect(const Rules& rules, const glm::dvec2& entityUnits, const glm::dvec2& collisionUnits,
                        const glm::dvec2& cameraCornerUnits) {
    const glm::dvec2 size = collisionUnits * rules.helpBlock.boxScale;
    return Hud::Rect{entityUnits - cameraCornerUnits - size * 0.5, size};
}

} // namespace MagicPortals::Popup
