// Credits and the achievements dashboard, as numbers: the main menu's info and
// Achievements screens, and what the port's save has opened.
//
// sim/Credits.hpp, sim/Dashboard.hpp, sim/Achievements.hpp and sim/Locking.hpp turn
// the port's ui.json and the save into rectangles, alphas, words and a scroll, and
// this pins them against the remake's ui3 spec (sections 3 and 4, acceptance 7.3
// and 7.4). Every position is stated in the pixels of a 1280x720 capture of the
// original, at 2.8125 px a design unit, so a number here holds against static.md
// and motion.md as they are.
//
// Pure: no window and no registry. It reads only the port's own committed data,
// and never the original's achievements - their titles, descriptions, icons and
// points are Asantee's and not in this repository - so every list here is its own,
// made up, and the suite never skips.

#include "TestHarness.hpp"

#include "sim/Achievements.hpp"
#include "sim/Chapters.hpp"
#include "sim/Credits.hpp"
#include "sim/Dashboard.hpp"
#include "sim/Hud.hpp"
#include "sim/Locking.hpp"
#include "sim/MenuState.hpp"
#include "sim/Scores.hpp"
#include "sim/UiLayer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;

namespace {

const std::string kUi = std::string(MAGICPORTALS_PORT_DATA_DIR) + "/ui.json";

constexpr double kPxPerUnit = 2.8125;
const glm::dvec2 kView720(1280.0 / kPxPerUnit, 720.0 / kPxPerUnit); // 455.11 x 256
const glm::dvec2 kView43(256.0 * 4.0 / 3.0, 256.0);                 // 341.33 x 256
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

bool AtPx(const glm::dvec2& units, double x, double y, double eps = 0.06) {
    return Near(units.x * kPxPerUnit, x, eps) && Near(units.y * kPxPerUnit, y, eps);
}

bool SizePx(const Hud::Rect& rect, double w, double h, double eps = 0.06) {
    return Near(rect.size.x * kPxPerUnit, w, eps) && Near(rect.size.y * kPxPerUnit, h, eps);
}

std::filesystem::path Scratch() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-mp-info";
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

Credits::Rules LoadCredits() {
    Credits::Rules rules;
    std::string error;
    CHECK_MSG(Credits::LoadRules(kUi, rules, error), "ui.json's credits read: " + error);
    return rules;
}

Dashboard::Rules LoadDashboard() {
    Dashboard::Rules rules;
    std::string error;
    CHECK_MSG(Dashboard::LoadRules(kUi, rules, error), "ui.json's dashboard reads: " + error);
    return rules;
}

Locking::Rules LoadLocking() {
    Locking::Rules rules;
    std::string error;
    CHECK_MSG(Locking::LoadRules(kUi, rules, error), "ui.json's locking reads: " + error);
    return rules;
}

// The original's level order in shape: four chapters of 32.
Chapters::Table FourChapters() {
    Chapters::Table table;
    for (int world = 0; world < 4; ++world) {
        for (int level = 0; level < 32; ++level) {
            Chapters::Level entry;
            entry.world = world;
            entry.index = level;
            entry.name = "w" + std::to_string(world) + "l" + std::to_string(level);
            table.levels.push_back(entry);
        }
    }
    return table;
}

Scores::Store Save() {
    Scores::Store store;
    std::string error;
    CHECK(store.Open("", error));
    return store;
}

// A list of this suite's own: `perWorld` achievements in each of `worlds` chapters,
// one a level from 0, with made-up words.
Achievements::Content MadeUp(int worlds, int perWorld) {
    Achievements::Content content;
    int id = 1000;
    for (int world = 0; world < worlds; ++world) {
        for (int i = 0; i < perWorld; ++i) {
            Achievements::Entry entry;
            entry.id = id++;
            entry.world = world;
            entry.level = i;
            entry.points = 1 + i;
            entry.title = "T" + std::to_string(entry.id);
            entry.description = "D" + std::to_string(entry.id);
            entry.icon = "i" + std::to_string(entry.id) + ".png";
            content.entries.push_back(entry);
        }
    }
    content.secret.title = "S";
    content.secret.descriptionPrefix = "P";
    content.secret.descriptionSeparator = "~";
    content.secret.icon = "s.png";
    return content;
}

// ---- credits ---------------------------------------------------------------------

void TheCreditsFileSaysWhatWasDecoded() {
    const Credits::Rules rules = LoadCredits();
    CHECK(rules.background.sprite == "entities/main_menu_bg.png" &&
          rules.background.sizeUnits == glm::dvec2(512.0, 256.0));
    CHECK(rules.back.sprite == "back_button_credits_screen.png" && rules.back.atScreen == glm::dvec2(0.05, 0.5) &&
          rules.back.origin == glm::dvec2(0.05, 0.5) && rules.back.sizeUnits == glm::dvec2(64.0));
    // setCreditButtonBounceEffect(back, 0.03): 1 - 0.03 first, 300 ms.
    CHECK(rules.backBounce.scaleA == glm::dvec2(0.97) && rules.backBounce.scaleB == glm::dvec2(1.03) &&
          rules.backBounce.strideMs == 300.0);
    CHECK(rules.papyrus.sprite == "papyrus_bg.png" && rules.papyrus.xOfWidth == glm::dvec2(0.382, 0.618) &&
          rules.papyrus.sizeUnits == glm::dvec2(256.0));
    CHECK(rules.strip.sprite == "credit_name.png" && rules.strip.sizeUnits == glm::dvec2(256.0, 512.0));
    CHECK_EQ(rules.strip.alphaByte, 230);
    CHECK_EQ(rules.strip.screenPxPerSecond, 64.0);
    CHECK_EQ(rules.strip.atScreenPx, 720.0);
    CHECK_EQ(rules.strip.flingDecayPerTick, 0.95);
}

// A-C1: C0, C1, C2 and C3 where info.png and info_b.png have them.
void TheCreditsSitWhereTheStillsPutThem() {
    const Credits::Rules rules = LoadCredits();
    Credits::Scroll scroll = Credits::Start(kView720);
    scroll.y = 501.25 / kPxPerUnit; // info.png's strip
    const std::vector<Credits::Piece> pieces = Credits::Pieces(rules, kView720, 1000.0, scroll, false);
    CHECK_EQ(pieces.size(), static_cast<std::size_t>(4));
    if (pieces.size() != 4) return;
    // The original's order: the scene, the layer, then CreditsScreenLayer::draw's two.
    CHECK(pieces[0].element == Credits::Element::Background && pieces[1].element == Credits::Element::Back &&
          pieces[2].element == Credits::Element::Papyrus && pieces[3].element == Credits::Element::Strip);
    CHECK_MSG(AtPx(pieces[0].rect.min, -80.0, 0.0) && SizePx(pieces[0].rect, 1440.0, 720.0),
              "C0 TL " + Px(pieces[0].rect.min));
    CHECK_MSG(AtPx(pieces[2].rect.min, 302.18, 0.0, 0.01) && SizePx(pieces[2].rect, 720.0, 720.0),
              "C2 TL " + Px(pieces[2].rect.min) + ", 2.8125 px a texel");
    CHECK_MSG(AtPx(pieces[3].rect.min, 302.18, 501.25, 0.01) && SizePx(pieces[3].rect, 720.0, 1440.0),
              "C3 TL " + Px(pieces[3].rect.min) + ", 1.40625 px an hd texel");
    CHECK_MSG(pieces[3].alphaByte == 230 && Near(pieces[3].alphaByte / 255.0, 0.90, 0.03), "C3 alpha 0.90");
    // C1 settled: TL (55, 270), and its bounce about (64, 360) px.
    const Hud::Rect hit = Credits::BackHitRect(rules, kView720, 1000.0);
    CHECK_MSG(AtPx(hit.min, 55.0, 270.0) && SizePx(hit, 180.0, 180.0), "C1 TL " + Px(hit.min));
    const glm::dvec2 origin = hit.min + hit.size * rules.back.origin;
    CHECK_MSG(AtPx(origin, 64.0, 360.0), "C1's origin " + Px(origin));
    for (double ms = 700.0; ms < 2000.0; ms += 7.0) {
        const Hud::Rect drawn = Credits::Pieces(rules, kView720, ms, scroll, false)[1].rect;
        const glm::dvec2 pivot = drawn.min + drawn.size * rules.back.origin;
        const double s = drawn.size.x / hit.size.x;
        CHECK_MSG(AtPx(pivot, 64.0, 360.0, 0.001) && s >= 0.97 - 1e-9 && s <= 1.03 + 1e-9 &&
                      Near(drawn.size.x, drawn.size.y, 1e-9),
                  "A-C4: uniform 0.97..1.03 about the origin at " + Num(ms) + " ms: " + Num(s));
    }
    // I7: stride 300 ms, from 0.97 at the button's creation.
    CHECK_MSG(Near(MenuState::BounceScale(rules.backBounce, 0.0).x, 0.97, 1e-6), "bounce at 0 ms");
    CHECK_MSG(Near(MenuState::BounceScale(rules.backBounce, 300.0).x, 1.03, 1e-6), "bounce at 300 ms");
    CHECK_MSG(Near(MenuState::BounceScale(rules.backBounce, 600.0).x, 0.97, 1e-6), "bounce at 600 ms");
    // Held: the press tint.
    CHECK_EQ(Credits::Pieces(rules, kView720, 1000.0, scroll, true)[1].rgbByte, 204);
    // At 4:3 the papyrus goes with the width, and stays 256 units.
    const Credits::Piece papyrus43 = Credits::Pieces(rules, kView43, 1000.0, scroll, false)[2];
    CHECK_MSG(Near(papyrus43.rect.min.x, kView43.x * 0.382 * 0.618, 1e-9) &&
                  papyrus43.rect.size == glm::dvec2(256.0),
              "4:3 papyrus x " + Num(papyrus43.rect.min.x));
}

// A-C5's slide: C1 comes in 32 units from the left, fading in over 700 ms.
void TheCreditsBackButtonComesIn() {
    const Credits::Rules rules = LoadCredits();
    const Credits::Scroll scroll = Credits::Start(kView720);
    const Hud::Rect start = Credits::BackHitRect(rules, kView720, 0.0);
    const Hud::Rect settled = Credits::BackHitRect(rules, kView720, 700.0);
    CHECK_MSG(Near(settled.min.x - start.min.x, 32.0, 1e-9) && Near(settled.min.y, start.min.y, 1e-9),
              "from 32 u to the left: " + Num(settled.min.x - start.min.x));
    // smoothEnd: 69.97 / 42.12 / 8.91 px still to go at 0.10 / 0.25 / 0.50 s.
    const auto left = [&](double ms) {
        return (settled.min.x - Credits::BackHitRect(rules, kView720, ms).min.x) * kPxPerUnit;
    };
    CHECK_MSG(Near(left(100.0), 69.97, 0.05) && Near(left(250.0), 42.12, 0.05) && Near(left(500.0), 8.91, 0.05),
              "A-S2 " + Num(left(100.0)) + " / " + Num(left(250.0)) + " / " + Num(left(500.0)));
    CHECK_EQ(Credits::Pieces(rules, kView720, 0.0, scroll, false)[1].alphaByte, 0);
    CHECK_EQ(Credits::Pieces(rules, kView720, 350.0, scroll, false)[1].alphaByte, 127);
    CHECK_EQ(Credits::Pieces(rules, kView720, 700.0, scroll, false)[1].alphaByte, 255);
}

// A-C2 and A-C3: 22.76 u/s at every view (P2), and the wrap after 768 u.
void TheStripRisesAndComesRound() {
    const Credits::Rules rules = LoadCredits();
    const double speed = Credits::UnitsPerSecond(rules, kView720);
    CHECK_MSG(Near(speed, 64.0 / kPxPerUnit, 1e-9) && Near(speed * kPxPerUnit, 64.0, 1e-9),
              "22.7556 u/s, 64 px/s at 720p: " + Num(speed));
    CHECK_MSG(Near(Credits::UnitsPerSecond(rules, kView43), speed, 1e-12), "the same at 4:3");

    Credits::Scroll scroll = Credits::Start(kView720);
    CHECK_EQ(scroll.y, 256.0);
    for (int tick = 0; tick < 600; ++tick) Credits::Step(rules, scroll, kView720, kTickMs, false, 0.0);
    CHECK_MSG(Near((256.0 - scroll.y) * kPxPerUnit / 10.0, 64.0, 64.0 * 0.01), "10 s: " + Num(256.0 - scroll.y));
    CHECK_MSG(Near(256.0 - scroll.y, speed * 10.0, 1e-6), "exactly 227.56 u in 600 ticks");

    // The wrap: the top re-enters on the bottom edge once it is 512 u above the top.
    scroll = Credits::Start(kView720);
    int wrapped = -1;
    for (int tick = 1; tick <= 2100 && wrapped < 0; ++tick) {
        const double before = scroll.y;
        Credits::Step(rules, scroll, kView720, kTickMs, false, 0.0);
        if (scroll.y > before) wrapped = tick;
    }
    CHECK_MSG(wrapped == 2025 || wrapped == 2026, "the wrap on tick " + std::to_string(wrapped) + " (768 u / 0.3793)");
    CHECK_MSG(Near(scroll.y, 256.0, 1e-9), "back on the bottom edge");
    CHECK_MSG(Near(wrapped / 60.0, 33.75, 33.75 * 0.01), "period " + Num(wrapped / 60.0) + " s");
    // And the other way: dragged below the bottom, it comes back above the top.
    scroll.y = 250.0;
    Credits::Step(rules, scroll, kView720, kTickMs, true, 10.0);
    CHECK_EQ(scroll.y, -512.0);
}

// A-C6: 1:1 while held, then the last tick's move decaying x0.95 a tick on top of
// the scroll.
void TheStripFollowsAFingerAndIsFlung() {
    const Credits::Rules rules = LoadCredits();
    const double v = Credits::UnitsPerSecond(rules, kView720) * kTickMs / 1000.0;
    Credits::Scroll scroll = Credits::Start(kView720);
    for (int tick = 0; tick < 10; ++tick) Credits::Step(rules, scroll, kView720, kTickMs, false, 0.0);
    double y = 256.0 - 10.0 * v;
    CHECK(Near(scroll.y, y, 1e-9));
    // Down, then 100 u up over 20 ticks: the strip goes with it, nothing else.
    Credits::Step(rules, scroll, kView720, kTickMs, true, 0.0);
    bool followed = Near(scroll.y, y, 1e-9);
    for (int tick = 0; tick < 20; ++tick) {
        Credits::Step(rules, scroll, kView720, kTickMs, true, -5.0);
        y -= 5.0;
        followed = followed && Near(scroll.y, y, 1e-9);
    }
    CHECK_MSG(followed, "1:1 while held");
    CHECK_EQ(scroll.moveSpeed, -5.0);
    bool flung = true;
    double speed = -5.0;
    for (int tick = 0; tick < 20; ++tick) {
        Credits::Step(rules, scroll, kView720, kTickMs, false, 0.0);
        speed *= 0.95;
        y -= v - speed;
        flung = flung && Near(scroll.y, y, 1.0 / kPxPerUnit);
    }
    CHECK_MSG(flung, "the fling decays x0.95 a tick on top of the scroll, within 1 px a tick");
    CHECK(Near(scroll.moveSpeed, -5.0 * std::pow(0.95, 20.0), 1e-9));
}

// ---- what the save has opened -----------------------------------------------------

void TheSaveOpensAsTheOriginal() {
    const Locking::Rules rules = LoadLocking();
    CHECK_EQ(rules.medalMax, 3);
    CHECK_EQ(rules.chapterUnlockPercent, 60);
    const Chapters::Table chapters = FourChapters();
    Scores::Store save = Save();
    CHECK_EQ(Locking::LevelsIn(chapters, 0), 32);
    CHECK_EQ(Locking::ChapterCompletion(rules, save, chapters, 0), 0);
    CHECK(Locking::ChapterUnlocked(rules, save, chapters, 0) && !Locking::ChapterUnlocked(rules, save, chapters, 1));
    CHECK(Locking::LevelUnlocked(rules, save, chapters, 0, 0) && Locking::LevelUnlocked(rules, save, chapters, 0, -1));
    CHECK(!Locking::LevelUnlocked(rules, save, chapters, 0, 1) && !Locking::LevelUnlocked(rules, save, chapters, 1, 0));
    save.Record(0, 0, Scores::kBronze);
    CHECK_MSG(Locking::LevelUnlocked(rules, save, chapters, 0, 1), "a level opens when the one before is scored");
    // All bronze is 33%, all gold 100% (ui3 spec 5.4).
    Scores::Store bronze = Save();
    Scores::Store gold = Save();
    for (int level = 0; level < 32; ++level) {
        bronze.Record(0, level, Scores::kBronze);
        gold.Record(0, level, Scores::kGold);
    }
    CHECK_EQ(Locking::ChapterCompletion(rules, bronze, chapters, 0), 33);
    CHECK_EQ(Locking::ChapterCompletion(rules, gold, chapters, 0), 100);
    CHECK(!Locking::ChapterUnlocked(rules, bronze, chapters, 1) && Locking::ChapterUnlocked(rules, gold, chapters, 1));
    // 58 medals are 60% (60.4), 57 are 59%: the line is at 60.
    Scores::Store edge = Save();
    for (int level = 0; level < 19; ++level) edge.Record(0, level, Scores::kGold);
    CHECK_EQ(Locking::ChapterCompletion(rules, edge, chapters, 0), 59); // 57
    edge.Record(0, 19, Scores::kBronze);
    CHECK_EQ(Locking::ChapterCompletion(rules, edge, chapters, 0), 60); // 58
    CHECK(Locking::ChapterUnlocked(rules, edge, chapters, 1));
    CHECK_EQ(Locking::ChapterCompletion(rules, gold, chapters, 4), 0);
}

void AchievementsFollowTheSave() {
    const Locking::Rules locking = LoadLocking();
    const Chapters::Table chapters = FourChapters();
    Achievements::Content content = MadeUp(2, 3);
    content.entries[2].isSecret = true;
    content.fromCompletion.push_back({0, 50, 1000});
    content.fromCompletion.push_back({0, 100, 1001});
    content.fromLevelMedal.push_back({1, 0, 3, 1003});
    content.fromLevelMedal.push_back({0, 1, 1, 1002});
    Scores::Store save = Save();
    CHECK_MSG(Achievements::Unlocked(content, locking, save, chapters).empty(), "a fresh save unlocks nothing");
    for (int level = 0; level < 16; ++level) save.Record(0, level, Scores::kGold); // 50%
    std::vector<int> unlocked = Achievements::Unlocked(content, locking, save, chapters);
    CHECK_MSG(unlocked == std::vector<int>({1000, 1002}), "50% and level 0-1 scored");
    CHECK_EQ(Achievements::Points(content, unlocked), 1 + 3);
    save.Record(1, 0, Scores::kSilver);
    CHECK_MSG(!Achievements::IsUnlocked(Achievements::Unlocked(content, locking, save, chapters), 1003),
              "a medal rule wants its medal");
    save.Record(1, 0, Scores::kGold);
    unlocked = Achievements::Unlocked(content, locking, save, chapters);
    CHECK(Achievements::IsUnlocked(unlocked, 1003) && !Achievements::IsUnlocked(unlocked, 1001));
    // A secret one says so while locked, and its own once open.
    const Achievements::Entry& secret = content.entries[2];
    CHECK(Achievements::TitleOf(content, secret, false) == "S" && Achievements::IconOf(content, secret, false) == "s.png");
    CHECK_MSG(Achievements::DescriptionOf(content, secret, false) == "P1~3", "prefix, world + 1, separator, level + 1");
    CHECK(Achievements::TitleOf(content, secret, true) == secret.title &&
          Achievements::DescriptionOf(content, secret, true) == secret.description);
    CHECK(Achievements::TitleOf(content, content.entries[0], false) == content.entries[0].title);
}

void TheAchievementsFileIsReadStrictly() {
    const std::string good =
        R"({"format": 1, "achievements": [{"id": 7, "world": 0, "level": -1, "points": 5, "title": "a",
        "description": "b", "icon": "c.png", "is_new": false, "is_secret": true}],
        "secret": {"title": "s", "description_prefix": "p", "description_separator": "-", "icon": "i.png"},
        "unlock": {"from_completion": [{"world": 0, "min_percent": 50, "id": 7}],
                   "from_level_medal": [{"world": 0, "level": 3, "min_medal": 3, "id": 7}]}})";
    Achievements::Content content;
    std::string error;
    CHECK_MSG(Achievements::Load(Write("good.json", good), content, error), "a good file reads: " + error);
    CHECK(content.entries.size() == 1 && content.entries[0].id == 7 && content.entries[0].level == -1 &&
          content.entries[0].isSecret && content.secret.descriptionSeparator == "-");
    CHECK(content.fromCompletion.size() == 1 && content.fromCompletion[0].minPercent == 50 &&
          content.fromLevelMedal.size() == 1 && content.fromLevelMedal[0].minMedal == 3);
    const auto refused = [&good](const char* name, const std::string& needle, const std::string& with) {
        std::string bad = good;
        const std::size_t at = bad.find(needle);
        CHECK_MSG(at != std::string::npos, std::string(name) + " has " + needle);
        if (at == std::string::npos) return;
        bad.replace(at, needle.size(), with);
        Achievements::Content out;
        out.entries.resize(3);
        std::string why;
        CHECK_MSG(!Achievements::Load(Write(name, bad), out, why) && !why.empty() && out.entries.size() == 3,
                  std::string(name) + " is refused and leaves what it had: " + why);
    };
    refused("format.json", "\"format\": 1", "\"format\": 2");
    refused("level.json", "\"level\": -1", "\"level\": -2");
    refused("points.json", "\"points\": 5", "\"points\": \"5\"");
    refused("secret.json", "\"description_prefix\"", "\"prefix\"");
    refused("rule.json", "\"min_medal\"", "\"medal\"");
    Achievements::Content none;
    CHECK(!Achievements::Load((Scratch() / "absent.json").string(), none, error) && !error.empty());
}

