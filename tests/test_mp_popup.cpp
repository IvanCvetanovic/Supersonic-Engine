// The tutorial and help popups, as numbers (sim/Popup.hpp), against the remake's ui2
// spec, section 5 and its acceptance rows A-H1..A-H13; and the two HUD rules the
// popups over 1-02 and 1-03 bring with them (Hud::PlaqueAlphaFrom,
// LevelEnd::HudEntered).
//
// Every expectation below is stated in the units it was MEASURED in where it was
// measured - screen pixels of a 1280x720 capture of the original, at 2.8125 px a
// design unit - so a number here can be held against the spec without converting
// anything back.
//
// Pure: no window, no registry, no level. ui.json is the port's own and committed,
// so this runs anywhere and never skips.

#include "TestHarness.hpp"

#include "sim/Hud.hpp"
#include "sim/LevelEnd.hpp"
#include "sim/Popup.hpp"
#include "sim/UiLayer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;

namespace {

const std::string kUi = std::string(MAGICPORTALS_PORT_DATA_DIR) + "/ui.json";

constexpr double kPxPerUnit = 2.8125;
const glm::dvec2 kView720(1280.0 / kPxPerUnit, 720.0 / kPxPerUnit); // 455.11 x 256
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

bool AtPx(const glm::dvec2& units, double x, double y, double eps) {
    return Near(units.x * kPxPerUnit, x, eps) && Near(units.y * kPxPerUnit, y, eps);
}

double SmoothEnd(double v) {
    return std::sin(std::numbers::pi / 2.0 * std::clamp(v, 0.0, 1.0));
}

std::string Write(const char* name, const std::string& text) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-mp-popup";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / name;
    std::ofstream file(path, std::ios::trunc);
    file << text;
    return path.string();
}

Popup::Rules Load() {
    Popup::Rules rules;
    std::string error;
    CHECK_MSG(Popup::LoadRules(kUi, rules, error), "ui.json's popups reads: " + error);
    return rules;
}

const Popup::Piece* Find(const std::vector<Popup::Piece>& pieces, const std::string& name) {
    for (const Popup::Piece& piece : pieces) {
        if (piece.name == name) return &piece;
    }
    return nullptr;
}
const Popup::Piece* Find(std::vector<Popup::Piece>&& pieces, const std::string& name) = delete;

// A popup `ms` into its clock as the layer steps it: one 60 Hz tick at a time.
Popup::Open Run(const Popup::Class& cls, double ms) {
    Popup::Open open = Popup::Start(cls);
    const int ticks = static_cast<int>(std::lround(ms / kTickMs));
    for (int i = 0; i < ticks; ++i) Popup::Tick(cls, open, kTickMs);
    return open;
}

const Popup::Item* Item(const Popup::Class& cls, const std::string& name) {
    for (const Popup::Item& item : cls.items) {
        if (item.name == name) return &item;
    }
    return nullptr;
}

std::size_t IndexOf(const Popup::Class& cls, const std::string& name) {
    for (std::size_t i = 0; i < cls.items.size(); ++i) {
        if (cls.items[i].name == name) return i;
    }
    return cls.items.size();
}

