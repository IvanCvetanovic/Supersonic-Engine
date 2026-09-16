// How a level ends, as numbers: the finished and lost screens and the HUD going
// (sim/LevelEnd.hpp), against the remake's ui2 spec, sections 3 and 4.
//
// Every expectation below is stated in the units it was MEASURED in where it was
// measured - screen pixels of a 1280x720 capture of the original, at 2.8125 px a
// design unit - so a number here can be held against spec 3.1, 4.2 and the
// acceptance rows A-F1..A-F12 and A-G1..A-G6 without converting anything back.
//
// Pure: no window, no registry, no level. ui.json is the port's own and
// committed, so this runs anywhere and never skips.

#include "TestHarness.hpp"

#include "sim/Hud.hpp"
#include "sim/LevelEnd.hpp"
#include "sim/UiLayer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <optional>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
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

std::string ShowPx(const glm::dvec2& p) {
    return "(" + Num(p.x * kPxPerUnit) + ", " + Num(p.y * kPxPerUnit) + ") px";
}

bool AtPx(const glm::dvec2& units, double x, double y, double eps) {
    return Near(units.x * kPxPerUnit, x, eps) && Near(units.y * kPxPerUnit, y, eps);
}

std::string Write(const char* name, const std::string& text) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-mp-levelend";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / name;
    std::ofstream file(path, std::ios::trunc);
    file << text;
    return path.string();
}

LevelEnd::Rules Load() {
    LevelEnd::Rules rules;
    std::string error;
    CHECK_MSG(LevelEnd::LoadRules(kUi, rules, error), "ui.json's level_end reads: " + error);
    return rules;
}

double SmoothEnd(double v) {
    return std::sin(std::numbers::pi / 2.0 * std::clamp(v, 0.0, 1.0));
}

const LevelEnd::Piece* Find(const std::vector<LevelEnd::Piece>& pieces, LevelEnd::Element element) {
    for (const LevelEnd::Piece& piece : pieces) {
        if (piece.element == element) return &piece;
    }
    return nullptr;
}

const LevelEnd::Piece* FindButton(const std::vector<LevelEnd::Piece>& pieces, LevelEnd::Button button) {
    for (const LevelEnd::Piece& piece : pieces) {
        if (piece.element == LevelEnd::Element::Button && piece.button == button) return &piece;
    }
    return nullptr;
}

// A piece found in a temporary list would dangle the moment the list went.
const LevelEnd::Piece* Find(std::vector<LevelEnd::Piece>&& pieces, LevelEnd::Element element) = delete;
const LevelEnd::Piece* FindButton(std::vector<LevelEnd::Piece>&& pieces, LevelEnd::Button button) = delete;

int IndexOf(const std::vector<LevelEnd::Piece>& pieces, const LevelEnd::Piece* piece) {
    return piece == nullptr ? -1 : static_cast<int>(piece - pieces.data());
}

// finished_1-1.png: 1-1 cleared with no portal, golden score 0, no crystals.
constexpr LevelEnd::Play kGold11{0, 0, 0, 0};