// ---- the dashboard -----------------------------------------------------------------

void TheDashboardFileSaysWhatWasDecoded() {
    const Dashboard::Rules rules = LoadDashboard();
    CHECK(rules.background.sprite == "entities/world_select_bg.png" &&
          rules.background.sizeUnits == glm::dvec2(512.0, 256.0));
    CHECK(rules.back.atScreen == glm::dvec2(-0.025, 0.5) && rules.back.origin == glm::dvec2(-0.025, 0.5) &&
          rules.back.sizeUnits == glm::dvec2(64.0));
    CHECK(rules.rows.tileUnits == 44.0 && rules.rows.lineFactor == 1.333 && rules.rows.columnFactor == 1.4 &&
          rules.rows.barLines == 4.0);
    CHECK(rules.rows.iconDirectory == "sprites/achievements/" && rules.rows.barSprite == "itembg.png" &&
          rules.rows.lockSprite == "lock_icon.png");
    CHECK(rules.rows.lockedIconAlphaByte == 64 && rules.rows.lockedBarAlphaByte == 180 &&
          rules.rows.lockedTextAlphaByte == 127 && rules.rows.lockedPointsAlphaByte == 64);
    CHECK(rules.header.font == "Matura84_shadow.fnt" && rules.header.unitsPerFontPx == 0.5 &&
          rules.header.atUnits == glm::dvec2(132.0, 32.0) && rules.headerPrefix == "Chapter ");
    CHECK(rules.title.font == "Verdana40_shadow.fnt" && rules.title.unitsPerFontPx == 0.375 &&
          rules.title.atUnits == glm::dvec2(9.0, 8.0));
    CHECK(rules.description.unitsPerFontPx == 0.3 && rules.description.atUnits == glm::dvec2(13.0, 24.0));
    CHECK(rules.points.unitsPerFontPx == 0.3 && rules.points.atUnits == glm::dvec2(22.0, 50.0));
    CHECK(rules.total.font == "Matura84_shadow.fnt" && rules.total.unitsPerFontPx == 0.5 &&
          rules.total.atUnits == glm::dvec2(32.0, 32.0));
    CHECK(rules.newLabel.words == "new!" && rules.newLabel.text.atUnits == glm::dvec2(182.0, 6.0) &&
          rules.newLabel.rgbBytes == glm::ivec3(100, 255, 100) && rules.newLabel.alphaFloorByte == 127 &&
          rules.newLabel.alphaPeriodMs == 255);
    CHECK(rules.plaque.sprite == "achievement_points.png" && rules.plaque.sizeUnits == glm::dvec2(64.0));
    CHECK(rules.scroll.momentumDecayPerTick == 0.9 && rules.scroll.topBandPerTick == 0.7 &&
          rules.scroll.bottomBandPerTick == 0.3 && rules.scroll.wheelUnitsPerNotch == 10.0);
    CHECK(rules.bar.sprite == "scroll_bar.png" && rules.bar.sizeUnits == glm::dvec2(8.0, 64.0) &&
          rules.bar.fromRightUnits == 7.5 && rules.bar.alphaByte == 200);
    CHECK(rules.start.sprite == "popup_close_button.png" && rules.start.offsetUnits == glm::dvec2(174.0, -2.0) &&
          rules.start.origin == glm::dvec2(0.0) && rules.start.dismissAfterUnits == 6.0);
}

