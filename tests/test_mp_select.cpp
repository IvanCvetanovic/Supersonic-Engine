// Chapter select and the level grid, as numbers: sim/Selector.hpp over the port's
// ui.json and a save, pinned against the remake's ui3 spec (sections 0.5, 5 and 6,
// acceptance 7.5 and 7.6). Every position is stated in the pixels of a 1280x720
// capture of the original, at 2.8125 px a unit.

#include "TestHarness.hpp"

#include "sim/Chapters.hpp"
#include "sim/Hud.hpp"
#include "sim/Locking.hpp"
#include "sim/Scores.hpp"
#include "sim/Selector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace MagicPortals;

namespace {

const std::string kUi = std::string(MAGICPORTALS_PORT_DATA_DIR) + "/ui.json";
constexpr double kPxPerUnit = 2.8125;
const glm::dvec2 kView720(1280.0 / kPxPerUnit, 720.0 / kPxPerUnit); // 455.11 x 256
constexpr double kSettledMs = 1.0e9;

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

Selector::Rules Load() {
    Selector::Rules rules;
    std::string error;
    CHECK_MSG(Selector::LoadRules(kUi, rules, error), "ui.json's selector reads: " + error);
    return rules;
}

Locking::Rules LoadLocking() {
    Locking::Rules rules;
    std::string error;
    CHECK_MSG(Locking::LoadRules(kUi, rules, error), "ui.json's locking reads: " + error);
    return rules;
}

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

// A save with every level of every chapter at `medal` (0: fresh).
Scores::Store SaveAll(int medal) {
    Scores::Store store;
    std::string error;
    CHECK(store.Open("", error));
    if (medal > 0) {
        for (int world = 0; world < 4; ++world) {
            for (int level = 0; level < 32; ++level) store.Record(world, level, medal);
        }
    }
    return store;
}

const Selector::Piece* Find(const std::vector<Selector::Piece>& pieces, const std::string& file, int nth = 0) {
    for (const Selector::Piece& piece : pieces) {
        if (piece.file != file && piece.words != file) continue;
        if (nth-- == 0) return &piece;
    }
    return nullptr;
}

int Count(const std::vector<Selector::Piece>& pieces, const std::string& file) {
    return static_cast<int>(std::count_if(pieces.begin(), pieces.end(), [&file](const Selector::Piece& piece) {
        return piece.file == file || piece.words == file;
    }));
}

void TheSelectorFileSaysWhatWasDecoded() {
    const Selector::Rules rules = Load();
    CHECK(Near(rules.pager.decayPerCall, 0.92, 1e-12));
    CHECK(Near(rules.pager.snapBelow, 0.0005, 1e-12));
    CHECK(Near(rules.pager.swapOffset, 0.2, 1e-12));
    CHECK(Near(rules.pager.rubberBand, 1.0 / 3.0, 1e-9));
    CHECK(Near(rules.pager.arrowFadeOutFactor, 0.96, 1e-12) && Near(rules.pager.arrowFadeInStep, 0.05, 1e-12));
    CHECK_MSG(rules.chapters.perPage == 2, "owner ruling R1: two chapters a page");
    CHECK(rules.levels.columns == 4 && rules.levels.rows == 4);
    CHECK_MSG(rules.chapters.back.sprite == "level_select_forward.png" &&
                  rules.chapters.forward.sprite == "level_select_back.png",
              "the arrow files the wrong way round from their pictures, as the decode has them");
    CHECK(rules.chapters.counter.tintAlphaByte == 220 && rules.chapters.counter.dotFrame == 15);
    CHECK(rules.levels.cornerArgb == glm::ivec4(155, 33, 22, 11));
}

void TheSaveOpensAsTheOriginal() {
    const Selector::Rules rules = Load();
    const Locking::Rules locking = LoadLocking();
    const Chapters::Table table = FourChapters();

    // Fresh: chapter 1 open, the rest locked; level 1 open, the rest locked.
    const Scores::Store fresh = SaveAll(0);
    const Selector::Board chapters = Selector::BuildChapters(rules, locking, fresh, table);
    CHECK_EQ(chapters.items, 4);
    CHECK_EQ(chapters.Pages(), 2);
    CHECK_MSG(chapters.chapters[0].unlocked && !chapters.chapters[1].unlocked && !chapters.chapters[3].unlocked,
              "fresh: chapter 1 open, chapter 2 locked (5.4)");
    CHECK_EQ(chapters.highlighted, 0);
    CHECK_MSG(chapters.chapters[0].completion == 0 && !chapters.chapters[0].warned, "no W4-W6 on a fresh save");
    const Selector::Board grid = Selector::BuildLevels(rules, locking, fresh, table, 0);
    CHECK_EQ(grid.items, 32);
    CHECK_EQ(grid.Pages(), 2);
    CHECK_MSG(grid.tiles[0].unlocked && !grid.tiles[1].unlocked && grid.tiles[31].boss && !grid.tiles[30].boss,
              "tile 1 open, the rest locked, the boss last (6.4)");
    CHECK_EQ(grid.highlighted, 0);
    CHECK_EQ(Selector::OpeningPage(grid, 0), 0);

    // Bronze everywhere: 33%, a bronze medal and the warning; chapter 2 still locked.
    const Scores::Store bronze = SaveAll(Scores::kBronze);
    const Selector::Board short33 = Selector::BuildChapters(rules, locking, bronze, table);
    CHECK_EQ(short33.chapters[0].completion, 33);
    CHECK_MSG(short33.chapters[0].warned && short33.chapters[1].warned, "'requires 60%' on both, locked or not");
    CHECK_MSG(!short33.chapters[1].unlocked, "chapter 2 locked at 33%");
    CHECK(Selector::ChapterMedal(rules, 33) == "medal_bronze_l.png");

    // Gold everywhere: all open, 100%, no warning, the grid opening on page 2.
    const Scores::Store gold = SaveAll(Scores::kGold);
    const Selector::Board full = Selector::BuildChapters(rules, locking, gold, table);
    CHECK_MSG(std::all_of(full.chapters.begin(), full.chapters.end(),
                          [](const Selector::Chapter& c) { return c.unlocked && c.completion == 100 && !c.warned; }),
              "gold: every chapter open at 100%, no warning");
    CHECK_EQ(full.highlighted, 3);
    CHECK_EQ(Selector::OpeningPage(full, 3), 1);
    CHECK_EQ(Selector::OpeningPage(full, 0), 0);
    const Selector::Board fullGrid = Selector::BuildLevels(rules, locking, gold, table, 2);
    CHECK_EQ(fullGrid.highlighted, 31);
    CHECK_MSG(Selector::OpeningPage(fullGrid, 0) == 1, "the last unlocked level is on page 2: it opens there");
    CHECK(Selector::ChapterMedal(rules, 100) == "medal_gold_l.png");
    CHECK(Selector::ChapterMedal(rules, 50) == "medal_silver_l.png");
    CHECK(Selector::ChapterMedal(rules, 74) == "medal_silver_l.png");
    CHECK(Selector::LevelMedal(rules, Scores::kGold) == "medal_gold_m.png");
    CHECK(Selector::LevelMedal(rules, Scores::kSilver) == "medal_silver_m.png");
    CHECK(Selector::LevelMedal(rules, Scores::kBronze) == "medal_bronze_m.png");
}

void ChapterSelectSitsWhereTheStillsPutIt() {
    const Selector::Rules rules = Load();
    const Chapters::Table table = FourChapters();
    const Selector::Board bronze = Selector::BuildChapters(rules, LoadLocking(), SaveAll(Scores::kBronze), table);
    Selector::State state;
    const std::vector<Selector::Piece> pieces =
        Selector::Pieces(rules, bronze, state, kView720, kSettledMs, 0.0, Selector::Control::None, -1);

    const Selector::Piece* background = Find(pieces, "entities/world_select_bg.png");
    CHECK_MSG(background != nullptr && &pieces.front() == background && AtPx(background->rect.min, 0.0, 0.0),
              "W0 first, at (0, 0) on page 1");
    const Selector::Piece* title = Find(pieces, "select_chapter.png");
    CHECK_MSG(title != nullptr && AtPx(title->rect.min, 460.0, 18.0), "W1 TL (460, 18): " + (title ? Px(title->rect.min) : ""));
    const Selector::Piece* icon0 = Find(pieces, "world_icon0.png");
    const Selector::Piece* icon1 = Find(pieces, "world_icon1.png");
    // Chapter 1 is the last open one on this save: I5 bounces it about its centre.
    CHECK_MSG(icon0 != nullptr && AtPx(icon0->rect.Centre(), 521.875, 360.0),
              "W2 col 0 (TL (403.75, 180) at rest) bouncing about its centre: " + (icon0 ? Px(icon0->rect.min) : ""));
    CHECK_MSG(AtPx(Selector::ItemRect(rules, bronze, 0, kView720).min, 403.75, 180.0), "and at rest at (403.75, 180)");
    CHECK_MSG(icon1 != nullptr && AtPx(icon1->rect.min, 640.0, 180.0), "W2 col 1 TL (640, 180)");
    CHECK_MSG(Count(pieces, "world_icon2.png") == 0, "page 2's icons are not drawn at rest");
    const Selector::Piece* lock = Find(pieces, "lock_icon.png");
    CHECK_MSG(lock != nullptr && Count(pieces, "lock_icon.png") == 1 && AtPx(lock->rect.min, 668.125, 270.0),
              "W3 on chapter 2 only, TL (668.125, 270)");
    const Selector::Piece* medal = Find(pieces, "medal_bronze_l.png");
    CHECK_MSG(medal != nullptr && AtPx(medal->rect.min, 431.875, 444.375), "W4 TL (431.875, 444.375)");
    const Selector::Piece* percent = Find(pieces, "33%");
    CHECK_MSG(percent != nullptr && percent->centred && AtPx(percent->at, 538.75, 523.125) &&
                  Near(percent->unitsPerFontPx, 0.5, 1e-12),
              "W5 centred at (538.75, 523.125)");
    const Selector::Piece* warning = Find(pieces, "requires 60%");
    CHECK_MSG(warning != nullptr && AtPx(warning->at, 533.125, 573.75) && Near(warning->unitsPerFontPx, 0.25, 1e-12),
              "W6 centred at (533.125, 573.75)");
    // I9: (t mod 600) / 600 on the wall clock.
    const std::vector<Selector::Piece> at300 =
        Selector::Pieces(rules, bronze, state, kView720, kSettledMs, 300.0, Selector::Control::None, -1);
    const Selector::Piece* half = Find(at300, "requires 60%");
    CHECK_MSG(half != nullptr && half->alphaByte == 127, "W6 at half its sawtooth 300 ms in");

    // W7 / W8: bouncing about their origin, which is their anchor.
    const Selector::Piece* back = Find(pieces, "level_select_forward.png");
    const Selector::Piece* forward = Find(pieces, "level_select_back.png");
    CHECK_MSG(back != nullptr && AtPx(back->rect.min + back->rect.size * rules.chapters.back.origin, 64.0, 360.0),
              "W7 about its anchor (64, 360)");
    CHECK_MSG(forward != nullptr &&
                  AtPx(forward->rect.min + forward->rect.size * rules.chapters.forward.origin, 1216.0, 360.0),
              "W8 about its anchor (1216, 360)");
    // W9: the current page's digit and a dot.
    const Selector::Piece* slot0 = Find(pieces, "page_numbers.png", 0);
    const Selector::Piece* slot1 = Find(pieces, "page_numbers.png", 1);
    CHECK_MSG(slot0 != nullptr && AtPx(slot0->rect.min, 595.0, 648.0) && slot0->uvMin == glm::dvec2(0.0) &&
                  slot0->uvMax == glm::dvec2(0.25) && slot0->alphaByte == 220 && slot0->rgbBytes == glm::ivec3(0),
              "W9 slot 1 at (595, 648): the digit 1, ARGB(220,0,0,0)");
    CHECK_MSG(slot1 != nullptr && AtPx(slot1->rect.min, 640.0, 648.0) && slot1->uvMin == glm::dvec2(0.75),
              "W9 slot 2 at (640, 648): the dot, frame 15");
    CHECK_MSG(&pieces.back() == slot1, "W9 last");
}

void TheGridSitsWhereTheStillsPutIt() {
    const Selector::Rules rules = Load();
    const Chapters::Table table = FourChapters();
    const Selector::Board gold = Selector::BuildLevels(rules, LoadLocking(), SaveAll(Scores::kGold), table, 1);
    for (int item : {0, 5, 15}) {
        const Hud::Rect rect = Selector::ItemRect(rules, gold, item, kView720);
        const int c = item % 4;
        const int r = item / 4;
        CHECK_MSG(AtPx(rect.min, 280.0 + 180.0 * c, 180.0 * r), "L3 TL (280 + 180c, 180r): " + Px(rect.min));
    }
    Selector::State page2;
    page2.page = 1;
    page2.globalOffset = 1.0;
    const std::vector<Selector::Piece> pieces =
        Selector::Pieces(rules, gold, page2, kView720, kSettledMs, 0.0, Selector::Control::None, -1);
    CHECK_MSG(AtPx(pieces.front().rect.min, -160.0, 0.0), "L0 on page 2 at -160 px");
    const Selector::Piece* chapter = Find(pieces, "2");
    CHECK_MSG(chapter != nullptr && !chapter->centred && AtPx(chapter->at, -14.0625, -126.5625) &&
                  chapter->rgbBytes == glm::ivec3(33, 22, 11) && chapter->alphaByte == 155,
              "L1: chapter 2's numeral from (-14.06, -126.56), ARGB(155,33,22,11)");
    const Selector::Piece* percent = Find(pieces, "100%");
    CHECK_MSG(percent != nullptr && AtPx(percent->at, 0.0, -5.625) && Near(percent->unitsPerFontPx, 0.225, 1e-12),
              "L2 from (0, -5.625)");
    const Selector::Piece* boss = Find(pieces, "boss_level_button.png");
    CHECK_MSG(boss != nullptr && AtPx(boss->rect.min, 820.0, 540.0), "L5 at (820, 540) on page 2");
    const Selector::Piece* number = Find(pieces, "32");
    CHECK_MSG(number != nullptr && number->centred && AtPx(number->at, 910.0, 630.0), "L4 32 centred on its tile");
    CHECK_EQ(Count(pieces, "medal_gold_m.png"), 16);
    const Selector::Piece* medal = Find(pieces, "medal_gold_m.png");
    CHECK_MSG(medal != nullptr && AtPx(medal->rect.min, 381.25, 101.25), "L6 TL (381.25, 101.25) on tile 17");
    const Selector::Piece* back = Find(pieces, "level_select_forward.png");
    CHECK_MSG(back != nullptr && AtPx(back->rect.min + back->rect.size * rules.levels.back.origin, 0.0, 360.0),
              "L7 about (0, 360)");
    const Selector::Piece* forward = Find(pieces, "level_select_back.png");
    CHECK_MSG(forward != nullptr && AtPx(forward->rect.min + forward->rect.size * rules.levels.forward.origin, 1280.0, 360.0),
              "L8 about (1280, 360)");

    // Fresh: locked tiles carry no number.
    const Selector::Board fresh = Selector::BuildLevels(rules, LoadLocking(), SaveAll(0), table, 0);
    const std::vector<Selector::Piece> first =
        Selector::Pieces(rules, fresh, Selector::State{}, kView720, kSettledMs, 0.0, Selector::Control::None, -1);
    CHECK_EQ(Count(first, "level_button.png"), 1);
    CHECK_EQ(Count(first, "level_locked_button.png"), 15);
    CHECK_MSG(Find(first, "1") != nullptr && Find(first, "2") == nullptr, "L4 on the open tile only");
}

void ThePagerSlidesAsFilmed() {
    const Selector::Rules rules = Load();
    const Selector::Board gold = Selector::BuildLevels(rules, LoadLocking(), SaveAll(Scores::kGold), FourChapters(), 0);
    // The grid opening on page 2: setCurrentPage from page 0, sliding in from the right.
    Selector::State state = Selector::Open(gold, 1);
    CHECK_MSG(state.page == 1 && state.offset == 1.0, "opens at +1 screen width");
    bool model = true;
    for (int k = 0; k < 30; ++k) {
        Selector::Step(rules, gold, state);
        const double want = 0.92 * std::pow(0.8464, k);
        const double old = std::pow(0.8464, k + 1);
        model = model && Near(state.currentOffset, want, 1e-9) && Near(state.neighbourOffset, old, 1e-9) &&
                state.neighbour == 0;
    }
    CHECK_MSG(model, "new page 0.92 x 0.8464^k, old page 0.8464^(k+1) (0.5)");
    int settled = 30;
    while (state.offset != 0.0 && settled < 60) {
        Selector::Step(rules, gold, state);
        ++settled;
    }
    CHECK_MSG(settled >= 44 && settled <= 47, "snaps to rest after about 46 ticks: " + std::to_string(settled));
    CHECK_MSG(Near(Selector::CameraX(rules, state, kView720.x), 512.0 - kView720.x, 1e-9), "and pans 160 px");

    // An arrow on page 2: back to page 1, drawn from the release tick.
    Selector::SetPage(state, 0);
    Selector::Step(rules, gold, state);
    CHECK_MSG(Near(state.currentOffset, -0.92, 1e-9) && state.neighbour == 1 &&
                  Near(state.neighbourOffset + 1.0, 1.0 - 0.8464, 1e-9),
              "prev: page 1 from the left, page 2 leaving to the right");
}

void TheForwardArrowFadesByTheByte() {
    const Selector::Rules rules = Load();
    std::vector<int> out;
    int alpha = 255;
    int zeroAt = 0;
    for (int frame = 1; frame <= 100 && zeroAt == 0; ++frame) {
        alpha = Selector::FadeForward(rules, alpha, true);
        if (out.size() < 5) out.push_back(alpha);
        if (alpha == 0) zeroAt = frame;
    }
    CHECK_MSG(out == std::vector<int>({244, 234, 224, 215, 206}), "out 255, 244, 234, 224, 215, 206");
    CHECK_MSG(zeroAt == 72, "0 on the 72nd frame: " + std::to_string(zeroAt));
    alpha = 0;
    int fullAt = 0;
    for (int frame = 1; frame <= 40 && fullAt == 0; ++frame) {
        alpha = Selector::FadeForward(rules, alpha, false);
        if (frame == 1) CHECK_EQ(alpha, 12);
        if (alpha == 255) fullAt = frame;
    }
    CHECK_MSG(fullAt == 22, "in +12 a frame, 255 on the 22nd: " + std::to_string(fullAt));
}

void TheFingerSwapsPagesPastAFifth() {
    const Selector::Rules rules = Load();
    const Selector::Board board = Selector::BuildLevels(rules, LoadLocking(), SaveAll(Scores::kGold), FourChapters(), 0);
    const double w = kView720.x;

    Selector::State state;
    Selector::TouchDown(state, 300.0);
    Selector::TouchMove(state, 300.0 - 0.087 * w, w);
    Selector::Step(rules, board, state);
    CHECK_MSG(Near(state.currentOffset, -0.087, 1e-9), "held: the page follows the finger, no decay");
    Selector::TouchUp(rules, board, state);
    CHECK_MSG(state.page == 0 && Near(state.offset, -0.087, 1e-9), "a short drag snaps back");
    Selector::Step(rules, board, state);
    Selector::Step(rules, board, state);
    CHECK_MSG(Near(state.offset, -0.087 * 0.8464 * 0.8464, 1e-9), "at 0.8464 a tick");

    state = Selector::State{};
    Selector::TouchDown(state, 400.0);
    Selector::TouchMove(state, 400.0 - 0.48 * w, w);
    Selector::TouchUp(rules, board, state);
    CHECK_MSG(state.page == 1 && Near(state.offset, 0.52, 1e-9), "a long drag: page 2, at +0.52");

    // Past the first page: a third of the drag, one decay a tick.
    state = Selector::State{};
    Selector::TouchDown(state, 100.0);
    Selector::TouchMove(state, 100.0 + 0.39 * w, w);
    Selector::Step(rules, board, state);
    CHECK_MSG(state.neighbour == -1 && Near(state.currentOffset, 0.13, 1e-9), "rubber band: drawn at a third");
    Selector::TouchUp(rules, board, state);
    CHECK_EQ(state.page, 0);
    Selector::Step(rules, board, state);
    CHECK_MSG(Near(state.offset, 0.39 * 0.92, 1e-9), "and one x0.92 a tick back");
}

void ATouchIsOnWhatIsDrawn() {
    const Selector::Rules rules = Load();
    const Selector::Board board = Selector::BuildLevels(rules, LoadLocking(), SaveAll(0), FourChapters(), 0);
    Selector::State state;
    const glm::dvec2 tile5 = Selector::ItemRect(rules, board, 5, kView720).Centre();
    Selector::Hit hit = Selector::HitAt(rules, board, state, kView720, kSettledMs, tile5);
    CHECK_MSG(hit.control == Selector::Control::Item && hit.item == 5, "a locked tile is still hit: the layer refuses it");
    hit = Selector::HitAt(rules, board, state, kView720, kSettledMs, glm::dvec2(1250.0, 360.0) / kPxPerUnit);
    CHECK_MSG(hit.control == Selector::Control::Forward, "L8 on page 1");
    hit = Selector::HitAt(rules, board, state, kView720, kSettledMs, glm::dvec2(30.0, 360.0) / kPxPerUnit);
    CHECK_MSG(hit.control == Selector::Control::Back, "L7");
    state.page = 1;
    hit = Selector::HitAt(rules, board, state, kView720, kSettledMs, glm::dvec2(1250.0, 360.0) / kPxPerUnit);
    CHECK_MSG(hit.control == Selector::Control::None, "no forward on the last page");
    hit = Selector::HitAt(rules, board, state, kView720, kSettledMs, tile5);
    CHECK_MSG(hit.control == Selector::Control::Item && hit.item == 21, "page 2's tile in the same place");
}

void runTests() {
    TheSelectorFileSaysWhatWasDecoded();
    TheSaveOpensAsTheOriginal();
    ChapterSelectSitsWhereTheStillsPutIt();
    TheGridSitsWhereTheStillsPutIt();
    ThePagerSlidesAsFilmed();
    TheForwardArrowFadesByTheByte();
    TheFingerSwapsPagesPastAFifth();
    ATouchIsOnWhatIsDrawn();
}

} // namespace

TEST_MAIN("test_mp_select", 100)
