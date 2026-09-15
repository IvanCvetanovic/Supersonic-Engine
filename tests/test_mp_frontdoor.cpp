// The game's front door, as numbers: the loading screen, the main menu, and what
// every menu state shares - its black, its buttons' press, bounce and blink.
//
// sim/MenuState.hpp, sim/MainMenu.hpp and sim/Loading.hpp turn the port's ui.json
// into rectangles, alphas and timelines, and this pins both halves against the
// remake's ui3 spec (sections 0.3-0.7 and 2, acceptance 7.1 and 7.2). Every
// position is stated in the pixels of a 1280x720 capture of the original, at
// 2.8125 px a design unit, so a number here can be held against static.md or
// motion.md without converting anything back.
//
// Pure: no window, no registry, no level, and only the port's own committed data.

#include "TestHarness.hpp"

#include "sim/Hud.hpp"
#include "sim/Loading.hpp"
#include "sim/MainMenu.hpp"
#include "sim/MenuState.hpp"
#include "sim/Pause.hpp"
#include "sim/UiLayer.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;

namespace {

const std::string kUi = std::string(MAGICPORTALS_PORT_DATA_DIR) + "/ui.json";

constexpr double kPxPerUnit = 2.8125;
const glm::dvec2 kView720(1280.0 / kPxPerUnit, 720.0 / kPxPerUnit); // 455.11 x 256
const glm::dvec2 kView43(256.0 * 4.0 / 3.0, 256.0);                 // 341.33 x 256
// The port's tick, as the layer runs it.
constexpr double kTickMs = 1000.0 / 60.0;

bool Near(double a, double b, double eps) {
    return std::fabs(a - b) <= eps;
}

std::string Num(double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.4f", v);
    return buffer;
}

std::string Px(const glm::dvec2& units) {
    return "(" + Num(units.x * kPxPerUnit) + ", " + Num(units.y * kPxPerUnit) + ") px";
}

bool TopLeftPx(const Hud::Rect& rect, double x, double y, double eps = 0.06) {
    return Near(rect.min.x * kPxPerUnit, x, eps) && Near(rect.min.y * kPxPerUnit, y, eps);
}

bool SizePx(const Hud::Rect& rect, double w, double h, double eps = 0.06) {
    return Near(rect.size.x * kPxPerUnit, w, eps) && Near(rect.size.y * kPxPerUnit, h, eps);
}

MainMenu::Rules LoadMenu() {
    MainMenu::Rules rules;
    std::string error;
    CHECK_MSG(MainMenu::LoadRules(kUi, rules, error), "ui.json's main menu reads: " + error);
    return rules;
}

Loading::Rules LoadLoading() {
    Loading::Rules rules;
    std::string error;
    CHECK_MSG(Loading::LoadRules(kUi, rules, error), "ui.json's loading screen reads: " + error);
    return rules;
}

const MainMenu::Piece* Find(const std::vector<MainMenu::Piece>& pieces, MainMenu::Button button) {
    for (const MainMenu::Piece& piece : pieces) {
        if (piece.element == MainMenu::Element::Button && piece.button == button) return &piece;
    }
    return nullptr;
}

std::filesystem::path Scratch() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-mp-frontdoor";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string Write(const char* name, const std::string& text) {
    const std::filesystem::path path = Scratch() / name;
    std::ofstream file(path, std::ios::trunc);
    file << text;
    return path.string();
}

// ---- what every menu state shares ---------------------------------------------

void TheFileSaysWhatWasDecoded() {
    const MainMenu::Rules rules = LoadMenu();
    CHECK_EQ(rules.state.fadeMs, 700.0);        // FadeInController.time
    CHECK_EQ(rules.state.pressTintByte, 204);   // 0xFFCCCCCC
    CHECK_EQ(rules.state.tileTravelUnits, 48.0); // scale(48)
    // The entrance is ui_layer.button's, unchanged.
    CHECK_EQ(rules.layer.buttonAppearMs, 700.0);
    CHECK_EQ(rules.layer.buttonSlideUnits, 32.0);
}