// A-A1 and A-A2's geometry: every piece where achievements.png has it.
void TheDashboardSitsWhereTheStillPutsIt() {
    const Dashboard::Rules rules = LoadDashboard();
    const Dashboard::Layout layout = Dashboard::LayOut(rules, kView720);
    CHECK_MSG(Near(layout.lineOffset, 58.652, 1e-9) && Near(layout.lineOffset * kPxPerUnit, 164.96, 0.01),
              "the pitch 58.652 u, 164.96 px");
    CHECK(Near(layout.columnAdvance, 61.6, 1e-9) && layout.iconSize == glm::dvec2(44.0));
    CHECK_MSG(Near(layout.barSize.x, 234.608, 1e-9) && Near(layout.barSize.x * kPxPerUnit, 659.8, 0.1),
              "the bar 234.61 x 58.652 u");
    const Dashboard::Layout narrow = Dashboard::LayOut(rules, kView43);
    CHECK_MSG(Near(narrow.barSize.x, kView43.x - 123.2, 1e-9), "at 4:3 the width decides: " + Num(narrow.barSize.x));

    const Chapters::Table chapters = FourChapters();
    Scores::Store gold = Save();
    for (const Chapters::Level& level : chapters.levels) gold.Record(level.world, level.index, Scores::kGold);
    Achievements::Content content = MadeUp(1, 5);
    for (const Achievements::Entry& entry : content.entries) content.fromLevelMedal.push_back({0, entry.level, 1, entry.id});
    const Dashboard::Board board = Dashboard::Build(&content, LoadLocking(), gold, chapters);
    CHECK_EQ(board.lines.size(), static_cast<std::size_t>(6));
    CHECK_EQ(board.points, 1 + 2 + 3 + 4 + 5);
    const Dashboard::State state;
    const std::vector<Dashboard::Piece> pieces = Dashboard::Pieces(rules, board, state, kView720, 1000.0, 0.0);
    const auto sprite = [&pieces](const std::string& file, int nth = 0) -> const Dashboard::Piece* {
        for (const Dashboard::Piece& piece : pieces) {
            if (piece.kind == Dashboard::Kind::Sprite && piece.file == file && nth-- == 0) return &piece;
        }
        return nullptr;
    };
    const auto words = [&pieces](const std::string& text) -> const Dashboard::Piece* {
        for (const Dashboard::Piece& piece : pieces) {
            if (piece.kind == Dashboard::Kind::Text && piece.words == text) return &piece;
        }
        return nullptr;
    };
    const Dashboard::Piece* bg = sprite("entities/world_select_bg.png");
    CHECK_MSG(bg != nullptr && AtPx(bg->rect.min, -80.0, 0.0) && &pieces.front() == bg, "A0 first, TL (-80, 0)");
    const Dashboard::Piece* back = sprite("back_button_credits_screen.png");
    CHECK_MSG(back != nullptr && AtPx(back->rect.min, -27.5, 270.0) && SizePx(back->rect, 180.0, 180.0),
              "A1 TL (-27.5, 270)");
    const Dashboard::Piece* plaque = sprite("achievement_points.png");
    CHECK_MSG(plaque != nullptr && AtPx(plaque->rect.min, 0.0, 0.0) && SizePx(plaque->rect, 180.0, 180.0), "A2");
    const Dashboard::Piece* bar = sprite("scroll_bar.png");
    CHECK_MSG(bar != nullptr && AtPx(bar->rect.min, 1258.91, 0.0, 0.01) && SizePx(bar->rect, 22.5, 180.0) &&
                  Near(bar->alphaByte / 255.0, 0.784, 0.03) && &pieces[1] == bar,
              "A12 TL (1258.91, 0), 0.703 px a texel, alpha 0.784, under the rows");
    for (int row = 1; row <= 3; ++row) {
        const Dashboard::Piece* icon = sprite("sprites/achievements/i" + std::to_string(999 + row) + ".png");
        const Dashboard::Piece* item = sprite("itembg.png", row - 1);
        CHECK_MSG(icon != nullptr && AtPx(icon->rect.min, 173.25, 164.95875 * row, 0.01) &&
                      SizePx(icon->rect, 123.75, 123.75) && icon->alphaByte == 255,
                  "A5 row " + std::to_string(row));
        CHECK_MSG(item != nullptr && AtPx(item->rect.min, 297.0, 164.95875 * row, 0.01) &&
                      SizePx(item->rect, 659.835, 164.95875, 0.01),
                  "A6 row " + std::to_string(row));
    }
    const Dashboard::Piece* total = words("15");
    CHECK_MSG(total != nullptr && total->centred && AtPx(total->at, 90.0, 90.0) && total->unitsPerFontPx == 0.5,
              "A3 centred (90, 90) px");
    const Dashboard::Piece* header = words("Chapter 1");
    CHECK_MSG(header != nullptr && header->centred && AtPx(header->at, 544.5, 90.0), "A4 centred (544.5, 90) px");
    const Dashboard::Piece* title = words("T1000");
    CHECK_MSG(title != nullptr && !title->centred && AtPx(title->at, 322.31, 187.46, 0.01) &&
                  title->unitsPerFontPx == 0.375,
              "A7 TL " + (title != nullptr ? Px(title->at) : std::string()));
    const Dashboard::Piece* description = words("D1000");
    CHECK_MSG(description != nullptr && !description->centred && AtPx(description->at, 333.56, 232.46, 0.01) &&
                  description->unitsPerFontPx == 0.3,
              "A8 TL");
    const Dashboard::Piece* points = words("1");
    CHECK_MSG(points != nullptr && points->centred && AtPx(points->at, 235.125, 305.58, 0.01) &&
                  points->unitsPerFontPx == 0.3,
              "A9 centred (235.1, 305.6) px");
    // The layer last: A1 after the plaque and the total.
    CHECK_MSG(back > total && total > plaque, "A2, A3, then the layer");
}

