#include "sim/Credits.hpp"

#include "core/Json.hpp"

#include <utility>

namespace MagicPortals::Credits {

namespace {

namespace Json = Supersonic::Json;
using UiLayer::Read::Byte;
using UiLayer::Read::Fraction;
using UiLayer::Read::Positive;
using UiLayer::Read::ReadPlaced;
using UiLayer::Read::Size;
using UiLayer::Read::Text;

// A value within 0..1 exclusive of 0: a factor a tick multiplies by.
bool Factor(const Json::Value& block, const char* key, double& out, std::string& why, const std::string& where) {
    const Json::Value& value = block[key];
    if (!value.IsNumber() || !(value.AsNumber() > 0.0) || value.AsNumber() > 1.0) {
        why = where + "." + key + " is missing or not within (0, 1]";
        return false;
    }
    out = value.AsNumber();
    return true;
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    Rules read;
    if (!UiLayer::LoadRules(path, read.layer, error) || !MenuState::LoadRules(path, read.state, error)) return false;

    Json::Value root;
    if (!UiLayer::Read::File(path, root, error)) return false;
    const Json::Value& credits = root["credits"];
    if (!credits.IsObject() || !credits["background"].IsObject() || !credits["back_button"].IsObject() ||
        !credits["back_button"]["bounce"].IsObject() || !credits["papyrus"].IsObject() ||
        !credits["strip"].IsObject()) {
        error = path + ": credits, and its background, back_button (with bounce), papyrus and strip, are each an object";
        return false;
    }
    std::string why;
    const auto fail = [&error, &path, &why]() {
        error = path + ": " + why;
        return false;
    };
    const Json::Value& background = credits["background"];
    const Json::Value& back = credits["back_button"];
    const Json::Value& papyrus = credits["papyrus"];
    const Json::Value& strip = credits["strip"];
    if (!Text(background, "sprite", read.background.sprite, why, "credits.background") ||
        !Size(background, "size_units", read.background.sizeUnits, why, "credits.background") ||
        !Fraction(background, "centre_of_screen", read.background.centreOfScreen, why, "credits.background") ||
        !ReadPlaced(back, "sprite", read.back, why, "credits.back_button") ||
        !Size(back["bounce"], "scale_a", read.backBounce.scaleA, why, "credits.back_button.bounce") ||
        !Size(back["bounce"], "scale_b", read.backBounce.scaleB, why, "credits.back_button.bounce") ||
        !Positive(back["bounce"], "stride_ms", read.backBounce.strideMs, why, "credits.back_button.bounce") ||
        !Text(papyrus, "sprite", read.papyrus.sprite, why, "credits.papyrus") ||
        !Fraction(papyrus, "x_of_width", read.papyrus.xOfWidth, why, "credits.papyrus") ||
        !Size(papyrus, "size_units", read.papyrus.sizeUnits, why, "credits.papyrus") ||
        !Text(strip, "sprite", read.strip.sprite, why, "credits.strip") ||
        !Size(strip, "size_units", read.strip.sizeUnits, why, "credits.strip") ||
        !Byte(strip, "alpha_byte", read.strip.alphaByte, why, "credits.strip") ||
        !Positive(strip, "speed_screen_px_per_s", read.strip.screenPxPerSecond, why, "credits.strip") ||
        !Positive(strip, "speed_at_screen_px", read.strip.atScreenPx, why, "credits.strip") ||
        !Factor(strip, "fling_decay_per_tick", read.strip.flingDecayPerTick, why, "credits.strip")) {
        return fail();
    }
    out = std::move(read);
    return true;
}

double UnitsPerSecond(const Rules& rules, const glm::dvec2& viewUnits) {
    // The view is 256 units tall, and a screen atScreenPx tall shows those 256 units:
    // the speed there, in units.
    return rules.strip.screenPxPerSecond * viewUnits.y / rules.strip.atScreenPx;
}

Scroll Start(const glm::dvec2& viewUnits) {
    Scroll scroll;
    scroll.y = viewUnits.y;
    return scroll;
}

void Step(const Rules& rules, Scroll& scroll, const glm::dvec2& viewUnits, double tickMs, bool touching,
          double moveUnitsY) {
    if (touching) {
        scroll.y += moveUnitsY;
        scroll.moveSpeed = moveUnitsY;
    } else {
        scroll.moveSpeed *= rules.strip.flingDecayPerTick;
        scroll.y -= UnitsPerSecond(rules, viewUnits) * tickMs / 1000.0 - scroll.moveSpeed;
    }
    const double minY = -rules.strip.sizeUnits.y;
    if (scroll.y < minY) scroll.y = viewUnits.y;
    if (scroll.y > viewUnits.y) scroll.y = minY;
}

double ColumnX(const Rules& rules, const glm::dvec2& viewUnits) {
    return viewUnits.x * rules.papyrus.xOfWidth.x * rules.papyrus.xOfWidth.y;
}

Hud::Rect BackHitRect(const Rules& rules, const glm::dvec2& viewUnits, double stateMs) {
    const glm::dvec2 at = UiLayer::ButtonAnchorAt(rules.layer, UiLayer::Anchor(rules.back, viewUnits), viewUnits, stateMs);
    return UiLayer::RectAt(rules.back, at);
}

std::vector<Piece> Pieces(const Rules& rules, const glm::dvec2& viewUnits, double stateMs, const Scroll& scroll,
                          bool backHeld) {
    std::vector<Piece> pieces;
    {
        // menu_bg.ent, a scene entity: under the layer.
        Piece bg;
        bg.element = Element::Background;
        bg.file = rules.background.sprite;
        bg.rect = Hud::Rect{rules.background.centreOfScreen * viewUnits - rules.background.sizeUnits * 0.5,
                            rules.background.sizeUnits};
        pieces.push_back(std::move(bg));
    }
    {
        // UILayer::draw, before CreditsScreenLayer::draw's own: the back button at its
        // entrance, bounced about its origin on its own clock.
        Piece back;
        back.element = Element::Back;
        back.file = rules.back.sprite;
        back.rect = MenuState::ScaledAbout(BackHitRect(rules, viewUnits, stateMs), rules.back.origin,
                                           MenuState::BounceScale(rules.backBounce, stateMs));
        back.rgbByte = backHeld ? rules.state.pressTintByte : 255;
        back.alphaByte = UiLayer::ButtonAlphaByte(rules.layer, stateMs);
        pieces.push_back(std::move(back));
    }
    const double x = ColumnX(rules, viewUnits);
    {
        Piece papyrus;
        papyrus.element = Element::Papyrus;
        papyrus.file = rules.papyrus.sprite;
        papyrus.rect = Hud::Rect{glm::dvec2(x, 0.0), rules.papyrus.sizeUnits};
        pieces.push_back(std::move(papyrus));
    }
    {
        Piece strip;
        strip.element = Element::Strip;
        strip.file = rules.strip.sprite;
        strip.rect = Hud::Rect{glm::dvec2(x, scroll.y), rules.strip.sizeUnits};
        strip.alphaByte = rules.strip.alphaByte;
        pieces.push_back(std::move(strip));
    }
    return pieces;
}

} // namespace MagicPortals::Credits
