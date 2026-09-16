#include "sim/MainMenu.hpp"

#include "core/Json.hpp"

#include <cmath>
#include <utility>

namespace MagicPortals::MainMenu {

namespace {

namespace Json = Supersonic::Json;
using UiLayer::Read::Fraction;
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

// A value within 0..1: a grey, an alpha.
bool Unit(const Json::Value& block, const char* key, double& out, std::string& why, const std::string& where) {
    double read = 0.0;
    if (!Number(block, key, read, why, where)) return false;
    if (read < 0.0 || read > 1.0) {
        why = where + "." + key + " is not within 0..1";
        return false;
    }
    out = read;
    return true;
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    Rules read;
    if (!UiLayer::LoadRules(path, read.layer, error) || !MenuState::LoadRules(path, read.state, error)) return false;

    Json::Value root;
    if (!UiLayer::Read::File(path, root, error)) return false;
    const Json::Value& menu = root["main_menu"];
    if (!menu.IsObject()) {
        error = path + ": main_menu is not an object";
        return false;
    }
    std::string why;
    const auto fail = [&error, &path, &why]() {
        error = path + ": " + why;
        return false;
    };

    const Json::Value& background = menu["background"];
    const Json::Value& play = menu["play_button"];
    const Json::Value& title = menu["title"];
    const Json::Value& music = menu["music_switch"];
    if (!background.IsObject() || !play.IsObject() || !play["bounce"].IsObject() || !play["blink"].IsObject() ||
        !title.IsObject() || !title["bob"].IsObject() || !music.IsObject()) {
        why = "main_menu.background, play_button (with bounce and blink), title (with bob) and music_switch are "
              "each an object";
        return fail();
    }
    if (!Text(background, "sprite", read.background.sprite, why, "main_menu.background") ||
        !Size(background, "size_units", read.background.sizeUnits, why, "main_menu.background") ||
        !Fraction(background, "centre_of_screen", read.background.centreOfScreen, why, "main_menu.background")) {
        return fail();
    }

    const Json::Value& bounce = play["bounce"];
    const Json::Value& blink = play["blink"];
    if (!ReadPlaced(play, "sprite", read.play, why, "main_menu.play_button") ||
        !Size(bounce, "scale_a", read.playBounce.scaleA, why, "main_menu.play_button.bounce") ||
        !Size(bounce, "scale_b", read.playBounce.scaleB, why, "main_menu.play_button.bounce") ||
        !Positive(bounce, "stride_ms", read.playBounce.strideMs, why, "main_menu.play_button.bounce") ||
        !Unit(blink, "colour_a", read.playBlink.colourA, why, "main_menu.play_button.blink") ||
        !Unit(blink, "colour_b", read.playBlink.colourB, why, "main_menu.play_button.blink") ||
        !Unit(blink, "alpha_a", read.playBlink.alphaA, why, "main_menu.play_button.blink") ||
        !Unit(blink, "alpha_b", read.playBlink.alphaB, why, "main_menu.play_button.blink") ||
        !Positive(blink, "stride_ms", read.playBlink.strideMs, why, "main_menu.play_button.blink")) {
        return fail();
    }

    if (!ReadPlaced(title, "sprite", read.title, why, "main_menu.title") ||
        !Positive(title["bob"], "amplitude_units", read.bobUnits, why, "main_menu.title.bob") ||
        !Positive(title["bob"], "radians_per_ms", read.bobRadiansPerMs, why, "main_menu.title.bob")) {
        return fail();
    }

    if (!ReadPlaced(menu["credits_button"], "sprite", read.credits, why, "main_menu.credits_button") ||
        !ReadPlaced(menu["achievements_button"], "sprite", read.achievements, why, "main_menu.achievements_button") ||
        !ReadPlaced(menu["sound_switch"], "on_sprite", read.sound, why, "main_menu.sound_switch") ||
        !Text(menu["sound_switch"], "off_sprite", read.soundOffSprite, why, "main_menu.sound_switch")) {
        return fail();
    }

    if (!Text(music, "on_sprite", read.music.sprite, why, "main_menu.music_switch") ||
        !Text(music, "off_sprite", read.music.offSprite, why, "main_menu.music_switch") ||
        !Number(music, "at_units_x", read.music.atUnitsX, why, "main_menu.music_switch") ||
        !Unit(music, "at_screen_y", read.music.atScreenY, why, "main_menu.music_switch") ||
        !Fraction(music, "origin", read.music.origin, why, "main_menu.music_switch") ||
        !Size(music, "size_units", read.music.sizeUnits, why, "main_menu.music_switch")) {
        return fail();
    }
    if (read.music.atUnitsX < 0.0) {
        why = "main_menu.music_switch.at_units_x is below zero";
        return fail();
    }

    out = std::move(read);
    return true;
}

glm::dvec2 MusicAnchor(const Rules& rules, const glm::dvec2& viewUnits) {
    return glm::dvec2(rules.music.atUnitsX, rules.music.atScreenY * viewUnits.y);
}

std::vector<Piece> Pieces(const Rules& rules, const Pause::Switches& switches, const glm::dvec2& viewUnits,
                          double stateMs, double wallMs, unsigned held) {
    std::vector<Piece> pieces;
    const UiLayer::Rules& layer = rules.layer;

    // THE BACKGROUND, menu_bg.ent: a scene entity, so under every button, and
    // untouched by the entrance.
    {
        Piece bg;
        bg.element = Element::Background;
        bg.file = rules.background.sprite;
        bg.rect = Hud::Rect{rules.background.centreOfScreen * viewUnits - rules.background.sizeUnits * 0.5,
                            rules.background.sizeUnits};
        bg.hitRect = bg.rect;
        pieces.push_back(std::move(bg));
    }

    // A UIButton at `anchor` (its place once the entrance is over), `ms` into its
    // entrance, with a sprite `origin` and `size`, plus whatever its own effects
    // add. The entrance alpha is the custom colour's; the press tint the button's.
    const auto button = [&](Button which, std::string file, const glm::dvec2& anchor, const glm::dvec2& origin,
                            const glm::dvec2& size, double ms, const MenuState::Bounce* bounce,
                            const MenuState::Blink* blink, const glm::dvec2& offset) {
        const glm::dvec2 at = UiLayer::ButtonAnchorAt(layer, anchor, viewUnits, ms);
        const Hud::Rect rect{at - size * origin, size};
        Piece p;
        p.element = Element::Button;
        p.button = which;
        p.file = std::move(file);
        p.hitRect = rect;
        // Button::draw: drawScaledSprite(pos + offset, bounce scale, origin). The
        // bounce and blink run on the button's own clock, from its creation.
        const glm::dvec2 scale = bounce != nullptr ? MenuState::BounceScale(*bounce, ms) : glm::dvec2(1.0);
        p.rect = MenuState::ScaledAbout(Hud::Rect{rect.min + offset, rect.size}, origin, scale);
        const MenuState::BlinkValue value = blink != nullptr ? MenuState::BlinkAt(*blink, ms) : MenuState::BlinkValue{};
        const int tint = (held & Bit(which)) != 0u ? rules.state.pressTintByte : 255;
        p.rgbByte = MenuState::ChannelByte(tint, 255, value.colour);
        p.alphaByte = MenuState::ChannelByte(255, UiLayer::ButtonAlphaByte(layer, ms), value.alpha);
        p.pressable = true;
        pieces.push_back(std::move(p));
    };
    const auto placed = [&](Button which, const UiLayer::Placed& place, std::string file,
                            const MenuState::Bounce* bounce = nullptr, const MenuState::Blink* blink = nullptr,
                            const glm::dvec2& offset = glm::dvec2(0.0)) {
        button(which, std::move(file), UiLayer::Anchor(place, viewUnits), place.origin, place.sizeUnits, stateMs,
               bounce, blink, offset);
    };

    // THE LAYER, in the order its buttons were added (UILayer::draw).
    placed(Button::Sound, rules.sound, switches.soundOn ? rules.sound.sprite : rules.soundOffSprite);
    placed(Button::Play, rules.play, rules.play.sprite, &rules.playBounce, &rules.playBlink);
    // doTitleAnimation's offset: GetTimeF, so the layer's clock, not the state's.
    const glm::dvec2 bob(0.0, std::sin(wallMs * rules.bobRadiansPerMs) * rules.bobUnits);
    placed(Button::Title, rules.title, rules.title.sprite, nullptr, nullptr, bob);
    placed(Button::Credits, rules.credits, rules.credits.sprite);
    placed(Button::Achievements, rules.achievements, rules.achievements.sprite);

    // The music switch on its own clock, as on the pause: dismissed while the
    // sound is off, and a fresh entrance each time it comes back.
    const std::string music = switches.musicOn ? rules.music.sprite : rules.music.offSprite;
    const glm::dvec2 musicAnchor = MusicAnchor(rules, viewUnits);
    const double sinceDismissed = stateMs - switches.musicDismissedMs;
    if (switches.musicDismissedMs >= 0.0 && sinceDismissed < layer.buttonDismissMs) {
        const glm::dvec2 at = UiLayer::ButtonDismissAnchorAt(layer, musicAnchor, viewUnits, sinceDismissed);
        Piece p;
        p.element = Element::Button;
        p.button = Button::Music;
        p.file = music;
        p.rect = Hud::Rect{at - rules.music.sizeUnits * rules.music.origin, rules.music.sizeUnits};
        p.hitRect = p.rect;
        p.alphaByte = UiLayer::ButtonDismissAlphaByte(layer, sinceDismissed);
        p.pressable = false;
        pieces.push_back(std::move(p));
    } else if (switches.soundOn && stateMs >= switches.musicAddedMs) {
        button(Button::Music, music, musicAnchor, rules.music.origin, rules.music.sizeUnits,
               stateMs - switches.musicAddedMs, nullptr, nullptr, glm::dvec2(0.0));
    }
    return pieces;
}

unsigned ButtonsAt(const Rules& rules, const Pause::Switches& switches, const glm::dvec2& viewUnits, double stateMs,
                   const glm::dvec2& point) {
    // Every button of the layer is updated, and each follows a touch inside its
    // own rectangle (Button::isPointInButton): at its entrance place, with no
    // bounce and no bob.
    unsigned inside = 0u;
    for (const Piece& piece : Pieces(rules, switches, viewUnits, stateMs, 0.0, 0u)) {
        if (piece.element == Element::Button && piece.pressable && piece.hitRect.Contains(point)) {
            inside |= Bit(piece.button);
        }
    }
    return inside;
}

std::optional<Button> FirstActed(unsigned buttons) {
    for (const Button button :
         {Button::Sound, Button::Music, Button::Play, Button::Credits, Button::Title, Button::Achievements}) {
        if ((buttons & Bit(button)) != 0u) return button;
    }
    return std::nullopt;
}

Hud::Rect SettledPlayRect(const Rules& rules, const glm::dvec2& viewUnits) {
    return UiLayer::RectAt(rules.play, UiLayer::Anchor(rules.play, viewUnits));
}

} // namespace MagicPortals::MainMenu