void TheFileSaysWhatWasDecoded() {
    const LevelEnd::Rules rules = Load();
    CHECK_EQ(rules.layer.spriteAppearMs, 1000.0);
    CHECK_EQ(rules.layer.buttonAppearMs, 700.0);
    CHECK_EQ(rules.wonDelayMs, 1400.0);
    CHECK_EQ(rules.lostDelayMs, 1400.0);
    CHECK_EQ(rules.padDecayFactor, 0.98);

    const LevelEnd::Rules::Finished& f = rules.finished;
    CHECK(f.veil.sprite == "fade_edge.png" && f.veil.tintAlphaByte == 200 && f.veil.sizeOfScreen == glm::dvec2(1.5, 1.0));
    // D2, D3, D1, D4: the sizes the port had wrong.
    CHECK(f.title.sprite == "level_finished.png" && f.title.sizeUnits == glm::dvec2(256.0, 64.0));
    CHECK(f.portalsPlaque.sprite == "portals_created_plaque.png" && f.portalsPlaque.sizeUnits == glm::dvec2(64.0));
    CHECK(f.goldenPlaque.sprite == "golden_score_plaque.png" && f.goldenPlaque.sizeUnits == glm::dvec2(64.0, 128.0) &&
          f.goldenPlaque.origin == glm::dvec2(0.5, 0.33));
    CHECK_EQ(f.goldenPlaqueBelowScore, 3);
    CHECK(f.restart.sprite == "button_restart.png" && f.next.sprite == "button_right.png" &&
          f.list.sprite == "list_button.png");
    for (const UiLayer::Placed* button : {&f.restart, &f.next, &f.list}) {
        CHECK_MSG(button->sizeUnits == glm::dvec2(64.0) && button->atScreen.x == 0.75,
                  "every finished button 64 u, in the column at 0.75");
    }
    CHECK(f.medal.sizeUnits == glm::dvec2(96.0) && f.medalGold == "medal_gold_l.png" &&
          f.medalSilver == "medal_silver_l.png" && f.medalBronze == "medal_bronze_l.png");
    CHECK(f.counter.font == "Matura128_shadow.fnt" && f.counter.unitsPerFontPx == 0.5 &&
          f.counter.offsetUnits == glm::dvec2(0.0, -7.5));
    CHECK_EQ(f.counterStrideMs, 100.0);
    CHECK(f.goldenNumber.font == "Matura84_shadow.fnt" && f.goldenNumber.offsetUnits == glm::dvec2(10.0, 1.0));
    CHECK(f.crystalSprite == "crystal.png" && f.crystalOffsetUnits == glm::dvec2(-30.0, 48.0) &&
          f.crystalSizeUnits == glm::dvec2(32.0));
    CHECK(f.crystalCount.font == "Matura84_shadow.fnt" && f.crystalCount.unitsPerFontPx == 0.35 &&
          f.crystalCount.offsetUnits == glm::dvec2(27.0, 3.0));

    const LevelEnd::Rules::Lost& l = rules.lost;
    CHECK(l.veil.sprite == "fade_edge.png" && l.veil.tintAlphaByte == 180 && l.veil.sizeOfScreen == glm::dvec2(0.9, 1.0));
    CHECK(l.title.sprite == "game_over.png" && l.title.sizeUnits == glm::dvec2(128.0)); // D5
    CHECK(l.restart.sprite == "button_restart.png" && l.restart.atScreen == glm::dvec2(0.4, 0.6) &&
          l.restart.sizeUnits == glm::dvec2(64.0));
    CHECK(l.list.sprite == "list_button.png" && l.list.atScreen == glm::dvec2(0.6, 0.6) &&
          l.list.sizeUnits == glm::dvec2(64.0));
}