// A-S1: one black a state, linear, from the state's first frame.
void EveryStateOpensUnderABlack() {
    const MenuState::Rules rules = LoadMenu().state;
    CHECK_EQ(MenuState::FadeAlphaByte(rules, 0.0), 255);
    // The gains A-S1 holds a capture to, as 1 - alpha: 0.143 / 0.50 / 0.857 +-0.03.
    const auto gain = [&rules](double ms) { return 1.0 - MenuState::FadeAlphaByte(rules, ms) / 255.0; };
    CHECK_MSG(Near(gain(100.0), 0.143, 0.005), "0.10 s: " + Num(gain(100.0)));
    CHECK_MSG(Near(gain(350.0), 0.50, 0.005), "0.35 s: " + Num(gain(350.0)));
    CHECK_MSG(Near(gain(600.0), 0.857, 0.005), "0.60 s: " + Num(gain(600.0)));
    // iTOb(fTOu(bias * 255)): truncated, not rounded.
    CHECK_EQ(MenuState::FadeAlphaByte(rules, 100.0), 218);
    CHECK_EQ(MenuState::FadeAlphaByte(rules, 699.0), 0);
    CHECK_EQ(MenuState::FadeAlphaByte(rules, 700.0), 0);
    CHECK_EQ(MenuState::FadeAlphaByte(rules, 5000.0), 0);
    // Linear, so the tick 21 frame (350 ms) is half: never smoothEnd's 0.707.
    CHECK_EQ(MenuState::FadeAlphaByte(rules, 21.0 * kTickMs), 127);
}

// Button::bounce and Button::blinkColor on TAP START's numbers (A-M4).
void TapStartBreathesAndBlinks() {
    const MainMenu::Rules rules = LoadMenu();
    CHECK_EQ(MenuState::Triangle(0.0, 400.0), 0.0);
    CHECK_EQ(MenuState::Triangle(200.0, 400.0), 0.5);
    CHECK_EQ(MenuState::Triangle(400.0, 400.0), 1.0);
    CHECK_EQ(MenuState::Triangle(600.0, 400.0), 0.5);
    CHECK_EQ(MenuState::Triangle(800.0, 400.0), 0.0);

    // Scale (0.99, 1.01) at a stride start, (1.01, 0.99) at its end, in antiphase,
    // eased by smoothEnd within the stride.
    const glm::dvec2 start = MenuState::BounceScale(rules.playBounce, 0.0);
    const glm::dvec2 end = MenuState::BounceScale(rules.playBounce, 400.0);
    const glm::dvec2 mid = MenuState::BounceScale(rules.playBounce, 200.0);
    CHECK_MSG(Near(start.x, 0.99, 1e-9) && Near(start.y, 1.01, 1e-9), "stride start " + Num(start.x) + " x " + Num(start.y));
    CHECK_MSG(Near(end.x, 1.01, 1e-9) && Near(end.y, 0.99, 1e-9), "stride end " + Num(end.x) + " x " + Num(end.y));
    CHECK_MSG(Near(mid.x, 0.99 + 0.02 * std::sin(3.14159265358979 / 4.0), 1e-9), "smoothEnd, not linear: " + Num(mid.x));
    CHECK_MSG(Near(mid.x + mid.y, 2.0, 1e-9), "sx and sy always sum to 2: the antiphase A-M4 measures");
    // A button with no bounce is left whole.
    CHECK(MenuState::BounceScale(MenuState::Bounce{}, 123.0) == glm::dvec2(1.0));

    // Alpha 0.25 -> 1.0 and colour 0.95 -> 1.0, LINEAR, one bias for both: the dip
    // is at the dim end (spec 0.7's MERGE reading).
    const MenuState::BlinkValue dim = MenuState::BlinkAt(rules.playBlink, 0.0);
    const MenuState::BlinkValue bright = MenuState::BlinkAt(rules.playBlink, 400.0);
    const MenuState::BlinkValue half = MenuState::BlinkAt(rules.playBlink, 200.0);
    CHECK(Near(dim.alpha, 0.25, 1e-12) && Near(dim.colour, 0.95, 1e-12));
    CHECK(Near(bright.alpha, 1.0, 1e-12) && Near(bright.colour, 1.0, 1e-12));
    CHECK_MSG(Near(half.alpha, 0.625, 1e-12) && Near(half.colour, 0.975, 1e-12), "linear: " + Num(half.alpha));
    CHECK_MSG(Near(dim.alpha * dim.colour, 0.2375, 1e-12), "brightness x alpha bottoms out at 0.2375 (A-M4)");
    // The period is two strides: 800 ms, 48 ticks.
    CHECK(Near(MenuState::BlinkAt(rules.playBlink, 48.0 * kTickMs).alpha, 0.25, 1e-9));

    // Button::draw's bytes: the press tint 0.80, times the blink, truncated.
    CHECK_EQ(MenuState::ChannelByte(204, 255, 1.0), 204);
    CHECK_EQ(MenuState::ChannelByte(255, 255, 0.95), 242);
    CHECK_EQ(MenuState::ChannelByte(204, 255, 0.95), 193);
    CHECK_EQ(MenuState::ChannelByte(255, 255, 0.25), 63);
    CHECK_EQ(MenuState::ChannelByte(255, 127, 0.25), 31);
}