void TheFileSaysWhatWasDecoded() {
    const Popup::Rules rules = Load();
    CHECK_EQ(rules.dimAlphaByte, 150);
    CHECK(rules.card.sprite == "sprites/help_popup.png" && rules.card.sizeUnits == glm::dvec2(340.0, 256.0) &&
          rules.card.atScreen == glm::dvec2(0.5) && rules.card.origin == glm::dvec2(0.5));
    CHECK(rules.closeButton.sprite == "sprites/popup_close_button.png" &&
          rules.closeButton.sizeUnits == glm::dvec2(64.0) && rules.closeButton.atScreen == glm::dvec2(0.92, 0.5));
    CHECK(rules.highlight.bounceA == glm::dvec2(0.97, 1.03) && rules.highlight.bounceB == glm::dvec2(1.03, 0.97));
    CHECK_EQ(rules.highlight.bounceStrideMs, 400.0);
    CHECK(rules.highlight.blinkA == 0.95 && rules.highlight.blinkB == 1.0);
    CHECK_EQ(rules.highlight.blinkStrideMs, 150.0);
    CHECK_EQ(rules.helpBlock.boxScale, 1.2);
    CHECK_EQ(rules.helpBlock.maxMovePx, 12.0);
    CHECK(rules.helpBlock.entity == "help_block.ent");
    // The decoded 1 - 150 / 255 against the world gains the recordings measured.
    const double k = 1.0 - rules.dimAlphaByte / 255.0;
    CHECK_MSG(Near(k, 0.4118, 5e-5) && Near(k, 0.4057, 0.02) && Near(k, 0.399, 0.02), "the dim leaves " + Num(k));

    // 5.1: two levels as they load, thirteen at a block, 4-02's opening nothing.
    CHECK_EQ(static_cast<int>(rules.levelStart.size()), 2);
    CHECK(Popup::LevelStartClass(rules, "level1") != nullptr &&
          Popup::LevelStartClass(rules, "level1")->name == "LevelHelp1Popup");
    CHECK(Popup::LevelStartClass(rules, "level2") != nullptr &&
          Popup::LevelStartClass(rules, "level2")->name == "LevelHelp2Popup");
    CHECK(Popup::LevelStartClass(rules, "level0") == nullptr && Popup::LevelStartClass(rules, "level12") == nullptr);
    CHECK_EQ(static_cast<int>(rules.helpBlock.levels.size()), 13);
    const struct {
        const char* scene;
        const char* popup;
    } blocks[] = {{"level1", "LevelHelp1Popup"},           {"level2", "LevelHelp2Popup"},
                  {"level9", "LevelHelpAntiportalAgent"},  {"level10", "LevelHelpAntiportalAgent"},
                  {"level12", "LevelHelp15Popup"},         {"level18", "LevelHelp15Popup"},
                  {"level24", "LevelHelp25Popup.stone"},   {"level31", "LevelHelp25Popup.stone"},
                  {"level2a", "LevelHelpW2P3Popup"},       {"level5a", "LevelHelpW2P3Popup"},
                  {"level8b", "LevelHelp25Popup.chara"},   {"level0c", "RedKeyLocationHelpPopup"}};
    for (const auto& block : blocks) {
        bool listed = false;
        const Popup::Class* cls = Popup::HelpBlockClass(rules, block.scene, listed);
        CHECK_MSG(listed && cls != nullptr && cls->name == block.popup,
                  std::string(block.scene) + "'s block opens " + block.popup);
    }
    bool listed = false;
    CHECK_MSG(Popup::HelpBlockClass(rules, "level1c", listed) == nullptr && listed,
              "4-02's block is listed and opens nothing (spec U6, not built)");
    CHECK_MSG(Popup::HelpBlockClass(rules, "level0", listed) == nullptr && !listed, "1-1 has no block");

    // The loops, as decoded.
    const struct {
        const char* name;
        double loopMs;
        int items;
    } loops[] = {{"LevelHelp1Popup", 5500.0, 7},       {"LevelHelp2Popup", 6400.0, 6},
                 {"LevelHelp15Popup", 3500.0, 5},      {"LevelHelp25Popup.stone", 2200.0, 3},
                 {"LevelHelp25Popup.chara", 2200.0, 3}, {"LevelHelpW2P3Popup", 4000.0, 3},
                 {"LevelHelpAntiportalAgent", 6500.0, 5}, {"RedKeyLocationHelpPopup", 0.0, 1}};
    for (const auto& loop : loops) {
        const Popup::Class* cls = rules.FindClass(loop.name);
        CHECK_MSG(cls != nullptr && cls->loopMs == loop.loopMs && static_cast<int>(cls->items.size()) == loop.items,
                  std::string(loop.name) + " loops on " + Num(loop.loopMs) + " ms with its " +
                      std::to_string(loop.items) + " pieces");
    }
    const Popup::Class* redKey = rules.FindClass("RedKeyLocationHelpPopup");
    CHECK_MSG(redKey != nullptr && redKey->card.sprite == "sprites/red_key_help.png" &&
                  redKey->card.sizeUnits == glm::dvec2(256.0),
              "4-01's card is its own image, 256 u");
    // Every sprite the demonstrations draw, at its 1x texel size in units.
    const Popup::Class* one = rules.FindClass("LevelHelp1Popup");
    if (one != nullptr) {
        const Popup::Item* hand = Item(*one, "m_hand");
        const Popup::Item* arrow = Item(*one, "m_arrow");
        const Popup::Item* chara = Item(*one, "m_chara");
        CHECK(hand != nullptr && hand->sprite == "entities/tap_icon.png" && hand->sizeUnits == glm::dvec2(48.0) &&
              hand->origin == glm::dvec2(0.5, 1.3));
        CHECK(arrow != nullptr && arrow->sizeUnits == glm::dvec2(64.0, 16.0) && arrow->waypoints[0].angleDeg == 23.0);
        CHECK(chara != nullptr && chara->sheet.columns == 4 && chara->sheet.frameMs == 150.0 &&
              chara->sizeUnits == glm::dvec2(32.0, 48.0));
    }
    const Popup::Class* anti = rules.FindClass("LevelHelpAntiportalAgent");
    if (anti != nullptr) {
        const Popup::Item* ring = Item(*anti, "m_ring");
        CHECK_MSG(ring != nullptr && ring->tintRgb == glm::dvec3(160.0 / 255.0, 0.0, 0.0) &&
                      ring->waypoints[0].alphaByte == 96,
                  "the ring's decoded ARGB(96, 0xA0, 0, 0) (U13)");
    }
}