// A-F1, A-F2, A-F3, A-F5: finished_1-1.png, settled.
void TheFinishedScreenSitsWhereTheCaptureShowsIt() {
    const LevelEnd::Rules rules = Load();
    const std::vector<LevelEnd::Piece> settled = LevelEnd::Finished(rules, kGold11, 0, 0, kView720, 3000.0);

    const struct {
        const char* name;
        const LevelEnd::Piece* piece;
        double decodedX, decodedY; // spec 3.1, "TL px @720p"
        double measuredX, measuredY, tolerance;
        double w, h;
    } rows[] = {
        {"F2 level finished", Find(settled, LevelEnd::Element::Title), 233.92, 110.16, 234.0, 110.25, 0.5, 720.0, 180.0},
        {"F3 portals plaque", Find(settled, LevelEnd::Element::PortalsPlaque), 511.6, 414.0, 511.5, 414.0, 0.5, 180.0,
         180.0},
        {"F5 restart", FindButton(settled, LevelEnd::Button::Restart), 870.0, 90.0, 870.0, 90.0, 0.5, 180.0, 180.0},
        {"F6 next", FindButton(settled, LevelEnd::Button::Next), 870.0, 270.0, 870.0, 270.0, 0.5, 180.0, 180.0},
        {"F7 level select", FindButton(settled, LevelEnd::Button::List), 870.0, 450.0, 870.0, 450.0, 0.5, 180.0,
         180.0},
        // The medal at 2.109, not 1.40625: 96 u, 270 px, within A-F1's pixel.
        {"F8 medal", Find(settled, LevelEnd::Element::Medal), 466.6, 261.0, 467.0, 261.25, 1.0, 270.0, 270.0},
    };
    for (const auto& row : rows) {
        CHECK_MSG(row.piece != nullptr, std::string(row.name) + " is drawn");
        if (row.piece == nullptr) continue;
        CHECK_MSG(AtPx(row.piece->rect.min, row.decodedX, row.decodedY, 0.06) &&
                      Near(row.piece->rect.size.x * kPxPerUnit, row.w, 1e-6) &&
                      Near(row.piece->rect.size.y * kPxPerUnit, row.h, 1e-6),
                  std::string(row.name) + " at the decoded TL and size: " + ShowPx(row.piece->rect.min));
        CHECK_MSG(AtPx(row.piece->rect.min, row.measuredX, row.measuredY, row.tolerance),
                  std::string(row.name) + " within A-F1's tolerance of the measured TL");
        CHECK_MSG(row.piece->alphaByte == 255, std::string(row.name) + " whole once settled (A-F5)");
    }
    const LevelEnd::Piece* medal = Find(settled, LevelEnd::Element::Medal);
    CHECK_MSG(medal != nullptr && medal->file == "medal_gold_l.png", "a gold medal for 0 portals against 0");

    // A-F2: "0" in Matura128_shadow, centred on the medal's position + (0, -7.5) u.
    const LevelEnd::Piece* counter = Find(settled, LevelEnd::Element::Counter);
    CHECK_MSG(counter != nullptr && counter->words == "0" && counter->file == "Matura128_shadow.fnt" &&
                  counter->centred && counter->unitsPerFontPx == 0.5,
              "F9 reads 0, Matura128_shadow at half a unit a font px, centred");
    if (counter != nullptr) {
        CHECK_MSG(AtPx(counter->at, 601.6, 374.9, 0.01), "F9 centred at the decoded " + ShowPx(counter->at));
        // The decoded centre sits 1.006 px below the measured one: the text offset
        // spec U11 records on every text of the original (0.5 px left, 0.5..1.0 px
        // up) and that the port does not copy. A-F2 is held on the capture.
        CHECK_MSG(AtPx(counter->at, 601.1, 373.9, 1.01), "within A-F2's pixel of the measured (601.1, 373.9)");
    }

    // A-F3: gold, so no golden plaque and no golden number; no crystals on 1-1.
    CHECK(Find(settled, LevelEnd::Element::GoldenPlaque) == nullptr);
    CHECK(Find(settled, LevelEnd::Element::GoldenNumber) == nullptr);
    CHECK(Find(settled, LevelEnd::Element::Crystal) == nullptr);
    CHECK(Find(settled, LevelEnd::Element::CrystalCount) == nullptr);

    // F1: fade_edge.png from the top-left, one and a half screens wide, at 200.
    const LevelEnd::Piece* veil = Find(settled, LevelEnd::Element::Veil);
    CHECK_MSG(veil != nullptr && IndexOf(settled, veil) == 0 && veil->rect.min == glm::dvec2(0.0) &&
                  Near(veil->rect.size.x * kPxPerUnit, 1920.0, 1e-6) &&
                  Near(veil->rect.size.y * kPxPerUnit, 720.0, 1e-6) && veil->alphaByte == 200,
              "F1 first, (0, 0) to 1920 x 720 px, alpha 200");

    // The order spec 3.4 gives: the veil, the sprites, the buttons, then the
    // medal over the plaque and the counter over the medal.
    const int title = IndexOf(settled, Find(settled, LevelEnd::Element::Title));
    const int plaque = IndexOf(settled, Find(settled, LevelEnd::Element::PortalsPlaque));
    const int restart = IndexOf(settled, FindButton(settled, LevelEnd::Button::Restart));
    const int list = IndexOf(settled, FindButton(settled, LevelEnd::Button::List));
    CHECK_MSG(0 < title && title < plaque && plaque < restart && restart < list &&
                  list < IndexOf(settled, medal) && IndexOf(settled, medal) < IndexOf(settled, counter),
              "veil, title, plaque, buttons, medal, counter");

    // Fractions of the screen: at 4:3 the column moves with the width, sizes stay.
    const glm::dvec2 view43(256.0 * 4.0 / 3.0, 256.0);
    const std::vector<LevelEnd::Piece> narrow = LevelEnd::Finished(rules, kGold11, 0, 0, view43, 3000.0);
    const LevelEnd::Piece* next43 = FindButton(narrow, LevelEnd::Button::Next);
    CHECK_MSG(next43 != nullptr && Near(next43->rect.Centre().x, 0.75 * view43.x, 1e-9) &&
                  next43->rect.size == glm::dvec2(64.0),
              "at 4:3 next is still at 0.75 of the width, 64 units");
}