// A-S6: a page tile refuses a touch that has travelled more than 48 u, the
// LARGEST distance while held, not where it was let go.
void ATileRefusesATouchThatTravelled() {
    const MenuState::Rules rules = LoadMenu().state;
    MenuState::Touch touch;
    MenuState::TouchDown(touch, glm::dvec2(100.0, 100.0));
    MenuState::TouchHeld(touch, glm::dvec2(100.0, 140.0));
    CHECK_MSG(MenuState::TileTakes(rules, touch), "40 u: taken");
    MenuState::TouchHeld(touch, glm::dvec2(100.0, 160.0));
    CHECK_MSG(!MenuState::TileTakes(rules, touch), "60 u: refused");
    MenuState::TouchHeld(touch, glm::dvec2(100.0, 101.0));
    CHECK_MSG(!MenuState::TileTakes(rules, touch), "and still refused back where it went down");
    MenuState::TouchDown(touch, glm::dvec2(0.0));
    MenuState::TouchHeld(touch, glm::dvec2(48.0, 0.0));
    CHECK_MSG(MenuState::TileTakes(rules, touch), "exactly 48 u is not more than 48");
}

// ---- the main menu ------------------------------------------------------------------

// A-M1, A-M2: every element where static.md measured it, once settled.
void TheMainMenuSitsWhereTheStillsPutIt() {
    const MainMenu::Rules rules = LoadMenu();
    const Pause::Switches on;
    // 48 ticks a period, 1 s in: a blink stride start, as main_menu.png is.
    const double settled = 96.0 * kTickMs;
    const std::vector<MainMenu::Piece> pieces = MainMenu::Pieces(rules, on, kView720, settled, 0.0, 0u);
    CHECK_EQ(pieces.size(), static_cast<std::size_t>(7));
    if (pieces.size() != 7) return;

    CHECK(pieces[0].element == MainMenu::Element::Background);
    CHECK_MSG(TopLeftPx(pieces[0].rect, -80.0, 0.0) && SizePx(pieces[0].rect, 1440.0, 720.0),
              "M0 top-left " + Px(pieces[0].rect.min));
    CHECK(pieces[0].file == "entities/main_menu_bg.png");

    // UILayer::draw's order: the sound switch the panel adds first, TAP START, the
    // title over it, info, Achievements, then the music switch.
    const MainMenu::Button order[] = {MainMenu::Button::Sound, MainMenu::Button::Play,
                                      MainMenu::Button::Title, MainMenu::Button::Credits,
                                      MainMenu::Button::Achievements, MainMenu::Button::Music};
    for (std::size_t i = 0; i < 6; ++i) CHECK(pieces[i + 1].button == order[i]);

    const MainMenu::Piece* play = Find(pieces, MainMenu::Button::Play);
    const MainMenu::Piece* title = Find(pieces, MainMenu::Button::Title);
    const MainMenu::Piece* credits = Find(pieces, MainMenu::Button::Credits);
    const MainMenu::Piece* scores = Find(pieces, MainMenu::Button::Achievements);
    const MainMenu::Piece* sound = Find(pieces, MainMenu::Button::Sound);
    const MainMenu::Piece* music = Find(pieces, MainMenu::Button::Music);
    if (!play || !title || !credits || !scores || !sound || !music) {
        CHECK_MSG(false, "every element of the menu is there");
        return;
    }
    CHECK_MSG(TopLeftPx(play->hitRect, 280.0, 385.2) && SizePx(play->hitRect, 720.0, 180.0),
              "M1 " + Px(play->hitRect.min));
    CHECK_MSG(Near(play->rect.Centre().x * kPxPerUnit, 640.0, 1e-6) && Near(play->rect.Centre().y * kPxPerUnit, 475.2, 1e-6),
              "M1 scales about its centre (640, 475.2): " + Px(play->rect.Centre()));
    CHECK_MSG(SizePx(play->rect, 720.0 * 0.99, 180.0 * 1.01), "M1 at the stride start is 0.99 x 1.01");
    CHECK_EQ(play->alphaByte, 63);  // 0.25
    CHECK_EQ(play->rgbByte, 242);   // 0.95
    CHECK_MSG(TopLeftPx(title->rect, 280.0, -180.0) && SizePx(title->rect, 720.0, 720.0),
              "M2 at the bob's zero " + Px(title->rect.min));
    CHECK_MSG(TopLeftPx(credits->rect, 0.0, 0.0) && SizePx(credits->rect, 90.0, 90.0), "M3 " + Px(credits->rect.min));
    CHECK_MSG(TopLeftPx(scores->rect, 920.0, 630.0) && SizePx(scores->rect, 360.0, 90.0), "M4 " + Px(scores->rect.min));
    CHECK_MSG(TopLeftPx(sound->rect, 0.0, 630.0) && SizePx(sound->rect, 90.0, 90.0), "M5 " + Px(sound->rect.min));
    CHECK_MSG(TopLeftPx(music->rect, 90.0, 630.0) && SizePx(music->rect, 90.0, 90.0),
              "M6 at scale(32), not the pause's 115.2: " + Px(music->rect.min));
    for (const MainMenu::Piece* button : {title, credits, scores, sound, music}) {
        CHECK_EQ(button->alphaByte, 255);
        CHECK_EQ(button->rgbByte, 255);
    }
    CHECK(sound->file == "sound_high.png" && music->file == "music_on.png");
    CHECK(play->file == "main_play_game_button.png" && title->file == "game_main_title.png");
    CHECK(credits->file == "credits.png" && scores->file == "scores.png");

    // At 4:3 the fractions move with the width and the sizes stay; the music
    // switch stays 32 units in.
    const std::vector<MainMenu::Piece> narrow = MainMenu::Pieces(rules, on, kView43, settled, 0.0, 0u);
    const MainMenu::Piece* narrowMusic = Find(narrow, MainMenu::Button::Music);
    const MainMenu::Piece* narrowScores = Find(narrow, MainMenu::Button::Achievements);
    const MainMenu::Piece* narrowPlay = Find(narrow, MainMenu::Button::Play);
    if (narrowMusic && narrowScores && narrowPlay) {
        CHECK_MSG(Near(narrowMusic->rect.min.x, 32.0, 1e-9), "4:3: the music switch stays 32 u in");
        CHECK(Near(narrowScores->rect.min.x, kView43.x - 128.0, 1e-9));
        CHECK(Near(narrowPlay->hitRect.Centre().x, kView43.x * 0.5, 1e-9) && Near(narrowPlay->hitRect.size.x, 256.0, 1e-9));
        CHECK(Near(narrow[0].rect.min.x, kView43.x * 0.5 - 256.0, 1e-9));
    }

    // The settled button a suite presses.
    const Hud::Rect settledPlay = MainMenu::SettledPlayRect(rules, kView720);
    CHECK(TopLeftPx(settledPlay, 280.0, 385.2) && SizePx(settledPlay, 720.0, 180.0));
}