// A13, A11, A10 and the locked alphas (ui3 spec 4.3), and the draw order of a row.
void TheRowsSayWhatIsLocked() {
    const Dashboard::Rules rules = LoadDashboard();
    const Chapters::Table chapters = FourChapters();
    Scores::Store save = Save();
    save.Record(0, 0, Scores::kGold);
    Achievements::Content content = MadeUp(2, 3);
    content.entries[0].isNew = true;
    content.fromLevelMedal.push_back({0, 0, 3, content.entries[0].id});
    const Dashboard::Board board = Dashboard::Build(&content, LoadLocking(), save, chapters);
    // A header before each chapter's first.
    CHECK_EQ(board.lines.size(), static_cast<std::size_t>(8));
    CHECK(board.lines[0].header && board.lines[0].world == 0 && board.lines[4].header && board.lines[4].world == 1);
    CHECK(board.lines[1].unlocked && board.lines[1].levelUnlocked);
    CHECK_MSG(!board.lines[2].unlocked && board.lines[2].levelUnlocked, "0-2: locked, its level open (0-1 scored)");
    CHECK_MSG(!board.lines[3].unlocked && !board.lines[3].levelUnlocked, "0-3: locked, and its level too");
    CHECK_MSG(!board.lines[5].levelUnlocked, "chapter 2 is locked on this save");
    CHECK_EQ(board.achievements, 6);
    CHECK_EQ(board.worlds, 4);

    const Dashboard::State state;
    const std::vector<Dashboard::Piece> pieces = Dashboard::Pieces(rules, board, state, kView720, 1000.0, 300.0);
    // Row 1, open and new: icon, bar, new!, title, description, points.
    std::vector<std::string> order;
    for (const Dashboard::Piece& piece : pieces) order.push_back(piece.kind == Dashboard::Kind::Text ? piece.words : piece.file);
    const std::vector<std::string> expected = {"entities/world_select_bg.png",
                                               "scroll_bar.png",
                                               "Chapter 1",
                                               "sprites/achievements/i1000.png",
                                               "itembg.png",
                                               "new!",
                                               "T1000",
                                               "D1000",
                                               "1",
                                               "sprites/achievements/i1001.png",
                                               "itembg.png",
                                               "T1001",
                                               "D1001",
                                               "2",
                                               "sprites/achievements/i1002.png",
                                               "lock_icon.png",
                                               "itembg.png",
                                               "T1002",
                                               "D1002",
                                               "3",
                                               "Chapter 2"};
    bool ordered = order.size() >= expected.size();
    for (std::size_t i = 0; ordered && i < expected.size(); ++i) ordered = order[i] == expected[i];
    std::string got;
    for (const std::string& piece : order) got += piece + " ";
    CHECK_MSG(ordered, "the loop's order: " + got);
    // Line 4, the second header, is at 234.6 u; line 5 is past the view and culled.
    CHECK_MSG(std::find(order.begin(), order.end(), "Chapter 2") != order.end() &&
                  std::find(order.begin(), order.end(), "T1003") == order.end(),
              "the second header is drawn, the row below the view is not");
    const auto find = [&pieces](const std::string& text) -> const Dashboard::Piece* {
        for (const Dashboard::Piece& piece : pieces) {
            if ((piece.kind == Dashboard::Kind::Text ? piece.words : piece.file) == text) return &piece;
        }
        return nullptr;
    };
    const Dashboard::Piece* newLabel = find("new!");
    CHECK_MSG(newLabel != nullptr && newLabel->rgbBytes == glm::ivec3(100, 255, 100) && newLabel->alphaByte == 127 &&
                  AtPx(newLabel->at, (105.6 + 182.0) * kPxPerUnit, (58.652 + 6.0) * kPxPerUnit, 0.01),
              "new! at bar + (182, 6): 300 ms is 45 of 255, floored at 127");
    const Dashboard::Piece* icon1 = find("sprites/achievements/i1001.png");
    const Dashboard::Piece* title1 = find("T1001");
    const Dashboard::Piece* points1 = find("2");
    CHECK_MSG(icon1 != nullptr && icon1->alphaByte == 64 && title1 != nullptr && title1->alphaByte == 127 &&
                  points1 != nullptr && points1->alphaByte == 64,
              "locked: icon 64, words 127, points 64");
    CHECK_MSG(pieces[10].file == "itembg.png" && pieces[10].alphaByte == 180, "locked bar 180");
    const std::vector<Dashboard::Piece> later = Dashboard::Pieces(rules, board, state, kView720, 1000.0, 200.0);
    for (const Dashboard::Piece& piece : later) {
        if (piece.words == "new!") CHECK_EQ(piece.alphaByte, 200);
    }
}