// D6, and the pieces nothing captured shows: F4, F10, F11, F12 (decode only).
void TheGoldenPlaqueAndTheCrystalsAreForThePlayThatEarnsThem() {
    const LevelEnd::Rules rules = Load();
    // Not gold: 5 portals against a golden score of 2, and 2 of 3 crystals.
    constexpr LevelEnd::Play play{5, 2, 2, 3};
    const std::vector<LevelEnd::Piece> settled = LevelEnd::Finished(rules, play, 5, 2, kView720, 3000.0);
    const LevelEnd::Piece* golden = Find(settled, LevelEnd::Element::GoldenPlaque);
    CHECK_MSG(golden != nullptr && AtPx(golden->rect.min, 204.4, 241.2, 0.06) &&
                  golden->rect.size == glm::dvec2(64.0, 128.0),
              "F4 at the decoded (204.4, 241.2) for a final score under 3");
    const LevelEnd::Piece* number = Find(settled, LevelEnd::Element::GoldenNumber);
    CHECK_MSG(number != nullptr && number->words == "2" && AtPx(number->at, 322.5, 362.8, 0.06) && number->centred,
              "F10 reads the golden score, centred at (322.5, 362.8)");
    const LevelEnd::Piece* crystal = Find(settled, LevelEnd::Element::Crystal);
    CHECK_MSG(crystal != nullptr && AtPx(crystal->rect.min, 517.2, 531.0, 0.06) &&
                  crystal->rect.size == glm::dvec2(32.0) && crystal->file == "crystal.png",
              "F11 from its top-left at (517.2, 531), 32 u");
    const LevelEnd::Piece* count = Find(settled, LevelEnd::Element::CrystalCount);
    CHECK_MSG(count != nullptr && count->words == "2/3" && !count->centred && count->unitsPerFontPx == 0.35 &&
                  crystal != nullptr && count->at == crystal->rect.min + glm::dvec2(27.0, 3.0),
              "F12 'n/max', NOT centred, from the crystal's corner + (27, 3) u");
    CHECK_MSG(IndexOf(settled, golden) < IndexOf(settled, FindButton(settled, LevelEnd::Button::Restart)) &&
                  IndexOf(settled, number) > IndexOf(settled, Find(settled, LevelEnd::Element::Counter)) &&
                  IndexOf(settled, count) > IndexOf(settled, crystal),
              "F4 among the sprites; F10 after the counter; F12 after its crystal");

    // The plaque's gate is the FINAL score: silver and bronze have it, gold not.
    for (const auto& [portals, golden2, shown] : {std::tuple{2, 2, false}, std::tuple{4, 2, true}, std::tuple{5, 2, true}}) {
        const LevelEnd::Play p{portals, golden2, 0, 0};
        const std::vector<LevelEnd::Piece> pieces = LevelEnd::Finished(rules, p, 0, 0, kView720, 3000.0);
        const bool has = Find(pieces, LevelEnd::Element::GoldenPlaque) != nullptr;
        CHECK_MSG(has == shown, "the golden plaque for " + std::to_string(portals) + " portals against " +
                                    std::to_string(golden2) + (shown ? " is drawn" : " is not"));
    }
}

// A-F12: the counter at 60 Hz, and the medal it earns as it goes.
void TheCounterStepsAndTheMedalFollowsIt() {
    const LevelEnd::Rules rules = Load();
    // 6 portals against a golden score of 1: gold while the count is 0 or 1,
    // silver at 2 and 3, bronze from 4.
    constexpr LevelEnd::Play play{6, 1, 0, 0};
    LevelEnd::Counter counter{0, play.portalsUsed, 0.0};
    int tick = 0;
    std::string steps;
    for (int k = 0; k <= 8; ++k) {
        // Mid-stride: 50 ms past t0 + k * 100 ms is tick 6k + 3.
        for (; tick < 6 * k + 3; ++tick) counter.Tick(kTickMs, rules.finished.counterStrideMs);
        const int want = std::min(k, play.portalsUsed);
        CHECK_MSG(counter.current == want, "at t0 + " + std::to_string(k * 100 + 50) + " ms the count is " +
                                               std::to_string(counter.current) + ", not " + std::to_string(want));
        const std::vector<LevelEnd::Piece> pieces =
            LevelEnd::Finished(rules, play, counter.current, 0, kView720, tick * kTickMs);
        const LevelEnd::Piece* medal = Find(pieces, LevelEnd::Element::Medal);
        const std::string wantMedal = want <= 1 ? "medal_gold_l.png" : want <= 3 ? "medal_silver_l.png" : "medal_bronze_l.png";
        CHECK_MSG(medal != nullptr && medal->file == wantMedal, "and the medal is " + wantMedal);
        steps += std::to_string(counter.current);
    }
    std::printf("  counter at mid-stride: %s\n", steps.c_str());
    // A step lands on the tick the timer reaches 100 ms: the sixth.
    LevelEnd::Counter edge{0, 3, 0.0};
    for (int t = 0; t < 5; ++t) edge.Tick(kTickMs, 100.0);
    CHECK_EQ(edge.current, 0);
    edge.Tick(kTickMs, 100.0);
    CHECK_EQ(edge.current, 1);
    // At its end it holds, and a counter whose end is below it counts down.
    LevelEnd::Counter down{3, 1, 0.0};
    for (int t = 0; t < 60; ++t) down.Tick(kTickMs, 100.0);
    CHECK_EQ(down.current, 1);

    // computeScore, all four arms.
    CHECK_EQ(LevelEnd::ComputeScore(0, 0, 0, 0), 3);
    CHECK_EQ(LevelEnd::ComputeScore(2, 2, 1, 2), 2);
    CHECK_EQ(LevelEnd::ComputeScore(4, 2, 2, 2), 2);
    CHECK_EQ(LevelEnd::ComputeScore(5, 2, 2, 2), 1);
    CHECK_EQ(LevelEnd::ComputeScore(0, 5, 0, 3), 1);
}

