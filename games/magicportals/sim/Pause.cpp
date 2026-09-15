#include "sim/Pause.hpp"

#include "core/Json.hpp"

#include <utility>

namespace MagicPortals::Pause {

namespace {

namespace Json = Supersonic::Json;
using UiLayer::Read::Byte;
using UiLayer::Read::Fraction;
using UiLayer::Read::Pair;
using UiLayer::Read::Positive;
using UiLayer::Read::ReadPlaced;
using UiLayer::Read::Size;
using UiLayer::Read::Text;

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    Rules read;
    if (!UiLayer::LoadRules(path, read.layer, error)) return false;

    Json::Value root;
    if (!UiLayer::Read::File(path, root, error)) return false;
    const Json::Value& pause = root["pause"];
    if (!pause.IsObject()) {
        error = path + ": pause is not an object";
        return false;
    }
    std::string why;
    const auto fail = [&error, &path, &why]() {
        error = path + ": " + why;
        return false;
    };

    if (!Byte(pause["dim"], "alpha_byte", read.dimAlphaByte, why, "pause.dim")) return fail();

    const Json::Value& current = pause["current_plaque"];
    if (!ReadPlaced(pause["golden_plaque"], "sprite", read.goldenPlaque, why, "pause.golden_plaque") ||
        !ReadPlaced(current, "sprite", read.currentPlaque, why, "pause.current_plaque") ||
        !Fraction(current, "medal_origin", read.medalOrigin, why, "pause.current_plaque") ||
        !Size(current, "medal_size_units", read.medalSizeUnits, why, "pause.current_plaque") ||
        !Text(current, "medal_bronze", read.medalBronze, why, "pause.current_plaque") ||
        !Text(current, "medal_silver", read.medalSilver, why, "pause.current_plaque") ||
        !Text(current, "medal_gold", read.medalGold, why, "pause.current_plaque") ||
        !ReadPlaced(pause["levels_button"], "sprite", read.levels, why, "pause.levels_button") ||
        !ReadPlaced(pause["resume_button"], "sprite", read.resume, why, "pause.resume_button") ||
        !ReadPlaced(pause["skip_button"], "sprite", read.skip, why, "pause.skip_button") ||
        !ReadPlaced(pause["achievements_button"], "sprite", read.achievements, why, "pause.achievements_button") ||
        !ReadPlaced(pause["sound_switch"], "on_sprite", read.sound, why, "pause.sound_switch") ||
        !Text(pause["sound_switch"], "off_sprite", read.soundOffSprite, why, "pause.sound_switch") ||
        !ReadPlaced(pause["music_switch"], "on_sprite", read.music, why, "pause.music_switch") ||
        !Text(pause["music_switch"], "off_sprite", read.musicOffSprite, why, "pause.music_switch")) {
        return fail();
    }

    const Json::Value& title = pause["title"];
    const Json::Value& golden = pause["golden_number"];
    if (!title.IsObject() || !golden.IsObject()) {
        why = "pause.title and pause.golden_number are each an object";
        return fail();
    }
    if (!Text(title, "font", read.title.font, why, "pause.title") ||
        !Text(title, "prefix", read.title.prefix, why, "pause.title") ||
        !Positive(title, "units_per_font_px", read.title.unitsPerFontPx, why, "pause.title") ||
        !Fraction(title, "centre_of_screen", read.title.centreOfScreen, why, "pause.title") ||
        !Text(golden, "font", read.goldenNumber.font, why, "pause.golden_number") ||
        !Positive(golden, "units_per_font_px", read.goldenNumber.unitsPerFontPx, why, "pause.golden_number") ||
        !Pair(golden, "offset_units", read.goldenNumber.offsetUnits, why, "pause.golden_number")) {
        return fail();
    }