// A-A6: the list as long as the original's - 82 achievements in four chapters of
// 18, 15, 21 and 28 - scrolls to -4788.07 u, where the bar is 192 u down.
void TheListIsAsLongAsTheOriginals() {
    const Dashboard::Rules rules = LoadDashboard();
    Achievements::Content content;
    const int perWorld[] = {18, 15, 21, 28};
    for (int world = 0; world < 4; ++world) {
        for (int i = 0; i < perWorld[world]; ++i) {
            Achievements::Entry entry;
            entry.id = static_cast<int>(content.entries.size());
            entry.world = world;
            content.entries.push_back(entry);
        }
    }
    const Dashboard::Board board = Dashboard::Build(&content, LoadLocking(), Save(), FourChapters());
    CHECK_EQ(board.lines.size(), static_cast<std::size_t>(86));
    CHECK_MSG(Near(Dashboard::Stride(rules, board, kView720), 5044.07, 0.5), "stride " + Num(Dashboard::Stride(rules, board, kView720)));
    const double minScroll = Dashboard::MinScroll(rules, board, kView720);
    CHECK_MSG(Near(minScroll, -4788.07, 0.5), "minScroll " + Num(minScroll));
    CHECK_MSG(Near(Dashboard::BarY(rules, board, kView720, minScroll), 192.0, 1e-9), "A12 at 192 u at the bottom");
    CHECK_EQ(Dashboard::BarY(rules, board, kView720, 0.0), 0.0);

    // The bottom band closes 0.3 of the overshoot a tick, the top 0.3 too (x0.7).
    Dashboard::State state;
    state.scroll = minScroll - 100.0;
    Dashboard::DoScrolling(rules, state, minScroll, false, 0.0);
    CHECK_MSG(Near(state.scroll, minScroll - 70.0, 1e-9), "bottom band: " + Num(state.scroll - minScroll));
    Dashboard::DoScrolling(rules, state, minScroll, false, 0.0);
    CHECK(Near(state.scroll, minScroll - 49.0, 1e-9));
    // Culling: a row whose top is on the bottom edge draws, one past it does not; a
    // row whose bottom is on the top edge draws, one above it does not.
    const double pitch = Dashboard::LayOut(rules, kView720).lineOffset;
    CHECK(Dashboard::LineShown(rules, kView720, 0, 256.0) && !Dashboard::LineShown(rules, kView720, 0, 256.001));
    CHECK(Dashboard::LineShown(rules, kView720, 0, -pitch) && !Dashboard::LineShown(rules, kView720, 0, -pitch - 0.001));
}