// A-M5: the bob, on the layer's clock.
void TheTitleBobs() {
    const MainMenu::Rules rules = LoadMenu();
    const Pause::Switches on;
    const auto titleY = [&](double wallMs) {
        const std::vector<MainMenu::Piece> pieces = MainMenu::Pieces(rules, on, kView720, 5000.0, wallMs, 0u);
        const MainMenu::Piece* title = Find(pieces, MainMenu::Button::Title);
        return title != nullptr ? title->rect.min.y * kPxPerUnit : 0.0;
    };
    const double period = 2.0 * 3.14159265358979323846 / rules.bobRadiansPerMs;
    CHECK_MSG(Near(period, 3141.59, 0.01), "period " + Num(period) + " ms (A-M5: 3141.6 +-1%)");
    CHECK_MSG(Near(titleY(period / 4.0), -180.0 + 3.9375, 1e-6), "amplitude 1.4 u = 3.94 px: " + Num(titleY(period / 4.0)));
    CHECK_MSG(Near(titleY(period * 0.75), -180.0 - 3.9375, 1e-6), "and the other way");
    CHECK(Near(titleY(period), -180.0, 1e-6));
    // It moves the picture and not the button: the rectangle a touch lands in
    // stays where the entrance put it.
    const std::vector<MainMenu::Piece> pieces = MainMenu::Pieces(rules, on, kView720, 5000.0, period / 4.0, 0u);
    if (const MainMenu::Piece* title = Find(pieces, MainMenu::Button::Title)) {
        CHECK(TopLeftPx(title->hitRect, 280.0, -180.0));
        CHECK(Near(title->rect.min.x, title->hitRect.min.x, 1e-12));
    }
    // The state's clock does not move it: only the layer's.
    const std::vector<MainMenu::Piece> later = MainMenu::Pieces(rules, on, kView720, 9000.0, 0.0, 0u);
    if (const MainMenu::Piece* title = Find(later, MainMenu::Button::Title)) CHECK(TopLeftPx(title->rect, 280.0, -180.0));
}