    out = std::move(read);
    return true;
}

std::string MedalSprite(const Rules& rules, int savedMedal) {
    if (savedMedal <= 0) return {};
    if (savedMedal == 3) return rules.medalGold;
    if (savedMedal == 2) return rules.medalSilver;
    return rules.medalBronze;
}

std::vector<Sprite> Sprites(const Rules& rules, const Level& level, const Switches& switches,
                            const glm::dvec2& viewUnits, double ms) {
    std::vector<Sprite> sprites;
    const UiLayer::Rules& layer = rules.layer;
    const bool finished = level.savedMedal > 0;

    // THE SPRITES, each a UISprite on the same 1000 ms.
    const auto sprite = [&](Element element, std::string file, const Hud::Rect& rect, int tintAlphaByte) {
        Sprite s;
        s.element = element;
        s.file = std::move(file);
        s.rect = rect;
        s.alphaByte = UiLayer::SpriteAlphaByte(layer, tintAlphaByte, ms);
        sprites.push_back(std::move(s));
    };
    sprite(Element::Dim, std::string(), Hud::Rect{glm::dvec2(0.0), viewUnits}, rules.dimAlphaByte);
    sprite(Element::GoldenPlaque, rules.goldenPlaque.sprite,
           UiLayer::RectAt(rules.goldenPlaque, UiLayer::Anchor(rules.goldenPlaque, viewUnits)), 255);
    if (finished) {
        const glm::dvec2 anchor = UiLayer::Anchor(rules.currentPlaque, viewUnits);
        sprite(Element::CurrentPlaque, rules.currentPlaque.sprite, UiLayer::RectAt(rules.currentPlaque, anchor), 255);
        UiLayer::Placed medal;
        medal.origin = rules.medalOrigin;
        medal.sizeUnits = rules.medalSizeUnits;
        sprite(Element::CurrentMedal, MedalSprite(rules, level.savedMedal), UiLayer::RectAt(medal, anchor), 255);
    }

    // THE BUTTONS, each a UIButton sliding in on the same 700 ms.
    const auto button = [&](Button which, const UiLayer::Placed& placed, std::string file) {
        const glm::dvec2 anchor = UiLayer::Anchor(placed, viewUnits);
        Sprite s;
        s.element = Element::Button;
        s.button = which;
        s.file = std::move(file);
        s.rect = UiLayer::RectAt(placed, UiLayer::ButtonAnchorAt(layer, anchor, viewUnits, ms));
        s.alphaByte = UiLayer::ButtonAlphaByte(layer, ms);
        s.pressable = true;
        sprites.push_back(std::move(s));
    };
    button(Button::Sound, rules.sound, switches.soundOn ? rules.sound.sprite : rules.soundOffSprite);
    button(Button::Levels, rules.levels, rules.levels.sprite);
    button(Button::Resume, rules.resume, rules.resume.sprite);
    if (finished) button(Button::Skip, rules.skip, rules.skip.sprite);
    button(Button::Achievements, rules.achievements, rules.achievements.sprite);

    // The music switch runs on its own clock: dismissed while the sound is off,
    // and a fresh entrance each time it comes back.
    const std::string music = switches.musicOn ? rules.music.sprite : rules.musicOffSprite;
    const glm::dvec2 musicAnchor = UiLayer::Anchor(rules.music, viewUnits);
    const double sinceDismissed = ms - switches.musicDismissedMs;
    if (switches.musicDismissedMs >= 0.0 && sinceDismissed < layer.buttonDismissMs) {
        Sprite s;
        s.element = Element::Button;
        s.button = Button::Music;
        s.file = music;
        s.rect = UiLayer::RectAt(rules.music, UiLayer::ButtonDismissAnchorAt(layer, musicAnchor, viewUnits,
                                                                            sinceDismissed));
        s.alphaByte = UiLayer::ButtonDismissAlphaByte(layer, sinceDismissed);
        s.pressable = false;
        sprites.push_back(std::move(s));
    } else if (switches.soundOn && ms >= switches.musicAddedMs) {
        const double since = ms - switches.musicAddedMs;
        Sprite s;
        s.element = Element::Button;
        s.button = Button::Music;
        s.file = music;
        s.rect = UiLayer::RectAt(rules.music, UiLayer::ButtonAnchorAt(layer, musicAnchor, viewUnits, since));
        s.alphaByte = UiLayer::ButtonAlphaByte(layer, since);
        s.pressable = true;
        sprites.push_back(std::move(s));
    }
    return sprites;
}

std::optional<Button> ButtonAt(const Rules& rules, const Level& level, const Switches& switches,
                               const glm::dvec2& viewUnits, double ms, const glm::dvec2& point) {
    for (const Sprite& sprite : Sprites(rules, level, switches, viewUnits, ms)) {
        if (sprite.element == Element::Button && sprite.pressable && sprite.rect.Contains(point)) return sprite.button;
    }
    return std::nullopt;
}

std::string TitleText(const Rules& rules, const Level& level) {
    return rules.title.prefix + std::to_string(level.index + 1);
}

glm::dvec2 TitleCentre(const Rules& rules, const glm::dvec2& viewUnits) {
    return rules.title.centreOfScreen * viewUnits;
}

std::string GoldenText(const Level& level) {
    return std::to_string(level.goldenScore);
}

glm::dvec2 GoldenCentre(const Rules& rules, const glm::dvec2& viewUnits) {
    return UiLayer::Anchor(rules.goldenPlaque, viewUnits) + rules.goldenNumber.offsetUnits;
}

int TextAlphaByte(const Rules& rules, double ms) {
    return UiLayer::ButtonAlphaByte(rules.layer, ms);
}

} // namespace MagicPortals::Pause