// A-F9, A-F10; A-G3.
void TheScreensComeInAsUISpriteAndUIButtonDo() {
    const LevelEnd::Rules rules = Load();
    // A-F9 at t0 + 0.1, 0.35, 0.69, 1.0 s.
    for (const double t : {0.1, 0.35, 0.69, 1.0}) {
        const std::vector<LevelEnd::Piece> at = LevelEnd::Finished(rules, kGold11, 0, 0, kView720, t * 1000.0);
        const double sprite = SmoothEnd(t / 1.0);
        const double button = std::min(t / 0.7, 1.0);
        const LevelEnd::Piece* veil = Find(at, LevelEnd::Element::Veil);
        const LevelEnd::Piece* title = Find(at, LevelEnd::Element::Title);
        const LevelEnd::Piece* plaque = Find(at, LevelEnd::Element::PortalsPlaque);
        const LevelEnd::Piece* next = FindButton(at, LevelEnd::Button::Next);
        const LevelEnd::Piece* medal = Find(at, LevelEnd::Element::Medal);
        CHECK_MSG(veil != nullptr && Near(veil->alphaByte / 200.0, sprite, 0.03),
                  "the veil's fade a(" + Num(t) + ") = " + Num(veil ? veil->alphaByte / 200.0 : -1.0));
        CHECK_MSG(title != nullptr && plaque != nullptr && Near(title->alphaByte / 255.0, sprite, 0.03) &&
                      Near(plaque->alphaByte / 255.0, sprite, 0.03),
                  "F2 and F3 at sin " + Num(sprite));
        CHECK_MSG(next != nullptr && medal != nullptr && Near(next->alphaByte / 255.0, button, 0.03) &&
                      medal->alphaByte == next->alphaByte,
                  "the buttons linear at " + Num(button) + ", the medal at theirs");
    }
    // The spec's own numbers for those four instants.
    const double wantSin[] = {0.156, 0.522, 0.884, 1.0};
    const double wantLin[] = {0.143, 0.50, 0.986, 1.0};
    const double ts[] = {0.1, 0.35, 0.69, 1.0};
    for (int i = 0; i < 4; ++i) {
        CHECK(Near(SmoothEnd(ts[i]), wantSin[i], 0.001) && Near(std::min(ts[i] / 0.7, 1.0), wantLin[i], 0.001));
    }

    // A-F10: the slide along each ray, measured 83.5 / 39 / 15.5 / 5 / 0 px.
    const glm::dvec2 home = glm::dvec2(0.75, 0.5) * kView720;
    const double times[] = {0.022, 0.257, 0.429, 0.527, 0.656};
    const double measured[] = {83.5, 39.0, 15.5, 5.0, 0.0};
    for (int i = 0; i < 5; ++i) {
        const std::vector<LevelEnd::Piece> pieces = LevelEnd::Finished(rules, kGold11, 0, 0, kView720, times[i] * 1000.0);
        const LevelEnd::Piece* next = FindButton(pieces, LevelEnd::Button::Next);
        const double px = next == nullptr ? -1.0 : glm::length(next->rect.Centre() - home) * kPxPerUnit;
        CHECK_MSG(Near(px, measured[i], 3.0), "next " + Num(px) + " px out at t0 + " + Num(times[i]) + " s, measured " +
                                                  Num(measured[i]));
    }
    // The start offsets spec 3.4 lists: F5 (+27.89, -15.69), F6 (+32, 0), F7 (+27.89, +15.69).
    const std::vector<LevelEnd::Piece> first = LevelEnd::Finished(rules, kGold11, 0, 0, kView720, 0.0);
    const struct {
        LevelEnd::Button button;
        glm::dvec2 at;
        glm::dvec2 offset;
    } starts[] = {{LevelEnd::Button::Restart, {0.75, 0.25}, {27.89, -15.69}},
                  {LevelEnd::Button::Next, {0.75, 0.5}, {32.0, 0.0}},
                  {LevelEnd::Button::List, {0.75, 0.75}, {27.89, 15.69}}};
    for (const auto& start : starts) {
        const LevelEnd::Piece* piece = FindButton(first, start.button);
        CHECK_MSG(piece != nullptr && glm::length(piece->rect.Centre() - start.at * kView720 - start.offset) < 0.01 &&
                      piece->alphaByte == 0,
                  "a finished button starts 32 u out along its ray, unseen");
    }
    // Home once the 700 ms are over, and never moving after.
    const std::vector<LevelEnd::Piece> late = LevelEnd::Finished(rules, kGold11, 0, 0, kView720, 701.0);
    const LevelEnd::Piece* settled = FindButton(late, LevelEnd::Button::Next);
    CHECK_MSG(settled != nullptr && settled->rect.Centre() == home, "home past 700 ms");

    // A-G3, the lost screen: L1 and L2 sin, L3 and L4 linear and 32 u out; its
    // first frame 48 ms in 78 px out (model 80.3).
    const std::vector<LevelEnd::Piece> lost0 = LevelEnd::Lost(rules, kView720, 0.0);
    const LevelEnd::Piece* restart0 = FindButton(lost0, LevelEnd::Button::Restart);
    const LevelEnd::Piece* list0 = FindButton(lost0, LevelEnd::Button::List);
    CHECK_MSG(restart0 != nullptr && list0 != nullptr &&
                  glm::length(restart0->rect.Centre() - glm::dvec2(0.4, 0.6) * kView720 - glm::dvec2(-27.89, 15.69)) <
                      0.01 &&
                  glm::length(list0->rect.Centre() - glm::dvec2(0.6, 0.6) * kView720 - glm::dvec2(27.89, 15.69)) < 0.01,
              "L3 from (-27.89, +15.69) u and L4 from (+27.89, +15.69) u");
    const std::vector<LevelEnd::Piece> lost48 = LevelEnd::Lost(rules, kView720, 48.0);
    const LevelEnd::Piece* restart48 = FindButton(lost48, LevelEnd::Button::Restart);
    const double out48 =
        restart48 == nullptr ? -1.0 : glm::length(restart48->rect.Centre() - glm::dvec2(0.4, 0.6) * kView720) * kPxPerUnit;
    CHECK_MSG(Near(out48, 78.0, 3.0) && Near(out48, 80.3, 0.1), "48 ms in, L3 is " + Num(out48) + " px out");
    for (const double t : {0.1, 0.35, 0.69, 1.0}) {
        const std::vector<LevelEnd::Piece> at = LevelEnd::Lost(rules, kView720, t * 1000.0);
        const LevelEnd::Piece* veil = Find(at, LevelEnd::Element::Veil);
        const LevelEnd::Piece* title = Find(at, LevelEnd::Element::Title);
        const LevelEnd::Piece* list = FindButton(at, LevelEnd::Button::List);
        CHECK_MSG(veil != nullptr && title != nullptr && list != nullptr &&
                      Near(veil->alphaByte / 180.0, SmoothEnd(t), 0.03) && Near(title->alphaByte / 255.0, SmoothEnd(t), 0.03) &&
                      Near(list->alphaByte / 255.0, std::min(t / 0.7, 1.0), 0.03),
                  "the lost screen's sin and linear at " + Num(t) + " s");
    }
}