void TheFrameworkSitsWhereTheCapturesShowIt() {
    const Popup::Rules rules = Load();
    const Popup::Class* cls = rules.FindClass("LevelHelp1Popup");
    if (cls == nullptr) return;
    // A-H1 / A-H2, 1.6 s in: the library still's phase.
    const Popup::Open open = Run(*cls, 1600.0);
    const std::vector<Popup::Piece> pieces = Popup::Pieces(rules, *cls, open, kView720);
    const Popup::Piece* dim = Find(pieces, "dim");
    const Popup::Piece* card = Find(pieces, "card");
    const Popup::Piece* close = Find(pieces, "close");
    CHECK_MSG(dim != nullptr && dim->rect.min == glm::dvec2(0.0) && dim->rect.size == kView720 &&
                  dim->alphaByte == 150 && dim->sprite.empty() && dim->rgb == glm::dvec3(0.0),
              "H1 covers the view in black at 150");
    CHECK_MSG(card != nullptr && AtPx(card->rect.min, 161.875, 0.0, 1e-6) && AtPx(card->rect.Centre(), 640.0, 360.0, 1e-6) &&
                  Near(card->rect.size.x * kPxPerUnit / 340.0, 2.8125, 1e-9) && card->alphaByte == 255,
              "H2 centred (640, 360) px at 2.8125 px a texel, TL (161.875, 0): " +
                  (card != nullptr ? Px(card->rect.min) : std::string("none")));
    // Decoded 0.92 x 1280 = 1177.6; measured (1177.5, 360.0), A-H1's 1.5 px.
    CHECK_MSG(close != nullptr && AtPx(close->rect.Centre(), 1177.6, 360.0, 1e-6) &&
                  AtPx(close->rect.Centre(), 1177.5, 360.0, 1.5),
              "H3 centred (1177.6, 360) px, whatever its bounce: " +
                  (close != nullptr ? Px(close->rect.Centre()) : std::string("none")));
    if (close != nullptr) {
        const glm::dvec2 scale = close->rect.size / 64.0;
        CHECK_MSG(scale.x >= 0.97 - 1e-9 && scale.x <= 1.03 + 1e-9 && Near(scale.x + scale.y, 2.0, 1e-9),
                  "and 64 u times a bounce inside 0.97..1.03: " + Num(scale.x) + " x " + Num(scale.y));
    }
    // The statics, A-H2: stones (384, 540) and (896, 410.4), the portal (896, 309.6).
    const Popup::Piece* left = Find(pieces, "stone_left");
    const Popup::Piece* right = Find(pieces, "stone_right");
    const Popup::Piece* portal = Find(pieces, "portal_right");
    CHECK_MSG(left != nullptr && AtPx(left->rect.Centre(), 384.0, 540.0, 1e-6) && left->rect.size == glm::dvec2(64.0, 32.0),
              "the left stone at (384, 540) px, 64 x 32 u");
    CHECK_MSG(right != nullptr && AtPx(right->rect.Centre(), 896.0, 410.4, 1e-6), "the right stone at (896, 410.4) px");
    CHECK_MSG(portal != nullptr && AtPx(portal->rect.Centre(), 896.0, 309.6, 1e-6) && portal->rect.size == glm::dvec2(64.0),
              "the portal at (896, 309.6) px, 64 u");
    // The order of drawing: UILayer::draw's sprites and button, then draw()'s own.
    std::vector<std::string> order;
    for (const Popup::Piece& piece : pieces) order.push_back(piece.name);
    const auto at = [&order](const char* name) {
        return static_cast<int>(std::distance(order.begin(), std::find(order.begin(), order.end(), name)));
    };
    CHECK_MSG(at("dim") == 0 && at("card") == 1 && at("close") == 2, "the dim, the card, then the button");
    CHECK_MSG(at("m_hand") < at("stone_left") && at("stone_left") < at("stone_right") &&
                  at("stone_right") < at("portal_right"),
              "then the hand before the three statics, as LevelHelp1Popup::draw draws them");
    // The hand is whole at its fingertip, the arrow and the left portal not in yet.
    const Popup::Piece* hand = Find(pieces, "m_hand");
    CHECK_MSG(hand != nullptr && hand->alphaByte == 255 &&
                  AtPx(hand->rect.min + hand->rect.size * glm::dvec2(0.5, 1.3), 384.0, 504.0, 1e-6),
              "the hand whole, its fingertip at (384, 504) px");
    CHECK_MSG(Find(pieces, "m_arrow") == nullptr && Find(pieces, "m_portal") == nullptr,
              "the arrow and the left portal still at alpha 0, so not drawn");
    // Statics wear the button's colour: 1.6 s is past its 700 ms, so whole.
    CHECK(left != nullptr && left->alphaByte == 255 && left->rgb == glm::dvec3(1.0));

    // At 4:3 the x positions move with the width and the sizes stay.
    const glm::dvec2 view43(256.0 * 4.0 / 3.0, 256.0);
    const std::vector<Popup::Piece> narrow = Popup::Pieces(rules, *cls, open, view43);
    const Popup::Piece* card43 = Find(narrow, "card");
    const Popup::Piece* close43 = Find(narrow, "close");
    CHECK(card43 != nullptr && card43->rect.size == glm::dvec2(340.0, 256.0) && Near(card43->rect.Centre().x, view43.x / 2, 1e-9));
    CHECK(close43 != nullptr && Near(close43->rect.Centre().x, view43.x * 0.92, 1e-9));
}

