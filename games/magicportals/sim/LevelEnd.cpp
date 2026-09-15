#include "sim/LevelEnd.hpp"

#include "core/Json.hpp"

#include <utility>

namespace MagicPortals::LevelEnd {

namespace {

namespace Json = Supersonic::Json;
using UiLayer::Read::Byte;
using UiLayer::Read::Pair;
using UiLayer::Read::Positive;
using UiLayer::Read::ReadPlaced;
using UiLayer::Read::Size;
using UiLayer::Read::Text;

bool ReadVeil(const Json::Value& block, Veil& out, std::string& why, const std::string& where) {
    if (!block.IsObject()) {
        why = where + " is not an object";
        return false;
    }
    Veil read;
    if (!Text(block, "sprite", read.sprite, why, where) || !Byte(block, "tint_alpha_byte", read.tintAlphaByte, why, where) ||
        !Size(block, "size_of_screen", read.sizeOfScreen, why, where)) {
        return false;
    }
    out = std::move(read);
    return true;
}

bool ReadText(const Json::Value& block, TextRule& out, std::string& why, const std::string& where) {
    if (!block.IsObject()) {
        why = where + " is not an object";
        return false;
    }
    TextRule read;
    if (!Text(block, "font", read.font, why, where) ||
        !Positive(block, "units_per_font_px", read.unitsPerFontPx, why, where) ||
        !Pair(block, "offset_units", read.offsetUnits, why, where)) {
        return false;
    }
    out = std::move(read);
    return true;
}

std::string MedalFile(const Rules::Finished& finished, int score) {
    if (score == 3) return finished.medalGold;
    if (score == 2) return finished.medalSilver;
    return finished.medalBronze;
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    Rules read;
    if (!UiLayer::LoadRules(path, read.layer, error)) return false;

    Json::Value root;
    if (!UiLayer::Read::File(path, root, error)) return false;
    const Json::Value& end = root["level_end"];
    if (!end.IsObject() || !end["beats"].IsObject() || !end["hud"].IsObject() || !end["finished"].IsObject() ||
        !end["lost"].IsObject()) {
        error = path + ": level_end and its beats, hud, finished and lost are each an object";
        return false;
    }
    std::string why;
    const auto fail = [&error, &path, &why]() {
        error = path + ": " + why;
        return false;
    };

    if (!Positive(end["beats"], "won_delay_ms", read.wonDelayMs, why, "level_end.beats") ||
        !Positive(end["beats"], "lost_delay_ms", read.lostDelayMs, why, "level_end.beats") ||
        !Positive(end["hud"], "pad_decay_factor", read.padDecayFactor, why, "level_end.hud")) {
        return fail();
    }
    if (read.padDecayFactor >= 1.0) {
        why = "level_end.hud.pad_decay_factor is not below 1: the pads would never go";
        return fail();
    }

    const Json::Value& finished = end["finished"];
    Rules::Finished& f = read.finished;
    const Json::Value& medal = finished["medal"];
    const Json::Value& counter = finished["counter"];
    const Json::Value& golden = finished["golden_plaque"];
    const Json::Value& crystal = finished["crystal"];
    const Json::Value& crystalCount = finished["crystal_count"];
    if (!ReadVeil(finished["veil"], f.veil, why, "level_end.finished.veil") ||
        !ReadPlaced(finished["title"], "sprite", f.title, why, "level_end.finished.title") ||
        !ReadPlaced(finished["portals_plaque"], "sprite", f.portalsPlaque, why, "level_end.finished.portals_plaque") ||
        !ReadPlaced(golden, "sprite", f.goldenPlaque, why, "level_end.finished.golden_plaque") ||
        !ReadPlaced(finished["restart_button"], "sprite", f.restart, why, "level_end.finished.restart_button") ||
        !ReadPlaced(finished["next_button"], "sprite", f.next, why, "level_end.finished.next_button") ||
        !ReadPlaced(finished["list_button"], "sprite", f.list, why, "level_end.finished.list_button") ||
        !ReadText(counter, f.counter, why, "level_end.finished.counter") ||
        !Positive(counter, "stride_ms", f.counterStrideMs, why, "level_end.finished.counter") ||
        !ReadText(finished["golden_number"], f.goldenNumber, why, "level_end.finished.golden_number") ||
        !ReadText(crystalCount, f.crystalCount, why, "level_end.finished.crystal_count") ||
        !Positive(crystalCount, "stride_ms", f.crystalStrideMs, why, "level_end.finished.crystal_count")) {
        return fail();
    }
    const Json::Value& shownBelow = golden["shown_below_score"];
    if (!shownBelow.IsNumber() || shownBelow.AsNumber() < 1.0 || shownBelow.AsNumber() > 4.0 ||
        shownBelow.AsNumber() != static_cast<double>(static_cast<int>(shownBelow.AsNumber()))) {
        why = "level_end.finished.golden_plaque.shown_below_score is not a whole score within 1..4";
        return fail();
    }
    f.goldenPlaqueBelowScore = static_cast<int>(shownBelow.AsNumber());
    // The medal has no sprite of its own - its tier names it - so it is read
    // field by field rather than as a Placed with a sprite key.
    if (!medal.IsObject()) {
        why = "level_end.finished.medal is not an object";
        return fail();
    }
    if (!UiLayer::Read::Fraction(medal, "at_screen", f.medal.atScreen, why, "level_end.finished.medal") ||
        !UiLayer::Read::Fraction(medal, "origin", f.medal.origin, why, "level_end.finished.medal") ||
        !Size(medal, "size_units", f.medal.sizeUnits, why, "level_end.finished.medal") ||
        !Text(medal, "bronze", f.medalBronze, why, "level_end.finished.medal") ||
        !Text(medal, "silver", f.medalSilver, why, "level_end.finished.medal") ||
        !Text(medal, "gold", f.medalGold, why, "level_end.finished.medal")) {
        return fail();
    }
    if (!crystal.IsObject()) {
        why = "level_end.finished.crystal is not an object";
        return fail();
    }
    if (!Text(crystal, "sprite", f.crystalSprite, why, "level_end.finished.crystal") ||
        !Pair(crystal, "offset_units", f.crystalOffsetUnits, why, "level_end.finished.crystal") ||
        !Size(crystal, "size_units", f.crystalSizeUnits, why, "level_end.finished.crystal")) {
        return fail();
    }

    const Json::Value& lost = end["lost"];
    if (!ReadVeil(lost["veil"], read.lost.veil, why, "level_end.lost.veil") ||
        !ReadPlaced(lost["title"], "sprite", read.lost.title, why, "level_end.lost.title") ||
        !ReadPlaced(lost["restart_button"], "sprite", read.lost.restart, why, "level_end.lost.restart_button") ||
        !ReadPlaced(lost["list_button"], "sprite", read.lost.list, why, "level_end.lost.list_button")) {
        return fail();
    }

    out = std::move(read);
    return true;
}

int ComputeScore(int portals, int goldenScore, int crystals, int crystalsTotal) {
    if (crystalsTotal > 0 && crystals == 0) return 1;
    if (portals <= goldenScore) return crystals < crystalsTotal ? 2 : 3;
    if (portals <= goldenScore + 2) return 2;
    return 1;
}

void Counter::Tick(double dtMs, double strideMs) {
    // ScoreCounter::update: Timer::update, then - only once the time has reached
    // the stride and there is still a step to take - Timer::reset and one step.
    // The reset drops the remainder, which at the port's 60 Hz tick is none.
    timeMs += dtMs;
    if (timeMs < strideMs || current == end) return;
    timeMs = 0.0;
    current += current < end ? 1 : -1;
}

int ShownScore(const Play& play, int portalsShown) {
    return ComputeScore(portalsShown, play.goldenScore, play.crystals, play.crystalsTotal);
}

std::vector<Piece> Finished(const Rules& rules, const Play& play, int portalsShown, int crystalsShown,
                            const glm::dvec2& viewUnits, double ms) {
    std::vector<Piece> pieces;
    const UiLayer::Rules& layer = rules.layer;
    const Rules::Finished& f = rules.finished;

    // THE SPRITES, each a UISprite on the same 1000 ms: UILayer::draw's first half.
    const auto sprite = [&](Element element, const std::string& file, const Hud::Rect& rect, int tintAlphaByte) {
        Piece piece;
        piece.element = element;
        piece.file = file;
        piece.rect = rect;
        piece.alphaByte = UiLayer::SpriteAlphaByte(layer, tintAlphaByte, ms);
        pieces.push_back(std::move(piece));
    };
    const auto placed = [&viewUnits](const UiLayer::Placed& p) {
        return UiLayer::RectAt(p, UiLayer::Anchor(p, viewUnits));
    };
    sprite(Element::Veil, f.veil.sprite, Hud::Rect{glm::dvec2(0.0), viewUnits * f.veil.sizeOfScreen},
           f.veil.tintAlphaByte);
    sprite(Element::Title, f.title.sprite, placed(f.title), 255);
    sprite(Element::PortalsPlaque, f.portalsPlaque.sprite, placed(f.portalsPlaque), 255);
    const bool golden = ComputeScore(play.portalsUsed, play.goldenScore, play.crystals, play.crystalsTotal) <
                        f.goldenPlaqueBelowScore;
    if (golden) sprite(Element::GoldenPlaque, f.goldenPlaque.sprite, placed(f.goldenPlaque), 255);

    // THE BUTTONS, sliding in together on 700 ms: its second half.
    const int buttonAlpha = UiLayer::ButtonAlphaByte(layer, ms);
    const auto button = [&](Button which, const UiLayer::Placed& p) {
        const glm::dvec2 anchor = UiLayer::Anchor(p, viewUnits);
        Piece piece;
        piece.element = Element::Button;
        piece.button = which;
        piece.file = p.sprite;
        piece.rect = UiLayer::RectAt(p, UiLayer::ButtonAnchorAt(layer, anchor, viewUnits, ms));
        piece.alphaByte = buttonAlpha;
        piece.pressable = true;
        pieces.push_back(std::move(piece));
    };
    button(Button::Restart, f.restart);
    button(Button::Next, f.next);
    button(Button::List, f.list);

    // LevelFinishedLayer::draw, every piece in the restart button's colour.
    const glm::dvec2 medalAnchor = UiLayer::Anchor(f.medal, viewUnits);
    Piece medal;
    medal.element = Element::Medal;
    medal.file = MedalFile(f, ShownScore(play, portalsShown));
    medal.rect = UiLayer::RectAt(f.medal, medalAnchor);
    medal.alphaByte = buttonAlpha;
    pieces.push_back(std::move(medal));

    const auto text = [&](Element element, const TextRule& rule, std::string words, const glm::dvec2& at,
                          bool centred) {
        Piece piece;
        piece.element = element;
        piece.file = rule.font;
        piece.words = std::move(words);
        piece.at = at;
        piece.unitsPerFontPx = rule.unitsPerFontPx;
        piece.centred = centred;
        piece.alphaByte = buttonAlpha;
        pieces.push_back(std::move(piece));
    };
    text(Element::Counter, f.counter, std::to_string(portalsShown), medalAnchor + f.counter.offsetUnits, true);
    if (golden) {
        text(Element::GoldenNumber, f.goldenNumber, std::to_string(play.goldenScore),
             UiLayer::Anchor(f.goldenPlaque, viewUnits) + f.goldenNumber.offsetUnits, true);
    }
    if (play.crystalsTotal > 0) {
        Piece crystal;
        crystal.element = Element::Crystal;
        crystal.file = f.crystalSprite;
        crystal.rect = Hud::Rect{medalAnchor + f.crystalOffsetUnits, f.crystalSizeUnits};
        crystal.alphaByte = buttonAlpha;
        const glm::dvec2 crystalAt = crystal.rect.min;
        pieces.push_back(std::move(crystal));
        text(Element::CrystalCount, f.crystalCount,
             std::to_string(crystalsShown) + "/" + std::to_string(play.crystalsTotal),
             crystalAt + f.crystalCount.offsetUnits, false);
    }
    return pieces;
}

std::vector<Piece> Lost(const Rules& rules, const glm::dvec2& viewUnits, double ms) {
    std::vector<Piece> pieces;
    const UiLayer::Rules& layer = rules.layer;
    const Rules::Lost& l = rules.lost;

    Piece veil;
    veil.element = Element::Veil;
    veil.file = l.veil.sprite;
    veil.rect = Hud::Rect{glm::dvec2(0.0), viewUnits * l.veil.sizeOfScreen};
    veil.alphaByte = UiLayer::SpriteAlphaByte(layer, l.veil.tintAlphaByte, ms);
    pieces.push_back(std::move(veil));

    Piece title;
    title.element = Element::Title;
    title.file = l.title.sprite;
    title.rect = UiLayer::RectAt(l.title, UiLayer::Anchor(l.title, viewUnits));
    title.alphaByte = UiLayer::SpriteAlphaByte(layer, 255, ms);
    pieces.push_back(std::move(title));

    for (const auto& [which, p] : {std::pair<Button, const UiLayer::Placed*>{Button::Restart, &l.restart},
                                   std::pair<Button, const UiLayer::Placed*>{Button::List, &l.list}}) {
        const glm::dvec2 anchor = UiLayer::Anchor(*p, viewUnits);
        Piece piece;
        piece.element = Element::Button;
        piece.button = which;
        piece.file = p->sprite;
        piece.rect = UiLayer::RectAt(*p, UiLayer::ButtonAnchorAt(layer, anchor, viewUnits, ms));
        piece.alphaByte = UiLayer::ButtonAlphaByte(layer, ms);
        piece.pressable = true;
        pieces.push_back(std::move(piece));
    }
    return pieces;
}

std::optional<Button> ButtonAt(const std::vector<Piece>& pieces, const glm::dvec2& point) {
    for (const Piece& piece : pieces) {
        if (piece.element == Element::Button && piece.pressable && piece.rect.Contains(point)) return piece.button;
    }
    return std::nullopt;
}

std::vector<Strip> ClampedStrips(const Hud::Rect& rect, const glm::ivec2& texels) {
    std::vector<Strip> strips;
    if (texels.x <= 0 || texels.y <= 0 || !(rect.size.x > 0.0) || !(rect.size.y > 0.0)) return strips;
    const double texelWide = rect.size.x / static_cast<double>(texels.x);
    const double half = 0.5 / static_cast<double>(texels.x);
    // Rows are sampled between their centres too, so no strip reaches across the
    // top or bottom edge into the row a repeat would wrap to.
    const double vMin = 0.5 / static_cast<double>(texels.y);
    const double vMax = 1.0 - vMin;
    const double edge = texelWide * 0.5;

    Strip left;
    left.rect = Hud::Rect{rect.min, glm::dvec2(edge, rect.size.y)};
    left.uvMin = glm::dvec2(half, vMin);
    left.uvMax = glm::dvec2(half, vMax);
    strips.push_back(left);

    Strip middle;
    middle.rect = Hud::Rect{rect.min + glm::dvec2(edge, 0.0), glm::dvec2(rect.size.x - 2.0 * edge, rect.size.y)};
    middle.uvMin = glm::dvec2(half, vMin);
    middle.uvMax = glm::dvec2(1.0 - half, vMax);
    strips.push_back(middle);

    Strip right;
    right.rect = Hud::Rect{rect.min + glm::dvec2(rect.size.x - edge, 0.0), glm::dvec2(edge, rect.size.y)};
    right.uvMin = glm::dvec2(1.0 - half, vMin);
    right.uvMax = glm::dvec2(1.0 - half, vMax);
    strips.push_back(right);
    return strips;
}

int PadDecayByte(const Rules& rules, int startByte, int ticks) {
    int alpha = startByte;
    for (int tick = 0; tick < ticks && alpha > 0; ++tick) {
        alpha = static_cast<int>(static_cast<double>(alpha) * rules.padDecayFactor);
    }
    return alpha;
}

glm::dvec2 HudAnchor(const Hud::Placement& placement, const glm::dvec2& viewUnits) {
    const Hud::Rect rect = Hud::Place(placement, viewUnits);
    switch (placement.anchor) {
    case Hud::Anchor::TopLeft:
        return rect.min;
    case Hud::Anchor::TopRight:
        return glm::dvec2(rect.Max().x, rect.min.y);
    case Hud::Anchor::BottomLeft:
        return glm::dvec2(rect.min.x, rect.Max().y);
    case Hud::Anchor::BottomRight:
        return rect.Max();
    }
    return rect.min;
}

Dismissed HudDismissed(const Rules& rules, const Hud::Placement& placement, int alphaByte,
                       const glm::dvec2& viewUnits, double ms) {
    Dismissed out;
    const int dismissByte = UiLayer::ButtonDismissAlphaByte(rules.layer, ms);
    if (dismissByte <= 0 || alphaByte <= 0) return out;
    const Hud::Rect home = Hud::Place(placement, viewUnits);
    const glm::dvec2 anchor = HudAnchor(placement, viewUnits);
    const glm::dvec2 now = UiLayer::ButtonDismissAnchorAt(rules.layer, anchor, viewUnits, ms);
    out.rect = Hud::Rect{home.min + (now - anchor), home.size};
    out.alpha = static_cast<double>(alphaByte) / 255.0 * static_cast<double>(dismissByte) / 255.0;
    out.shown = true;
    return out;
}

} // namespace MagicPortals::LevelEnd