// A-G1: 2-32_h6.3.png, settled.
void TheLostScreenSitsWhereTheCaptureShowsIt() {
    const LevelEnd::Rules rules = Load();
    const std::vector<LevelEnd::Piece> settled = LevelEnd::Lost(rules, kView720, 3000.0);
    CHECK_EQ(settled.size(), std::size_t{4});
    const LevelEnd::Piece* veil = Find(settled, LevelEnd::Element::Veil);
    CHECK_MSG(veil != nullptr && IndexOf(settled, veil) == 0 && veil->rect.min == glm::dvec2(0.0) &&
                  Near(veil->rect.size.x * kPxPerUnit, 1152.0, 1e-6) &&
                  Near(veil->rect.size.y * kPxPerUnit, 720.0, 1e-6) && veil->alphaByte == 180,
              "L1 first, (0, 0) to 1152 x 720 px, alpha 180");
    const LevelEnd::Piece* title = Find(settled, LevelEnd::Element::Title);
    CHECK_MSG(title != nullptr && AtPx(title->rect.Centre(), 640.0, 252.0, 0.5) &&
                  Near(title->rect.size.x * kPxPerUnit, 360.0, 1e-6) && title->alphaByte == 255,
              "L2 centred at (640, 252), 360 px");
    const LevelEnd::Piece* restart = FindButton(settled, LevelEnd::Button::Restart);
    const LevelEnd::Piece* list = FindButton(settled, LevelEnd::Button::List);
    CHECK_MSG(restart != nullptr && AtPx(restart->rect.Centre(), 512.0, 432.0, 0.5) &&
                  Near(restart->rect.size.x * kPxPerUnit, 180.0, 1e-6),
              "L3 centred at (512, 432), 180 px");
    CHECK_MSG(list != nullptr && AtPx(list->rect.Centre(), 768.0, 432.0, 0.5), "L4 centred at (768, 432)");
    CHECK_MSG(IndexOf(settled, title) < IndexOf(settled, restart) && IndexOf(settled, restart) < IndexOf(settled, list),
              "the title, then restart, then the list");
    CHECK(FindButton(settled, LevelEnd::Button::Next) == nullptr);
}