// A-S2, A-S3, A-S4, A-M7: every element comes in on the state's first frame, 32 u
// along the ray from the screen centre through its ANCHOR, alpha linear.
void TheMenuComesInAsUIButtonsDo() {
    const MainMenu::Rules rules = LoadMenu();
    const Pause::Switches on;
    const std::vector<MainMenu::Piece> first = MainMenu::Pieces(rules, on, kView720, 0.0, 0.0, 0u);
    const std::vector<MainMenu::Piece> home = MainMenu::Pieces(rules, on, kView720, 700.0 + kTickMs, 0.0, 0u);
    const auto offsetUnits = [&](MainMenu::Button button, const std::vector<MainMenu::Piece>& at) {
        const MainMenu::Piece* now = Find(at, button);
        const MainMenu::Piece* settled = Find(home, button);
        return now && settled ? now->hitRect.min - settled->hitRect.min : glm::dvec2(1e9);
    };
    const auto offsetIs = [&](MainMenu::Button button, double x, double y, const char* name) {
        const glm::dvec2 d = offsetUnits(button, first);
        CHECK_MSG(Near(d.x, x, 0.01) && Near(d.y, y, 0.01),
                  std::string(name) + " starts (" + Num(d.x) + ", " + Num(d.y) + ") u out");
        CHECK_MSG(Near(glm::length(d), 32.0, 1e-9), std::string(name) + " starts 32 u out");
    };
    offsetIs(MainMenu::Button::Title, 0.0, -32.0, "M2");
    offsetIs(MainMenu::Button::Play, 0.0, 32.0, "M1");
    offsetIs(MainMenu::Button::Credits, -27.89, -15.69, "M3");
    offsetIs(MainMenu::Button::Achievements, 27.89, 15.69, "M4");
    offsetIs(MainMenu::Button::Sound, -27.89, 15.69, "M5");
    // A-S3: (-75.3, +49.3) px, along the ray through (32 u, 256 u).
    offsetIs(MainMenu::Button::Music, -26.77, 17.53, "M6");
    const glm::dvec2 m4 = offsetUnits(MainMenu::Button::Achievements, first) * kPxPerUnit;
    CHECK_MSG(Near(m4.x, 78.4, 0.06) && Near(m4.y, 44.1, 0.06), "A-S3 M4 " + Num(m4.x) + ", " + Num(m4.y));

    // A-S2's remaining offsets: 69.97 / 42.12 / 8.91 px at 0.10 / 0.25 / 0.50 s.
    const struct {
        double ms;
        double px;
    } remaining[] = {{100.0, 69.97}, {250.0, 42.12}, {500.0, 8.91}};
    for (const auto& r : remaining) {
        const double left = glm::length(offsetUnits(MainMenu::Button::Title, MainMenu::Pieces(rules, on, kView720, r.ms, 0.0, 0u))) *
                            kPxPerUnit;
        CHECK_MSG(Near(left, r.px, 0.02), "A-S2 at " + Num(r.ms) + " ms: " + Num(left) + " px left");
    }
    // Home once the 700 ms are past.
    CHECK(Near(glm::length(offsetUnits(MainMenu::Button::Title, home)), 0.0, 1e-12));

    // A-S4: alpha linear over 700 ms, every button together (A-M7). TAP START's
    // is the entrance's times its blink.
    for (const MainMenu::Piece& piece : first) {
        if (piece.element == MainMenu::Element::Button) CHECK_EQ(piece.alphaByte, 0);
    }
    const std::vector<MainMenu::Piece> half = MainMenu::Pieces(rules, on, kView720, 350.0, 0.0, 0u);
    if (const MainMenu::Piece* title = Find(half, MainMenu::Button::Title)) CHECK_EQ(title->alphaByte, 127);
    if (const MainMenu::Piece* play = Find(half, MainMenu::Button::Play)) {
        // 350 ms is 7/8 of a rising stride: blink alpha 0.25 + 0.75 * 0.875.
        const int expected = MenuState::ChannelByte(255, 127, 0.25 + 0.75 * 0.875);
        CHECK_EQ(play->alphaByte, expected);
    }
}