void TheEntranceAndTheDismissalAreUISpriteAndUIButton() {
    const Popup::Rules rules = Load();
    const Popup::Class* cls = rules.FindClass("LevelHelp15Popup");
    if (cls == nullptr) return;
    const glm::dvec2 home = glm::dvec2(0.92, 0.5) * kView720;
    // A-H6: the dim and the card on sin T 1.0 s; A-H7: the button linear over 0.7 s,
    // sliding 32 u in along +x, eased by smoothEnd.
    for (const double t : {100.0, 350.0, 700.0, 1000.0}) {
        Popup::Open open = Popup::Start(*cls);
        open.clockMs = t;
        const std::vector<Popup::Piece> pieces = Popup::Pieces(rules, *cls, open, kView720);
        const Popup::Piece* card = Find(pieces, "card");
        const Popup::Piece* dim = Find(pieces, "dim");
        const Popup::Piece* close = Find(pieces, "close");
        const int cardByte = static_cast<int>(SmoothEnd(t / 1000.0) * 255.0);
        const int dimByte = static_cast<int>(SmoothEnd(t / 1000.0) * 150.0);
        const int buttonByte = static_cast<int>(std::min(t / 700.0, 1.0) * 255.0);
        CHECK_MSG(card != nullptr && card->alphaByte == cardByte && dim != nullptr && dim->alphaByte == dimByte,
                  "at " + Num(t) + " ms the card is " + std::to_string(cardByte) + " and the dim " +
                      std::to_string(dimByte));
        const double slide = t >= 700.0 ? 0.0 : 32.0 * (1.0 - SmoothEnd(t / 700.0));
        CHECK_MSG(close != nullptr && close->alphaByte == buttonByte &&
                      Near(close->rect.Centre().x - home.x, slide, 1e-9) && Near(close->rect.Centre().y, home.y, 1e-9),
                  "and the button " + std::to_string(buttonByte) + ", " + Num(slide) + " u out along +x");
    }
    {
        // A-P8's own instant for a button, 350 ms: alpha 0.50 and 9.37 u out.
        Popup::Open open = Popup::Start(*cls);
        open.clockMs = 350.0;
        const Popup::CloseButtonState state = Popup::CloseButtonAt(rules, open, kView720);
        CHECK_MSG(state.alphaByte == 127 && Near(state.anchor.x - home.x, 9.3726, 1e-3), Num(state.anchor.x - home.x));
    }
    // The first frame: at 0 the button starts 32 u out, at (1267.5, 360) px, drawn at nothing.
    {
        const Popup::Open open = Popup::Start(*cls);
        const Popup::CloseButtonState state = Popup::CloseButtonAt(rules, open, kView720);
        CHECK_MSG(state.alphaByte == 0 && AtPx(state.anchor, 1267.6, 360.0, 1e-6), Px(state.anchor));
        const std::vector<Popup::Piece> first = Popup::Pieces(rules, *cls, open, kView720);
        CHECK_MSG(Find(first, "dim") == nullptr && Find(first, "card") == nullptr && Find(first, "close") == nullptr,
                  "the framework sends nothing at alpha 0 (the wall's track starts whole)");
    }

    // A-H6 out, A-H11: closed at 5 s. The tracks go on the close frame; the dim and
    // the card go on 1 - smoothEnd over 1 s, the button over 0.7 s back out.
    Popup::Open open = Run(*cls, 5000.0);
    const std::vector<Popup::Piece> beforeClose = Popup::Pieces(rules, *cls, open, kView720);
    CHECK_MSG(Find(beforeClose, "m_wall") != nullptr, "the wall is up before the close");
    Popup::Close(open);
    CHECK(Popup::Closing(open));
    const double closedAt = open.closedAtMs;
    {
        const std::vector<Popup::Piece> pieces = Popup::Pieces(rules, *cls, open, kView720);
        CHECK_MSG(Find(pieces, "m_wall") == nullptr && Find(pieces, "m_stone") == nullptr,
                  "every track goes on the close frame");
        const Popup::Piece* card = Find(pieces, "card");
        const Popup::Piece* close = Find(pieces, "close");
        CHECK_MSG(card != nullptr && card->alphaByte == 255 && close != nullptr && close->alphaByte == 255,
                  "while the card and the button start their dismissal whole");
    }
    Popup::Close(open);
    CHECK_MSG(open.closedAtMs == closedAt, "a second close changes nothing");
    for (int tick = 1; tick <= 60; ++tick) {
        Popup::Tick(*cls, open, kTickMs);
        const double since = open.clockMs - closedAt;
        const std::vector<Popup::Piece> pieces = Popup::Pieces(rules, *cls, open, kView720);
        const Popup::Piece* card = Find(pieces, "card");
        const Popup::Piece* close = Find(pieces, "close");
        const int cardByte = since >= 1000.0 ? 0 : static_cast<int>((1.0 - SmoothEnd(since / 1000.0)) * 255.0);
        const int buttonByte = since >= 700.0 ? 0 : static_cast<int>((1.0 - SmoothEnd(since / 700.0)) * 255.0);
        if ((card ? card->alphaByte : 0) != cardByte || (close ? close->alphaByte : 0) != buttonByte ||
            Popup::Gone(rules, open) != (since >= 1000.0 - 1e-9)) {
            CHECK_MSG(false, "the dismissal " + Num(since) + " ms in");
            break;
        }
        if (tick == 21) {
            // 350 ms out: the button 9.37 u back out along its ray.
            CHECK_MSG(close != nullptr && Near(close->rect.Centre().x - home.x, 32.0 * SmoothEnd(since / 700.0), 1e-9),
                      Num(close != nullptr ? close->rect.Centre().x - home.x : -1.0));
        }
    }
    CHECK_MSG(Popup::Gone(rules, open), "everything dismissed 60 ticks - 1000 ms - after the close");
    Popup::Open early = Run(*cls, 5000.0);
    Popup::Close(early);
    for (int tick = 0; tick < 59; ++tick) Popup::Tick(*cls, early, kTickMs);
    CHECK_MSG(!Popup::Gone(rules, early), "and not a tick before");
}