// doScrolling's order, and A-A3 and A-A4's models.
void TheListGlidesAndSpringsBack() {
    const Dashboard::Rules rules = LoadDashboard();
    const double minScroll = -4788.07;
    Dashboard::State state;
    state.scroll = 10.0;
    state.moveSpeed = 5.0;
    Dashboard::DoScrolling(rules, state, minScroll, false, 0.0);
    CHECK_MSG(Near(state.moveSpeed, 4.5, 1e-12) && Near(state.scroll, (10.0 + 4.5) * 0.7, 1e-12),
              "momentum first, then the band: " + Num(state.scroll));

    // A-A3: a slow drag let go glides 9 times its last move at x0.9 a tick.
    state = Dashboard::State{};
    state.scroll = -1000.0;
    for (int tick = 0; tick < 10; ++tick) Dashboard::DoScrolling(rules, state, minScroll, true, -4.0);
    CHECK(Near(state.scroll, -1040.0, 1e-9) && Near(state.accumulated, -40.0, 1e-9));
    const double released = state.scroll;
    double previous = 0.0;
    bool factor = true;
    for (int tick = 0; tick < 300; ++tick) {
        const double before = state.scroll;
        Dashboard::DoScrolling(rules, state, minScroll, false, 0.0);
        const double step = state.scroll - before;
        if (tick > 0 && std::fabs(previous) > 1e-6) factor = factor && Near(step / previous, 0.9, 0.01);
        previous = step;
    }
    CHECK_MSG(factor, "0.90 a tick");
    CHECK_MSG(Near((state.scroll - released) / -4.0, 9.0, 9.0 * 0.03), "glide " + Num((state.scroll - released) / -4.0) + " x");

    // A-A4: flung down past the top, the list comes back within 1 px of it.
    state = Dashboard::State{};
    for (int tick = 0; tick < 4; ++tick) Dashboard::DoScrolling(rules, state, minScroll, true, 12.0);
    int back = -1;
    double peak = state.scroll;
    for (int tick = 1; tick <= 200 && back < 0; ++tick) {
        Dashboard::DoScrolling(rules, state, minScroll, false, 0.0);
        peak = std::max(peak, state.scroll);
        if (std::fabs(state.scroll) < 1.0 / kPxPerUnit) back = tick;
    }
    CHECK_MSG(back > 0 && back <= 55, "within 1 px of the top " + std::to_string(back) + " ticks after the fling");
    // The fling ends 48 u past the top; the next tick decays the speed to 10.8 and bands
    // 58.8 by x0.7 to 41.16, so the peak is the release itself and the list came back from it.
    CHECK_MSG(peak >= 48.0 && state.scroll < peak, "past the top first: " + Num(peak));
}