// A-M3 and the sound panel's rules: muted, the music switch goes out over 700 ms
// and is not drawn; back on, it comes in afresh.
void TheSoundSwitchTakesTheMusicSwitchWithIt() {
    const MainMenu::Rules rules = LoadMenu();
    Pause::Switches muted;
    muted.soundOn = false;
    muted.musicDismissedMs = 1000.0;
    // A copy: the pieces are a temporary.
    const auto music = [&](const Pause::Switches& switches, double ms) -> std::optional<MainMenu::Piece> {
        const std::vector<MainMenu::Piece> pieces = MainMenu::Pieces(rules, switches, kView720, ms, 0.0, 0u);
        const MainMenu::Piece* found = Find(pieces, MainMenu::Button::Music);
        return found != nullptr ? std::optional<MainMenu::Piece>(*found) : std::nullopt;
    };
    const std::vector<MainMenu::Piece> pieces = MainMenu::Pieces(rules, muted, kView720, 3000.0, 0.0, 0u);
    const MainMenu::Piece* sound = Find(pieces, MainMenu::Button::Sound);
    CHECK(sound != nullptr && sound->file == "sound_mute.png" && TopLeftPx(sound->rect, 0.0, 630.0));
    CHECK_MSG(!music(muted, 3000.0).has_value(), "A-M3: no music switch while muted");
    const std::optional<MainMenu::Piece> leaving = music(muted, 1350.0);
    CHECK_MSG(leaving && !leaving->pressable && leaving->alphaByte < 255, "on its way out 350 ms after");
    CHECK_MSG(MainMenu::ButtonsAt(rules, muted, kView720, 1350.0, glm::dvec2(135.0, 675.0) / kPxPerUnit) == 0u,
              "and a touch cannot land on it");

    Pause::Switches back;
    back.musicAddedMs = 4000.0;
    const std::optional<MainMenu::Piece> fresh = music(back, 4000.0);
    CHECK_MSG(fresh && fresh->alphaByte == 0, "back on: a fresh entrance");
    back.musicOn = false;
    const std::optional<MainMenu::Piece> off = music(back, 6000.0);
    CHECK(off && off->file == "music_off.png");
}

// A touch lands in the rectangles Button::isPointInButton tests, and the held
// button draws at the press tint.
void AMenuTouchLandsOnTheButtonsItSees() {
    const MainMenu::Rules rules = LoadMenu();
    const Pause::Switches on;
    const auto at = [&](double x, double y) {
        return MainMenu::ButtonsAt(rules, on, kView720, 2000.0, glm::dvec2(x, y) / kPxPerUnit);
    };
    CHECK_EQ(at(640.0, 550.0), MainMenu::Bit(MainMenu::Button::Play));
    CHECK_EQ(at(640.0, 450.0), MainMenu::Bit(MainMenu::Button::Play) | MainMenu::Bit(MainMenu::Button::Title));
    CHECK(MainMenu::FirstActed(at(640.0, 450.0)) == MainMenu::Button::Play);
    CHECK_EQ(at(640.0, 200.0), MainMenu::Bit(MainMenu::Button::Title));
    CHECK_EQ(at(45.0, 45.0), MainMenu::Bit(MainMenu::Button::Credits));
    CHECK_EQ(at(1100.0, 675.0), MainMenu::Bit(MainMenu::Button::Achievements));
    CHECK_EQ(at(45.0, 675.0), MainMenu::Bit(MainMenu::Button::Sound));
    CHECK_EQ(at(135.0, 675.0), MainMenu::Bit(MainMenu::Button::Music));
    CHECK_EQ(at(1100.0, 300.0), 0u);
    CHECK(!MainMenu::FirstActed(0u).has_value());
    // Where a button is on the tick: 100 ms into the entrance, info has not come
    // down to its corner yet.
    CHECK_EQ(MainMenu::ButtonsAt(rules, on, kView720, 100.0, glm::dvec2(80.0, 80.0) / kPxPerUnit), 0u);

    const std::vector<MainMenu::Piece> held =
        MainMenu::Pieces(rules, on, kView720, 96.0 * kTickMs, 0.0, MainMenu::Bit(MainMenu::Button::Play));
    if (const MainMenu::Piece* play = Find(held, MainMenu::Button::Play)) {
        CHECK_MSG(play->rgbByte == 193, "held at a stride start: 0.80 x 0.95 = " + std::to_string(play->rgbByte));
    }
    if (const MainMenu::Piece* title = Find(held, MainMenu::Button::Title)) CHECK_EQ(title->rgbByte, 255);
}