void TheButtonBouncesAndBlinks() {
    // A-H8: sx and sy between 0.97 and 1.03 in antiphase, a stride 400 ms, smoothEnd
    // within it; the rgb 0.95..1.0 on a 150 ms stride, linear. Frozen at the close.
    const Popup::Rules rules = Load();
    const Popup::Class* cls = rules.FindClass("LevelHelp15Popup");
    if (cls == nullptr) return;
    const auto at = [&](double ms) {
        Popup::Open open = Popup::Start(*cls);
        open.clockMs = ms;
        return Popup::CloseButtonAt(rules, open, kView720);
    };
    CHECK_MSG(at(0.0).scale == glm::dvec2(0.97, 1.03) && Near(at(0.0).brightness, 0.95, 1e-12),
              "scale A and brightness A at the start");
    CHECK_MSG(glm::length(at(400.0).scale - glm::dvec2(1.03, 0.97)) < 1e-12, "B at the first stride's end");
    CHECK_MSG(glm::length(at(800.0).scale - glm::dvec2(0.97, 1.03)) < 1e-12, "A again at the second's");
    CHECK_MSG(Near(at(200.0).scale.x, 0.97 + 0.06 * SmoothEnd(0.5), 1e-12), "smoothEnd within a stride");
    CHECK_MSG(Near(at(500.0).scale.x, 0.97 + 0.06 * SmoothEnd(0.75), 1e-12), "the bias reversed on the odd stride");
    CHECK_MSG(Near(at(150.0).brightness, 1.0, 1e-12) && Near(at(300.0).brightness, 0.95, 1e-12) &&
                  Near(at(75.0).brightness, 0.975, 1e-12),
              "the blink linear, a stride 150 ms");
    double minX = 2.0;
    double maxX = 0.0;
    double minB = 2.0;
    bool antiphase = true;
    for (double ms = 0.0; ms < 2400.0; ms += kTickMs) {
        const Popup::CloseButtonState state = at(ms);
        minX = std::min(minX, state.scale.x);
        maxX = std::max(maxX, state.scale.x);
        minB = std::min(minB, state.brightness);
        antiphase = antiphase && Near(state.scale.x + state.scale.y, 2.0, 1e-12);
    }
    CHECK_MSG(minX >= 0.97 - 1e-12 && maxX <= 1.03 + 1e-12 && maxX - minX > 0.059, "0.97..1.03: " + Num(minX) + ".." + Num(maxX));
    CHECK_MSG(antiphase, "sx and sy in antiphase every tick");
    CHECK_MSG(Near(minB, 0.95, 0.001), "brightness down to 0.95");
    // Frozen from the close on.
    Popup::Open open = Popup::Start(*cls);
    open.clockMs = 1234.0;
    Popup::Close(open);
    const Popup::CloseButtonState atClose = Popup::CloseButtonAt(rules, open, kView720);
    open.clockMs = 1234.0 + 300.0;
    const Popup::CloseButtonState later = Popup::CloseButtonAt(rules, open, kView720);
    CHECK_MSG(later.scale == atClose.scale && later.brightness == atClose.brightness,
              "UIButton::update stops the bounce and the blink at the close");
}

void TheLoopsStepAsAFrameTimerDoes() {
    // FrameTimer::set: one frame at most a call, the remainder kept; a 0 ms stride is
    // a jump that still takes its call.
    Popup::Loop loop;
    const std::vector<double> strides = {100.0, 0.0, 50.0};
    loop.Tick(strides, 60.0);
    CHECK(loop.frame == 0 && loop.timeMs == 60.0);
    loop.Tick(strides, 60.0);
    CHECK(loop.frame == 1 && loop.timeMs == 20.0);
    loop.Tick(strides, 60.0);
    CHECK_MSG(loop.frame == 2 && loop.timeMs == 80.0, "the jump takes a call and no time");
    loop.Tick(strides, 60.0);
    CHECK_MSG(loop.frame == 0 && loop.timeMs == 90.0, "and past the last it wraps");
    loop.Tick(strides, 10.0);
    CHECK_MSG(loop.frame == 1 && loop.timeMs == 0.0, "a stride reached exactly steps");

    const Popup::Rules rules = Load();
    // A-H12: every class's tracks repeat exactly a decoded loop later at 60 Hz, and at
    // no lag within a tick of it. Measured the way m3_period.py measures the original:
    // the sum over a loop's ticks of every track's distance from itself a lag later.
    for (const Popup::Class& cls : rules.classes) {
        if (!(cls.loopMs > 0.0)) continue;
        const int loopTicks = static_cast<int>(std::lround(cls.loopMs / kTickMs));
        const int span = loopTicks + 2;
        std::vector<std::vector<glm::dvec3>> points; // per tick, per track: x px, y px, alpha
        Popup::Open open = Popup::Start(cls);
        for (int tick = 0; tick < 2 * span + 10; ++tick) {
            std::vector<glm::dvec3> row;
            for (std::size_t i = 0; i < cls.items.size(); ++i) {
                if (cls.items[i].kind != Popup::Item::Kind::Track) continue;
                const Popup::Point p = Popup::PointOf(cls.items[i], open.tracks[i]);
                row.emplace_back(p.at.x * 1280.0, p.at.y * 720.0, p.alpha * 255.0);
            }
            points.push_back(std::move(row));
            Popup::Tick(cls, open, kTickMs);
        }
        const auto mismatch = [&](int lag) {
            double sum = 0.0;
            for (int t = 5; t < 5 + loopTicks; ++t) {
                for (std::size_t j = 0; j < points[static_cast<std::size_t>(t)].size(); ++j) {
                    sum += glm::length(points[static_cast<std::size_t>(t + lag)][j] - points[static_cast<std::size_t>(t)][j]);
                }
            }
            return sum;
        };
        const double atLoop = mismatch(loopTicks);
        const double before = mismatch(loopTicks - 1);
        const double after = mismatch(loopTicks + 1);
        CHECK_MSG(atLoop < 1e-6 && before > 1.0 && after > 1.0,
                  cls.name + " repeats after " + std::to_string(loopTicks) + " ticks (" + Num(loopTicks * kTickMs) +
                      " ms against the decoded " + Num(cls.loopMs) + "): " + Num(atLoop) + " there, " + Num(before) +
                      " / " + Num(after) + " a tick either side");
        CHECK_MSG(std::fabs(loopTicks * kTickMs - cls.loopMs) / cls.loopMs <= 0.005, cls.name + " within 0.5%");
    }
}