// The rows' taps, the start button and the back button, as ScoreDashboard::loop and
// its layer run them.
void ATapOnARowRaisesTheStartButton() {
    const Dashboard::Rules rules = LoadDashboard();
    const Chapters::Table chapters = FourChapters();
    const Achievements::Content content = MadeUp(2, 10);
    const Dashboard::Board board = Dashboard::Build(&content, LoadLocking(), Save(), chapters);
    Dashboard::State state;
    double ms = 1000.0;
    const auto tick = [&](bool pressed, bool held, bool released, const glm::dvec2& at) {
        Dashboard::Touch touch;
        touch.pressed = pressed;
        touch.held = held;
        touch.released = released;
        touch.over = true;
        touch.at = at;
        const Dashboard::Outcome outcome = Dashboard::Tick(rules, board, state, kView720, ms, touch);
        ms += kTickMs;
        return outcome;
    };
    const auto tapAt = [&](const glm::dvec2& at) {
        tick(true, true, false, at);
        return tick(false, false, true, at);
    };
    // Line 1 is 1-1, open on a fresh save.
    const glm::dvec2 row1(150.0, 58.652 + 20.0);
    Dashboard::Outcome outcome = tapAt(row1);
    CHECK_MSG(outcome.pick && !outcome.denied && state.start.present && state.lastClicked == 1,
              "a tap on an open level's row: the pick, and the start button");
    CHECK_MSG(AtPx(state.start.anchor, (105.6 + 174.0) * kPxPerUnit, (58.652 - 2.0) * kPxPerUnit, 0.01),
              "at the bar + (174, -2): " + Px(state.start.anchor));
    CHECK(state.currentWorld == 0 && state.currentLevel == 0);
    const double added = state.start.addedMs;
    outcome = tapAt(row1);
    CHECK_MSG(!outcome.pick && state.start.addedMs == added, "the same row again: nothing new");
    // Line 2 is 1-2, locked on a fresh save.
    outcome = tapAt(glm::dvec2(150.0, 2.0 * 58.652 + 20.0));
    CHECK_MSG(outcome.denied && !outcome.pick && state.currentLevel == 1, "a locked level's row: denied");
    CHECK_MSG(state.start.present && state.lastClicked == 1, "and the start button stays where it was");
    // A touch let go off the row it went down on taps nothing.
    tick(true, true, false, row1);
    outcome = tick(false, false, true, glm::dvec2(150.0, 3.0 * 58.652 + 20.0));
    CHECK(!outcome.pick && !outcome.denied);

    // The start button, once in: pressed on its release, openState(the row's).
    for (int i = 0; i < 45; ++i) tick(false, false, false, glm::dvec2(0.0));
    tapAt(row1); // currentLevel back to 1-1's
    Hud::Rect start;
    CHECK(Dashboard::StartHitRect(rules, state, kView720, ms, start));
    tick(true, true, false, start.Centre());
    CHECK_MSG(state.startHeld, "held: the press tint");
    outcome = tick(false, false, true, start.Centre());
    CHECK_MSG(outcome.start && outcome.world == 0 && outcome.level == 0, "start: level 1-1");

    // Moved further than 6 u: dismissed, and gone 700 ms on.
    tick(true, true, false, glm::dvec2(400.0, 150.0));
    tick(false, true, false, glm::dvec2(400.0, 146.0));
    CHECK_MSG(state.start.dismissedMs < 0.0, "4 u: still there");
    tick(false, true, false, glm::dvec2(400.0, 142.0));
    CHECK_MSG(state.start.dismissedMs >= 0.0 && state.lastClicked == -1 && state.accumulated == 0.0,
              "past 6 u: dismissed, the row forgotten");
    CHECK_MSG(!Dashboard::StartHitRect(rules, state, kView720, ms, start), "a leaving button takes no press");
    tick(false, false, true, glm::dvec2(400.0, 142.0));
    for (int i = 0; i < 42; ++i) tick(false, false, false, glm::dvec2(0.0));
    CHECK_MSG(!state.start.present, "gone after 700 ms");

    // The back button: held, then let go inside.
    const glm::dvec2 backAt = Dashboard::BackHitRect(rules, kView720, ms).Centre();
    tick(true, true, false, backAt);
    CHECK(state.backHeld);
    for (const Dashboard::Piece& piece : Dashboard::Pieces(rules, board, state, kView720, ms, 0.0)) {
        if (piece.file == rules.back.sprite) CHECK_EQ(piece.rgbBytes.r, 204);
    }
    outcome = tick(false, false, true, backAt);
    CHECK_MSG(outcome.back && !state.backHeld, "the back button, on its release");

    // The wheel: 10 u a notch, and no momentum that tick.
    state = Dashboard::State{};
    state.scroll = -300.0;
    Dashboard::Touch wheel;
    wheel.wheelNotches = -2.0;
    Dashboard::Tick(rules, board, state, kView720, ms, wheel);
    CHECK_MSG(Near(state.scroll, -320.0, 1e-9) && Near(state.moveSpeed, -20.0, 1e-9), "two notches: 20 u");
}