// ---- the loading screen ------------------------------------------------------------

void TheLoadingScreenWalksAndHolds() {
    const Loading::Rules rules = LoadLoading();
    CHECK_EQ(rules.resources, 162);
    CHECK_EQ(rules.resourcesPerFrame, 2);
    CHECK_EQ(Loading::LastLoadingFrame(rules), 81);
    CHECK_EQ(Loading::LoadedBefore(rules, 1), 0);
    CHECK_EQ(Loading::LoadedBefore(rules, 81), 160);
    CHECK_EQ(Loading::LoadedBefore(rules, 82), 162);
    CHECK(Loading::IsLoading(rules, 81) && !Loading::IsLoading(rules, 82));
    CHECK(Loading::CharacterShown(rules, 80) && !Loading::CharacterShown(rules, 81));

    // m_loadedTime > 1000 of frame time from frame 82: the 61st frame of it, so the
    // menu is set on frame 142 and its first frame is the 143rd. Whichever way the
    // tick's milliseconds are summed.
    CHECK_EQ(Loading::MenuFrame(rules, kTickMs), 142);
    CHECK_EQ(Loading::MenuFrame(rules, static_cast<double>(1.0f / 60.0f) * 1000.0), 142);
    CHECK(!Loading::HoldOver(rules, 141, kTickMs) && Loading::HoldOver(rules, 142, kTickMs));

    // The walk: 0.35 of the width to 0.65, by what is loaded.
    const glm::dvec2 start = Loading::CharacterAt(rules, kView720, 1) * kPxPerUnit;
    CHECK_MSG(Near(start.x, 448.0, 1e-6) && Near(start.y, 360.0, 1e-6), "start " + Num(start.x));
    const glm::dvec2 last = Loading::CharacterAt(rules, kView720, 81) * kPxPerUnit;
    CHECK_MSG(Near(last.x, 448.0 + 384.0 * 160.0 / 162.0, 1e-6), "last " + Num(last.x));
    CHECK(Loading::CharacterAt(rules, kView720, 200) == Loading::CharacterAt(rules, kView720, 81));
    CHECK(Near(Loading::PortalAt(rules, kView720).x * kPxPerUnit, 832.0, 1e-6));

    // FrameTimer::set(8, 11, 90): 8 for the first 90 ms, one on each 90 ms.
    CHECK_EQ(Loading::CharacterFrame(rules, 1, kTickMs), 8);
    CHECK_EQ(Loading::CharacterFrame(rules, 6, kTickMs), 8);  // 83 ms
    CHECK_EQ(Loading::CharacterFrame(rules, 7, kTickMs), 9);  // 100 ms
    CHECK_EQ(Loading::CharacterFrame(rules, 28, kTickMs), 9); // 450 ms: 5 steps, round the four
    CHECK_EQ(Loading::CharacterFrame(rules, 22, kTickMs), 11); // 350 ms: 3 steps
    CHECK_EQ(Loading::CharacterFrame(rules, 23, kTickMs), 8);  // 367 ms: the fourth wraps past 11

    // m_swapStrings[loaded mod 26], each pattern twice.
    CHECK(Loading::DotsText(rules, 0) == "          ");
    CHECK(Loading::DotsText(rules, 2) == ".         ");
    CHECK(Loading::DotsText(rules, 3) == ".         ");
    CHECK(Loading::DotsText(rules, 6) == "...       ");
    CHECK(Loading::DotsText(rules, 8) == " ...      ");
    CHECK(Loading::DotsText(rules, 20) == "       ...");
    CHECK(Loading::DotsText(rules, 22) == "        ..");
    CHECK(Loading::DotsText(rules, 24) == "         .");
    CHECK(Loading::DotsText(rules, 26) == "          ");
    CHECK(Loading::TrackText(rules) == "............");
    CHECK_EQ(rules.dots.trackAlphaByte, 40);
    CHECK(rules.dots.font == "Matura84_shadow.fnt" && rules.dots.unitsPerFontPx == 0.5);

    // vanishEffect: the suck effect 32 u from the portal towards the character,
    // turned getAngle(direction) - 90 = atan2(-1, 0) + 360 - 90 = 180 degrees, and the
    // sparkles where the character last stood.
    const glm::dvec2 suck = Loading::SuckAt(rules, kView720) * kPxPerUnit;
    CHECK_MSG(Near(suck.x, 832.0 - 90.0, 1e-6) && Near(suck.y, 360.0, 1e-6), "suck effect at " + Num(suck.x));
    CHECK_MSG(Near(Loading::SuckAngleDeg(rules, kView720), 180.0, 1e-9), "turned " + Num(Loading::SuckAngleDeg(rules, kView720)));
    CHECK(Loading::SparklesAt(rules, kView720) == Loading::CharacterAt(rules, kView720, 81));
    CHECK(rules.portalEntity == "portal.ent" && rules.haloSprite == "entities/black_halo.bmp" && rules.haloScale == 4.0);
    CHECK(rules.vanish.suckEntity == "suck_effect.ent" && rules.vanish.sparklesEntity == "sparkles.ent");

    // The logo at (0.5, 0.2), 128 x 64 u, centred.
    const Hud::Rect logo = UiLayer::RectAt(rules.logo, UiLayer::Anchor(rules.logo, kView720));
    CHECK_MSG(TopLeftPx(logo, 460.0, 54.0) && SizePx(logo, 360.0, 180.0), "logo " + Px(logo.min));
    CHECK(rules.background.sprite == "entities/world_select_bg.png" && rules.background.sizeUnits == glm::dvec2(512.0, 256.0));
}