void TheTracksAreTheDecodedWaypoints() {
    const Popup::Rules rules = Load();
    // A-H13, a point a phase: each at the decode's own arithmetic, on the 60 Hz tick.
    const auto pieceAt = [&](const char* cls, const char* item, double ms) -> Popup::Piece {
        const Popup::Class* c = rules.FindClass(cls);
        if (c == nullptr) return Popup::Piece{};
        const Popup::Open open = Run(*c, ms);
        const std::vector<Popup::Piece> pieces = Popup::Pieces(rules, *c, open, kView720);
        const Popup::Piece* piece = Find(pieces, item);
        return piece != nullptr ? *piece : Popup::Piece{};
    };
    // LevelHelp1's hand, 750 ms into its 1500 ms smoothEnd descent: halfway by the
    // filter, (384, 288 + 216 sin(pi/4)) at alpha sin(pi/4).
    {
        const Popup::Piece hand = pieceAt("LevelHelp1Popup", "m_hand", 750.0);
        const glm::dvec2 tip = hand.rect.min + hand.rect.size * glm::dvec2(0.5, 1.3);
        CHECK_MSG(AtPx(tip, 384.0, 288.0 + 216.0 * SmoothEnd(0.5), 1e-6) &&
                      hand.alphaByte == static_cast<int>(SmoothEnd(0.5) * 255.0),
                  "the hand at " + Px(tip) + ", alpha " + std::to_string(hand.alphaByte));
        CHECK_MSG(Near(hand.rect.size.x * kPxPerUnit / 96.0, 1.40625, 1e-9), "its hd twin at 1.40625 px a texel");
    }
    // LevelHelp15's stone, 750 ms into its 1500 ms smoothBeginning fall from (640, 144)
    // to (640, 410.4), which the recording tracked at a median 1.1 px.
    {
        const Popup::Piece stone = pieceAt("LevelHelp15Popup", "m_stone", 1250.0);
        const double eased = 1.0 - std::sin(std::numbers::pi / 2.0 * 0.5);
        CHECK_MSG(AtPx(stone.rect.Centre(), 640.0, 144.0 + 266.4 * eased, 1e-6), Px(stone.rect.Centre()));
        const Popup::Piece wall = pieceAt("LevelHelp15Popup", "m_wall", 1250.0);
        CHECK_MSG(wall.angleDeg == 90.0 && AtPx(wall.rect.Centre(), 640.0, 504.0, 1e-6) &&
                      wall.rect.size == glm::dvec2(32.0, 128.0),
                  "the wall 32 x 128 u turned 90 degrees about (640, 504) px");
        // The dust turns as it fades: 35 degrees at the end of its 1500 ms.
        const Popup::Piece dust = pieceAt("LevelHelp15Popup", "m_dust0", 2750.0);
        CHECK_MSG(Near(dust.angleDeg, 35.0 * SmoothEnd(0.5), 1e-9) && AtPx(dust.rect.Centre(), 550.4, 504.0, 1e-6),
                  Num(dust.angleDeg));
    }
    // W2P3's fireball: from the right at 0 degrees, back from the upper portal at 180.
    {
        const Popup::Piece low = pieceAt("LevelHelpW2P3Popup", "m_fireball", 500.0);
        CHECK_MSG(low.angleDeg == 0.0 && AtPx(low.rect.Centre(), 1056.0, 504.0, 1e-6), Px(low.rect.Centre()));
        const Popup::Piece high = pieceAt("LevelHelpW2P3Popup", "m_fireball", 2500.0);
        CHECK_MSG(high.angleDeg == 180.0 && Near(high.rect.Centre().y * kPxPerUnit, 216.0, 1e-6) &&
                      high.rect.Centre().x * kPxPerUnit > 576.0 && high.rect.Centre().x * kPxPerUnit < 960.0,
                  "at 180 degrees on the upper row: " + Px(high.rect.Centre()));
    }
    // The antiportal's projectile at its 200 degrees, and its ring's red.
    {
        const Popup::Piece shot = pieceAt("LevelHelpAntiportalAgent", "m_projectile", 3000.0);
        CHECK_MSG(shot.angleDeg == 200.0 && shot.alphaByte == 255 &&
                      AtPx(shot.rect.Centre(), 384.0 + (870.4 - 384.0) * 0.5, 410.4 + (288.0 - 410.4) * 0.5, 0.2),
                  Px(shot.rect.Centre()));
        const Popup::Piece ring = pieceAt("LevelHelpAntiportalAgent", "m_ring", 1000.0);
        CHECK(ring.alphaByte == 96 && ring.rgb == glm::dvec3(160.0 / 255.0, 0.0, 0.0));
        const Popup::Piece chara = pieceAt("LevelHelpAntiportalAgent", "chara", 1000.0);
        CHECK_MSG(chara.uvMin == glm::dvec2(0.0) && chara.uvMax == glm::dvec2(0.25, 1.0) &&
                      AtPx(chara.rect.Centre(), 384.0, 410.4, 1e-6),
                  "its static chara the sheet's first frame at (384, 410.4) px");
    }
    // LevelHelp25's stone: the jump from the right to the left, turned 180.
    {
        const Popup::Piece fall = pieceAt("LevelHelp25Popup.stone", "m_stone", 600.0);
        CHECK_MSG(fall.angleDeg == 0.0 && Near(fall.rect.Centre().x * kPxPerUnit, 896.0, 1e-6), Px(fall.rect.Centre()));
        const Popup::Piece up = pieceAt("LevelHelp25Popup.stone", "m_stone", 1300.0);
        CHECK_MSG(up.angleDeg == 180.0 && Near(up.rect.Centre().x * kPxPerUnit, 384.0, 1e-6), Px(up.rect.Centre()));
        const Popup::Piece chara = pieceAt("LevelHelp25Popup.chara", "m_stone", 1300.0);
        CHECK_MSG(chara.angleDeg == 0.0 && chara.uvMax == glm::dvec2(0.25, 1.0), "3-09's chara, unturned, frame 0");
    }
    // The chara's sheet steps a frame every 150 ms while the button is not dismissed.
    {
        const Popup::Class* cls = rules.FindClass("LevelHelp1Popup");
        const std::size_t chara = cls != nullptr ? IndexOf(*cls, "m_chara") : 0;
        if (cls != nullptr && chara < cls->items.size()) {
            CHECK_EQ(Run(*cls, 450.0).sheets[chara].frame, 3);
            CHECK_EQ(Run(*cls, 600.0).sheets[chara].frame, 0);
            Popup::Open open = Run(*cls, 600.0);
            Popup::Close(open);
            for (int tick = 0; tick < 30; ++tick) Popup::Tick(*cls, open, kTickMs);
            CHECK_MSG(open.sheets[chara].frame == 0, "and stops at the close");
        }
    }
    // A turned sprite whose origin is not its centre is carried round its origin.
    {
        Popup::Class cls;
        cls.name = "turned";
        cls.loopMs = 100.0;
        cls.card.sprite = "card";
        cls.card.atScreen = glm::dvec2(0.5);
        cls.card.origin = glm::dvec2(0.5);
        cls.card.sizeUnits = glm::dvec2(10.0);
        Popup::Item hand;
        hand.kind = Popup::Item::Kind::Track;
        hand.name = "hand";
        hand.sprite = "hand";
        hand.sizeUnits = glm::dvec2(48.0);
        hand.origin = glm::dvec2(0.5, 1.3);
        hand.waypoints = {Popup::Waypoint{glm::dvec2(0.0), 255, 90.0, Popup::Filter::Linear, 100.0},
                          Popup::Waypoint{glm::dvec2(0.0), 255, 90.0, Popup::Filter::Linear, 0.0}};
        cls.items.push_back(hand);
        Popup::Open open = Popup::Start(cls);
        open.clockMs = 800.0;
        const Popup::Rules loaded = Load();
        const std::vector<Popup::Piece> pieces = Popup::Pieces(loaded, cls, open, kView720);
        const Popup::Piece* piece = Find(pieces, "hand");
        // Unturned its centre is 48 * 0.8 = 38.4 u above the fingertip; turned 90
        // counter-clockwise it is 38.4 u to the fingertip's left.
        CHECK_MSG(piece != nullptr && Near(piece->rect.Centre().x, kView720.x / 2 - 38.4, 1e-9) &&
                      Near(piece->rect.Centre().y, kView720.y / 2, 1e-9),
                  "the hand turned about its fingertip");
    }
}