void ATapLandsOnTheButtonItSees() {
    const LevelEnd::Rules rules = Load();
    const std::vector<LevelEnd::Piece> settled = LevelEnd::Finished(rules, kGold11, 0, 0, kView720, 3000.0);
    CHECK(LevelEnd::ButtonAt(settled, glm::dvec2(0.75, 0.25) * kView720) == LevelEnd::Button::Restart);
    CHECK(LevelEnd::ButtonAt(settled, glm::dvec2(0.75, 0.5) * kView720) == LevelEnd::Button::Next);
    CHECK(LevelEnd::ButtonAt(settled, glm::dvec2(0.75, 0.75) * kView720) == LevelEnd::Button::List);
    CHECK(!LevelEnd::ButtonAt(settled, glm::dvec2(0.47, 0.55) * kView720).has_value());
    // Mid-slide, where the button is and not where it will be.
    const std::vector<LevelEnd::Piece> sliding = LevelEnd::Lost(rules, kView720, 100.0);
    const LevelEnd::Piece* list = FindButton(sliding, LevelEnd::Button::List);
    CHECK(list != nullptr && LevelEnd::ButtonAt(sliding, list->rect.Centre()) == LevelEnd::Button::List);
}

// The veil's texture, as a renderer that repeats can draw it clamped.
void TheVeilIsAClampedTexture() {
    const Hud::Rect rect{glm::dvec2(0.0), glm::dvec2(1920.0, 720.0) / kPxPerUnit};
    const std::vector<LevelEnd::Strip> strips = LevelEnd::ClampedStrips(rect, glm::ivec2(64, 16));
    CHECK_EQ(strips.size(), std::size_t{3});
    if (strips.size() != 3) return;
    // 30 px a texel: the end strips are half of one each.
    CHECK_MSG(Near(strips[0].rect.size.x * kPxPerUnit, 15.0, 1e-9) && strips[0].uvMin.x == 0.5 / 64.0 &&
                  strips[0].uvMax.x == 0.5 / 64.0,
              "the left strip shows only the first texel's centre");
    CHECK_MSG(Near(strips[2].rect.min.x * kPxPerUnit, 1905.0, 1e-9) && strips[2].uvMin.x == 63.5 / 64.0,
              "the right strip only the last one's");
    CHECK_MSG(Near(strips[1].rect.min.x * kPxPerUnit, 15.0, 1e-9) && Near(strips[1].rect.Max().x * kPxPerUnit, 1905.0, 1e-9) &&
                  strips[1].uvMin.x == 0.5 / 64.0 && strips[1].uvMax.x == 63.5 / 64.0,
              "and between them the texture from centre to centre");
    // Across the middle strip, u at screen x is x / width: the texture where a
    // clamped sampler reads it, 64 x / 1920 texels in.
    for (const double x : {15.0, 320.0, 640.0, 1180.0, 1279.0, 1905.0}) {
        const LevelEnd::Strip& middle = strips[1];
        const double f = (x / kPxPerUnit - middle.rect.min.x) / middle.rect.size.x;
        const double u = middle.uvMin.x + f * (middle.uvMax.x - middle.uvMin.x);
        CHECK_MSG(Near(u, x / 1920.0, 1e-12), "u at x " + Num(x) + " is x / 1920");
    }
    for (const LevelEnd::Strip& strip : strips) {
        CHECK_MSG(strip.uvMin.y == 0.5 / 16.0 && strip.uvMax.y == 15.5 / 16.0 && strip.rect.size.y == rect.size.y,
                  "every strip the full height, between the rows' centres");
    }
}