void WhatTheFrontDoorRefuses() {
    std::ifstream in(kUi, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Each needle is looked for after its block's own key: "fade_ms": 700 is the
    // level's black too.
    const auto refused = [&text](const char* name, const std::string& needle, const std::string& with,
                                 const std::string& named, bool menu) {
        const std::size_t block = text.find(menu ? "\"menu_state\"" : "\"loading\"");
        const std::size_t found = block == std::string::npos ? std::string::npos : text.find(needle, block);
        CHECK_MSG(found != std::string::npos, std::string(name) + ": ui.json has " + needle);
        if (found == std::string::npos) return;
        std::string bad = text;
        bad.replace(found, needle.size(), with);
        std::string why;
        bool loaded = false;
        if (menu) {
            MainMenu::Rules out;
            loaded = MainMenu::LoadRules(Write(name, bad), out, why);
        } else {
            Loading::Rules out;
            loaded = Loading::LoadRules(Write(name, bad), out, why);
        }
        CHECK_MSG(!loaded && why.find(named) != std::string::npos, std::string(name) + " is refused by name: " + why);
    };
    refused("menu_fade.json", "\"fade_ms\": 700", "\"fade_ms\": 0", "fade_ms", true);
    refused("menu_tint.json", "\"tint_byte\": 204", "\"tint_byte\": 304", "tint_byte", true);
    refused("menu_blink.json", "\"alpha_a\": 0.25", "\"alpha_a\": 1.25", "alpha_a", true);
    refused("menu_music.json", "\"at_units_x\": 32", "\"at_units_x\": -32", "at_units_x", true);
    refused("menu_bob.json", "\"radians_per_ms\": 0.002", "\"radians_per_ms\": 0", "radians_per_ms", true);
    refused("loading_list.json", "\"resources\": 162", "\"resources\": 0", "resources", false);
    refused("loading_frames.json", "\"last_frame\": 11", "\"last_frame\": 7", "last_frame", false);
    refused("loading_track.json", "\"track_alpha_byte\": 40", "\"track_alpha_byte\": 400", "track_alpha_byte", false);
    MainMenu::Rules none;
    std::string error;
    CHECK(!MainMenu::LoadRules(Write("menu_empty.json", "{}"), none, error) && !error.empty());
}

void runTests() {
    TheFileSaysWhatWasDecoded();
    EveryStateOpensUnderABlack();
    TapStartBreathesAndBlinks();
    ATileRefusesATouchThatTravelled();
    TheMainMenuSitsWhereTheStillsPutIt();
    TheTitleBobs();
    TheMenuComesInAsUIButtonsDo();
    TheSoundSwitchTakesTheMusicSwitchWithIt();
    AMenuTouchLandsOnTheButtonsItSees();
    TheLoadingScreenWalksAndHolds();
    WhatTheFrontDoorRefuses();
}

} // namespace

TEST_MAIN("test_mp_frontdoor", 150)