void AHelpBlocksTouchIsItsBoxScaled() {
    const Popup::Rules rules = Load();
    // 1-13's block at (45, 191) with the camera's corner at (0, 88.33): 38 x 1.2 u
    // square about (45, 102.67) u, which is (126.6, 288.8) px - the original's own
    // recording tapped it at (128, 272).
    const Hud::Rect rect = Popup::HelpBlockRect(rules, glm::dvec2(45.0, 191.0), glm::dvec2(38.0), glm::dvec2(0.0, 88.3333));
    CHECK_MSG(Near(rect.size.x, 45.6, 1e-9) && Near(rect.size.y, 45.6, 1e-9), "45.6 u square");
    CHECK_MSG(Near(rect.Centre().x, 45.0, 1e-9) && Near(rect.Centre().y, 102.6667, 1e-4), Px(rect.Centre()));
    CHECK(rect.Contains(glm::dvec2(128.0, 272.0) / kPxPerUnit));
}

void GameLayerComesInAfterAPopupRaisedAsTheLevelLoads() {
    // A-H9, level start: the popup holds the level's age at zero, so GameLayer's
    // restart and pause come in from the popup-gone frame as UIButtons - settled by
    // 0.70 s - and the plaque, dismissed while the popup was up, fades from whole.
    Hud::Rules hud;
    LevelEnd::Rules end;
    std::string error;
    CHECK_MSG(Hud::LoadRules(kUi, hud, error) && LevelEnd::LoadRules(kUi, end, error), error);
    const LevelEnd::Dismissed start = LevelEnd::HudEntered(end, hud.restart, hud.alphaByte, kView720, 0.0);
    const Hud::Rect home = Hud::Place(hud.restart, kView720);
    const glm::dvec2 anchor = LevelEnd::HudAnchor(hud.restart, kView720);
    const glm::dvec2 ray = glm::normalize(anchor - kView720 * 0.5);
    CHECK_MSG(start.shown && start.alpha == 0.0 && glm::length((start.rect.min - home.min) - ray * 32.0) < 1e-9,
              "restart starts 32 u out along the ray through its anchor, at nothing");
    const LevelEnd::Dismissed half = LevelEnd::HudEntered(end, hud.restart, hud.alphaByte, kView720, 350.0);
    CHECK_MSG(Near(half.alpha, 120.0 / 255.0 * 127.0 / 255.0, 1e-12) &&
                  Near(glm::length(half.rect.min - home.min), 32.0 * (1.0 - SmoothEnd(0.5)), 1e-9),
              "half its alpha and 9.37 u out at 350 ms");
    const LevelEnd::Dismissed settled = LevelEnd::HudEntered(end, hud.pause, hud.alphaByte, kView720, 700.0);
    CHECK_MSG(Near(settled.alpha, 120.0 / 255.0, 1e-12) && settled.rect.min == Hud::Place(hud.pause, kView720).min,
              "pause home and at its 120 by 700 ms");
    // In and out along the one ray through the anchor.
    const glm::dvec2 pauseHome = Hud::Place(hud.pause, kView720).min;
    const glm::dvec2 out = LevelEnd::HudDismissed(end, hud.pause, hud.alphaByte, kView720, 350.0).rect.min - pauseHome;
    const glm::dvec2 in = LevelEnd::HudEntered(end, hud.pause, hud.alphaByte, kView720, 350.0).rect.min - pauseHome;
    CHECK_MSG(std::fabs(out.x * in.y - out.y * in.x) < 1e-9 && glm::dot(out, in) > 0.0, "in and out along the one ray");

    // The plaque: dismissed where an unstopped level has it, the same curve as ever.
    bool same = true;
    for (double age = 0.0; age < 3200.0; age += kTickMs) {
        const double dismissAt = age > hud.plaque.dismissAfterMs ? hud.plaque.dismissAfterMs : -1.0;
        same = same && Hud::PlaqueAlphaFrom(hud, age, dismissAt) == Hud::PlaqueAlpha(hud, age);
    }
    CHECK_MSG(same, "PlaqueAlphaFrom is PlaqueAlpha where no stop moved the dismissal");
    // Dismissed at age 0, under a popup longer than 2 s: whole the first tick GameLayer
    // updates, gone 1000 ms on (spec 5.4: at 1.0 and gone by ~0.9 s on the emulator).
    // 1 - smoothEnd(16.7 / 1000): smoothEnd leaves zero at pi/2 a unit, so a tick
    // already takes 2.6% off.
    CHECK_MSG(Hud::PlaqueAlphaFrom(hud, kTickMs, 0.0) > 0.97, Num(Hud::PlaqueAlphaFrom(hud, kTickMs, 0.0)));
    CHECK_MSG(Hud::PlaqueAlphaFrom(hud, 500.0, 0.0) < 0.3 && Hud::PlaqueAlphaFrom(hud, 500.0, 0.0) > 0.25,
              "0.29 at half a second");
    CHECK_EQ(Hud::PlaqueAlphaFrom(hud, 1000.0, 0.0), 0.0);
    // And a pause 1.5 s long taken at 1 s: the dismissal at age 1 s, when the frame
    // clock passes 2 s under it.
    CHECK_MSG(Hud::PlaqueAlphaFrom(hud, 1000.0 + kTickMs, 1000.0) > 0.97 && Hud::PlaqueAlphaFrom(hud, 2000.0, 1000.0) == 0.0,
              "a stop moves the dismissal to where the level's age stood");
}