void WhatTheInfoScreensRefuse() {
    std::ifstream in(kUi, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto refused = [&text](const char* name, const char* block, const std::string& needle,
                                 const std::string& with, const std::string& named) {
        const std::size_t at = text.find(std::string("\"") + block + "\"");
        const std::size_t found = at == std::string::npos ? std::string::npos : text.find(needle, at);
        CHECK_MSG(found != std::string::npos, std::string(name) + ": ui.json has " + needle);
        if (found == std::string::npos) return;
        std::string bad = text;
        bad.replace(found, needle.size(), with);
        std::string why;
        bool loaded = false;
        const std::string path = Write(name, bad);
        if (std::string(block) == "credits") {
            Credits::Rules out;
            loaded = Credits::LoadRules(path, out, why);
        } else if (std::string(block) == "dashboard") {
            Dashboard::Rules out;
            loaded = Dashboard::LoadRules(path, out, why);
        } else {
            Locking::Rules out;
            loaded = Locking::LoadRules(path, out, why);
        }
        CHECK_MSG(!loaded && why.find(named) != std::string::npos, std::string(name) + " is refused by name: " + why);
    };
    refused("credits_alpha.json", "credits", "\"alpha_byte\": 230", "\"alpha_byte\": 300", "alpha_byte");
    refused("credits_fling.json", "credits", "\"fling_decay_per_tick\": 0.95", "\"fling_decay_per_tick\": 1.5",
            "fling_decay_per_tick");
    refused("credits_speed.json", "credits", "\"speed_screen_px_per_s\": 64", "\"speed_screen_px_per_s\": 0",
            "speed_screen_px_per_s");
    refused("dash_momentum.json", "dashboard", "\"momentum_decay_per_tick\": 0.9", "\"momentum_decay_per_tick\": 0",
            "momentum_decay_per_tick");
    refused("dash_rgb.json", "dashboard", "\"rgb_bytes\": [100, 255, 100]", "\"rgb_bytes\": [100, 255]", "rgb_bytes");
    refused("dash_tile.json", "dashboard", "\"tile_units\": 44", "\"tile_units\": -44", "tile_units");
    refused("lock_percent.json", "locking", "\"chapter_unlock_percent\": 60", "\"chapter_unlock_percent\": 160",
            "chapter_unlock_percent");
}

void runTests() {
    TheCreditsFileSaysWhatWasDecoded();
    TheCreditsSitWhereTheStillsPutThem();
    TheCreditsBackButtonComesIn();
    TheStripRisesAndComesRound();
    TheStripFollowsAFingerAndIsFlung();
    TheSaveOpensAsTheOriginal();
    AchievementsFollowTheSave();
    TheAchievementsFileIsReadStrictly();
    TheDashboardFileSaysWhatWasDecoded();
    TheDashboardSitsWhereTheStillPutsIt();
    TheRowsSayWhatIsLocked();
    TheListIsAsLongAsTheOriginals();
    TheListGlidesAndSpringsBack();
    ATapOnARowRaisesTheStartButton();
    WhatTheInfoScreensRefuse();
}

} // namespace

TEST_MAIN("test_mp_info", 150)