// A-F6, A-F7, A-G4, A-G5: the HUD as a level ends.
void TheHudGoesAsTheLevelEnds() {
    const LevelEnd::Rules rules = Load();
    // uint(a * 0.98) a tick: 120, 180 and 210 are gone after 81, 99 and 106.
    for (const auto& [from, gone] : {std::pair{120, 81}, std::pair{180, 99}, std::pair{210, 106}}) {
        CHECK_MSG(LevelEnd::PadDecayByte(rules, from, gone - 1) > 0 && LevelEnd::PadDecayByte(rules, from, gone) == 0,
                  "a pad at " + std::to_string(from) + " is gone after " + std::to_string(gone) + " ticks");
    }
    CHECK_EQ(LevelEnd::PadDecayByte(rules, 120, 1), 117);
    CHECK_EQ(LevelEnd::PadDecayByte(rules, 180, 1), 176);

    Hud::Placement restart;
    restart.sprite = "restart_level_button.png";
    restart.anchor = Hud::Anchor::TopRight;
    restart.insetUnits = glm::dvec2(32.0, 0.0);
    restart.sizeUnits = glm::dvec2(32.0);
    Hud::Placement pause = restart;
    pause.insetUnits = glm::dvec2(0.0);
    CHECK_MSG(LevelEnd::HudAnchor(restart, kView720) == glm::dvec2(kView720.x - 32.0, 0.0) &&
                  LevelEnd::HudAnchor(pause, kView720) == glm::dvec2(kView720.x, 0.0),
              "restart and pause are anchored at their top-right corners");

    // A-G4: the dismiss from the death tick, a tick at a time.
    int lastShown = -1;
    for (int tick = 0; tick <= 45; ++tick) {
        const double ms = tick * kTickMs;
        const LevelEnd::Dismissed d = LevelEnd::HudDismissed(rules, pause, 120, kView720, ms);
        const double want = 120.0 / 255.0 * (ms >= 700.0 ? 0.0 : 1.0 - SmoothEnd(ms / 700.0));
        CHECK_MSG(Near(d.alpha, want, 0.03) || (!d.shown && want < 0.03),
                  "the pause control at " + Num(d.alpha) + " on tick " + std::to_string(tick));
        if (d.shown) lastShown = tick;
        if (tick == 0) {
            CHECK_MSG(d.shown && Near(d.alpha, 120.0 / 255.0, 1e-9) && d.rect.min == Hud::Place(pause, kView720).min,
                      "whole and home on the death tick");
        }
    }
    const LevelEnd::Dismissed last = LevelEnd::HudDismissed(rules, pause, 120, kView720, lastShown * kTickMs);
    const double travel = glm::length(last.rect.min - Hud::Place(pause, kView720).min);
    CHECK_MSG(lastShown >= 0 && lastShown * kTickMs < 700.0 && travel >= 28.0,
              "gone at 700 ms, having moved " + Num(travel) + " u out by its last visible tick " +
                  std::to_string(lastShown));
    const LevelEnd::Dismissed out = LevelEnd::HudDismissed(rules, pause, 120, kView720, 350.0);
    const glm::dvec2 moved = out.rect.min - Hud::Place(pause, kView720).min;
    CHECK_MSG(moved.x > 0.0 && moved.y < 0.0, "outward: right and up, away from the screen's centre");
}

void WhatIsRefused() {
    LevelEnd::Rules rules;
    std::string error;
    CHECK(!LevelEnd::LoadRules(Write("end_missing.json", "{}"), rules, error));
    CHECK_MSG(!error.empty(), "an empty file says why");

    std::ifstream in(kUi, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto refused = [&text](const char* name, const std::string& needle, const std::string& with,
                                 const std::string& named) {
        const std::size_t found = text.find(needle);
        CHECK_MSG(found != std::string::npos, std::string(name) + ": ui.json has " + needle);
        if (found == std::string::npos) return;
        std::string bad = text;
        bad.replace(found, needle.size(), with);
        LevelEnd::Rules out;
        std::string why;
        CHECK_MSG(!LevelEnd::LoadRules(Write(name, bad), out, why) && why.find(named) != std::string::npos,
                  std::string(name) + " is refused by name: " + why);
    };
    refused("end_tint.json", "\"tint_alpha_byte\": 200", "\"tint_alpha_byte\": 256", "tint_alpha_byte");
    refused("end_decay.json", "\"pad_decay_factor\": 0.98", "\"pad_decay_factor\": 1.0", "pad_decay_factor");
    refused("end_delay.json", "\"won_delay_ms\": 1400", "\"won_delay_ms\": 0", "won_delay_ms");
    refused("end_gate.json", "\"shown_below_score\": 3", "\"shown_below_score\": 7", "shown_below_score");
    refused("end_veil.json", "\"size_of_screen\": [0.9, 1]", "\"size_of_screen\": [0, 1]", "size_of_screen");
    refused("end_title.json", "\"at_screen\": [0.464, 0.278]", "\"at_screen\": [1.464, 0.278]", "finished.title");
}

void runTests() {
    TheFileSaysWhatWasDecoded();
    TheFinishedScreenSitsWhereTheCaptureShowsIt();
    TheGoldenPlaqueAndTheCrystalsAreForThePlayThatEarnsThem();
    TheCounterStepsAndTheMedalFollowsIt();
    TheScreensComeInAsUISpriteAndUIButtonDo();
    TheLostScreenSitsWhereTheCaptureShowsIt();
    ATapLandsOnTheButtonItSees();
    TheVeilIsAClampedTexture();
    TheHudGoesAsTheLevelEnds();
    WhatIsRefused();
}

} // namespace

TEST_MAIN("test_mp_levelend", 100)