void WhatIsRefused() {
    Popup::Rules rules;
    std::string error;
    CHECK(!Popup::LoadRules(Write("popup_missing.json", "{}"), rules, error));
    std::ifstream file(kUi, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string good = buffer.str();
    const auto refused = [&good](const char* name, const std::string& needle, const std::string& with,
                                 const std::string& named) {
        std::string bad = good;
        const std::size_t found = bad.find(needle);
        CHECK_MSG(found != std::string::npos, std::string(name) + ": the file has " + needle);
        if (found == std::string::npos) return;
        bad.replace(found, needle.size(), with);
        Popup::Rules out;
        std::string why;
        CHECK_MSG(!Popup::LoadRules(Write(name, bad), out, why) && why.find(named) != std::string::npos,
                  std::string(name) + " is refused by name: " + why);
    };
    refused("popup_dim.json", "\"alpha_byte\": 150", "\"alpha_byte\": 256", "popups.dim.alpha_byte");
    refused("popup_loop.json", "\"loop_ms\": 5500", "\"loop_ms\": 5600", "m_arrow");
    refused("popup_class.json", "\"popup\": \"LevelHelp15Popup\"", "\"popup\": \"LevelHelp16Popup\"",
            "LevelHelp16Popup");
    refused("popup_filter.json", "\"filter\": \"smooth_beginning\"", "\"filter\": \"smoothBeginning\"", "filter");
    refused("popup_move.json", "\"max_move_px\": 12", "\"max_move_px\": 0", "max_move_px");
    refused("popup_sheet.json", "\"frame\": 0", "\"frame\": 4", "sheet.frame");
}

void runTests() {
    TheFileSaysWhatWasDecoded();
    TheFrameworkSitsWhereTheCapturesShowIt();
    TheEntranceAndTheDismissalAreUISpriteAndUIButton();
    TheButtonBouncesAndBlinks();
    TheLoopsStepAsAFrameTimerDoes();
    TheTracksAreTheDecodedWaypoints();
    AHelpBlocksTouchIsItsBoxScaled();
    GameLayerComesInAfterAPopupRaisedAsTheLevelLoads();
    WhatIsRefused();
}

} // namespace

TEST_MAIN("test_mp_popup", 100)
