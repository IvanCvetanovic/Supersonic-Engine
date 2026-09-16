#include "sim/Selector.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MagicPortals::Selector {

namespace Json = Supersonic::Json;
using UiLayer::Read::Byte;
using UiLayer::Read::Fraction;
using UiLayer::Read::Pair;
using UiLayer::Read::Positive;
using UiLayer::Read::ReadPlaced;
using UiLayer::Read::Size;
using UiLayer::Read::Text;

namespace {

bool Factor(const Json::Value& block, const char* key, double& out, std::string& why, const std::string& where) {
    const Json::Value& value = block[key];
    if (!value.IsNumber() || !(value.AsNumber() > 0.0) || value.AsNumber() > 1.0) {
        why = where + "." + key + " is missing or not within (0, 1]";
        return false;
    }
    out = value.AsNumber();
    return true;
}

bool Count(const Json::Value& block, const char* key, int& out, std::string& why, const std::string& where) {
    if (!Byte(block, key, out, why, where)) return false;
    if (out < 1) {
        why = where + "." + key + " is below 1";
        return false;
    }
    return true;
}

bool ReadText(const Json::Value& block, const char* atKey, Rules::Text& out, std::string& why,
              const std::string& where) {
    return Text(block, "font", out.font, why, where) &&
           Positive(block, "units_per_font_px", out.unitsPerFontPx, why, where) &&
           Pair(block, atKey, out.atUnits, why, where);
}

bool ReadBounce(const Json::Value& block, MenuState::Bounce& out, std::string& why, const std::string& where) {
    return Size(block, "scale_a", out.scaleA, why, where) && Size(block, "scale_b", out.scaleB, why, where) &&
           Positive(block, "stride_ms", out.strideMs, why, where);
}

bool ReadMedals(const Json::Value& block, std::string& gold, std::string& silver, std::string& bronze,
                glm::dvec2& size, std::string& why, const std::string& where) {
    return Text(block, "gold", gold, why, where) && Text(block, "silver", silver, why, where) &&
           Text(block, "bronze", bronze, why, where) && Size(block, "size_units", size, why, where);
}

int WorldsIn(const Chapters::Table& table) {
    int worlds = 0;
    for (const Chapters::Level& level : table.levels) worlds = std::max(worlds, level.world + 1);
    return worlds;
}

std::string Percent(int percent) {
    return std::to_string(percent) + "%";
}

// Where item `item` stands with its page drawn `shift` screen widths across.
Hud::Rect Cell(const Rules& rules, const Board& board, int item, const glm::dvec2& view, double shift) {
    const int slot = item % board.perPage;
    if (!board.levels) {
        const Rules::Chapters& c = rules.chapters;
        const double left = (view.x - c.iconSizeUnits.x * c.perPage) * 0.5;
        // Page::Page's getScreenOffset: the block centred both ways, one row.
        return Hud::Rect{glm::dvec2(left + c.iconSizeUnits.x * slot + shift * view.x,
                                    (view.y - c.iconSizeUnits.y) * 0.5),
                         c.iconSizeUnits};
    }
    const Rules::Levels& l = rules.levels;
    const glm::dvec2 block(l.tileSizeUnits.x * l.columns, l.tileSizeUnits.y * l.rows);
    const glm::dvec2 origin = (view - block) * 0.5;
    const int column = slot % l.columns;
    const int row = slot / l.columns;
    return Hud::Rect{glm::dvec2(origin.x + l.tileSizeUnits.x * column + shift * view.x,
                                origin.y + l.tileSizeUnits.y * row),
                     l.tileSizeUnits};
}

const UiLayer::Placed& BackOf(const Rules& rules, const Board& board) {
    return board.levels ? rules.levels.back : rules.chapters.back;
}

const UiLayer::Placed& ForwardOf(const Rules& rules, const Board& board) {
    return board.levels ? rules.levels.forward : rules.chapters.forward;
}

// A layer button where its entrance has it `stateMs` in: the rectangle a touch is
// tested against, with no bounce.
Hud::Rect Entering(const Rules& rules, const UiLayer::Placed& placed, const glm::dvec2& view, double stateMs) {
    return UiLayer::RectAt(placed, UiLayer::ButtonAnchorAt(rules.layer, UiLayer::Anchor(placed, view), view, stateMs));
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    Rules read;
    if (!UiLayer::LoadRules(path, read.layer, error) || !MenuState::LoadRules(path, read.state, error)) return false;
    Json::Value root;
    if (!UiLayer::Read::File(path, root, error)) return false;
    const Json::Value& s = root["selector"];
    if (!s.IsObject() || !s["pager"].IsObject() || !s["background"].IsObject() || !s["arrow_bounce"].IsObject() ||
        !s["chapter_select"].IsObject() || !s["level_grid"].IsObject()) {
        error = path + ": selector, and its pager, background, arrow_bounce, chapter_select and level_grid, are "
                       "each an object";
        return false;
    }
    std::string why;
    const Json::Value& pager = s["pager"];
    const Json::Value& c = s["chapter_select"];
    const Json::Value& l = s["level_grid"];
    Rules::Chapters& chapters = read.chapters;
    Rules::Levels& levels = read.levels;
    const bool ok =
        Factor(pager, "decay_per_call", read.pager.decayPerCall, why, "selector.pager") &&
        Positive(pager, "snap_below", read.pager.snapBelow, why, "selector.pager") &&
        Factor(pager, "swap_offset", read.pager.swapOffset, why, "selector.pager") &&
        Factor(pager, "rubber_band", read.pager.rubberBand, why, "selector.pager") &&
        Factor(pager, "arrow_fade_out_factor", read.pager.arrowFadeOutFactor, why, "selector.pager") &&
        Factor(pager, "arrow_fade_in_step", read.pager.arrowFadeInStep, why, "selector.pager") &&
        Text(s["background"], "sprite", read.background.sprite, why, "selector.background") &&
        Size(s["background"], "size_units", read.background.sizeUnits, why, "selector.background") &&
        ReadBounce(s["arrow_bounce"], read.arrowBounce, why, "selector.arrow_bounce") &&
        // Chapter select.
        ReadPlaced(c["title"], "sprite", chapters.title, why, "selector.chapter_select.title") &&
        ReadPlaced(c["back"], "sprite", chapters.back, why, "selector.chapter_select.back") &&
        ReadPlaced(c["forward"], "sprite", chapters.forward, why, "selector.chapter_select.forward") &&
        Text(c, "icon_prefix", chapters.iconPrefix, why, "selector.chapter_select") &&
        Size(c, "icon_size_units", chapters.iconSizeUnits, why, "selector.chapter_select") &&
        Count(c, "per_page", chapters.perPage, why, "selector.chapter_select") &&
        c["lock"].IsObject() && Text(c["lock"], "sprite", chapters.lockSprite, why, "selector.chapter_select.lock") &&
        Size(c["lock"], "size_units", chapters.lockSizeUnits, why, "selector.chapter_select.lock") &&
        c["medal"].IsObject() &&
        ReadMedals(c["medal"], chapters.medalGold, chapters.medalSilver, chapters.medalBronze,
                   chapters.medalSizeUnits, why, "selector.chapter_select.medal") &&
        Pair(c["medal"], "centre_units", chapters.medalCentreUnits, why, "selector.chapter_select.medal") &&
        Count(c["medal"], "gold_percent", chapters.medalGoldPercent, why, "selector.chapter_select.medal") &&
        Count(c["medal"], "bronze_below_percent", chapters.medalBronzeBelowPercent, why,
              "selector.chapter_select.medal") &&
        c["percent"].IsObject() &&
        ReadText(c["percent"], "centre_units", chapters.percent, why, "selector.chapter_select.percent") &&
        c["warning"].IsObject() &&
        ReadText(c["warning"], "centre_units", chapters.warning, why, "selector.chapter_select.warning") &&
        Text(c["warning"], "words", chapters.warningWords, why, "selector.chapter_select.warning") &&
        Positive(c["warning"], "sawtooth_ms", chapters.warningSawtoothMs, why, "selector.chapter_select.warning") &&
        c["counter"].IsObject() &&
        Text(c["counter"], "sprite", chapters.counter.sprite, why, "selector.chapter_select.counter") &&
        Count(c["counter"], "columns", chapters.counter.columns, why, "selector.chapter_select.counter") &&
        Count(c["counter"], "rows", chapters.counter.rows, why, "selector.chapter_select.counter") &&
        Size(c["counter"], "slot_units", chapters.counter.slotUnits, why, "selector.chapter_select.counter") &&
        Fraction(c["counter"], "at_screen", chapters.counter.atScreen, why, "selector.chapter_select.counter") &&
        Byte(c["counter"], "tint_alpha_byte", chapters.counter.tintAlphaByte, why, "selector.chapter_select.counter") &&
        Byte(c["counter"], "dot_frame", chapters.counter.dotFrame, why, "selector.chapter_select.counter") &&
        c["highlight"].IsObject() && ReadBounce(c["highlight"], chapters.highlight, why, "selector.chapter_select.highlight") &&
        // The level grid.
        ReadPlaced(l["back"], "sprite", levels.back, why, "selector.level_grid.back") &&
        ReadPlaced(l["forward"], "sprite", levels.forward, why, "selector.level_grid.forward") &&
        Text(l, "tile", levels.tileSprite, why, "selector.level_grid") &&
        Text(l, "locked_tile", levels.lockedTileSprite, why, "selector.level_grid") &&
        Text(l, "boss_tile", levels.bossSprite, why, "selector.level_grid") &&
        Size(l, "tile_size_units", levels.tileSizeUnits, why, "selector.level_grid") &&
        Count(l, "columns", levels.columns, why, "selector.level_grid") &&
        Count(l, "rows", levels.rows, why, "selector.level_grid") && l["number"].IsObject() &&
        Text(l["number"], "font", levels.number.font, why, "selector.level_grid.number") &&
        Positive(l["number"], "units_per_font_px", levels.number.unitsPerFontPx, why, "selector.level_grid.number") &&
        l["medal"].IsObject() &&
        ReadMedals(l["medal"], levels.medalGold, levels.medalSilver, levels.medalBronze, levels.medalSizeUnits, why,
                   "selector.level_grid.medal") &&
        Pair(l["medal"], "at_units", levels.medalAtUnits, why, "selector.level_grid.medal") &&
        l["corner_chapter"].IsObject() &&
        ReadText(l["corner_chapter"], "at_units", levels.cornerChapter, why, "selector.level_grid.corner_chapter") &&
        l["corner_percent"].IsObject() &&
        ReadText(l["corner_percent"], "at_units", levels.cornerPercent, why, "selector.level_grid.corner_percent") &&
        l["highlight"].IsObject() && ReadBounce(l["highlight"], levels.highlight, why, "selector.level_grid.highlight");
    if (!ok) {
        error = path + ": " + (why.empty() ? std::string("selector is missing an object it needs") : why);
        return false;
    }
    const Json::Value& argb = l["corner_argb"];
    if (!argb.IsArray() || argb.AsArray().size() != 4) {
        error = path + ": selector.level_grid.corner_argb is not four bytes";
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        const Json::Value& value = argb.AsArray()[static_cast<std::size_t>(i)];
        const double byte = value.IsNumber() ? value.AsNumber() : -1.0;
        if (byte < 0.0 || byte > 255.0 || byte != std::floor(byte)) {
            error = path + ": selector.level_grid.corner_argb is not four bytes";
            return false;
        }
        levels.cornerArgb[i] = static_cast<int>(byte);
    }
    if (chapters.counter.dotFrame >= chapters.counter.columns * chapters.counter.rows) {
        error = path + ": selector.chapter_select.counter.dot_frame is past the sheet";
        return false;
    }
    out = std::move(read);
    return true;
}

Board BuildChapters(const Rules& rules, const Locking::Rules& locking, const Scores::Store& scores,
                    const Chapters::Table& table) {
    Board board;
    board.levels = false;
    board.perPage = rules.chapters.perPage;
    const int worlds = WorldsIn(table);
    for (int w = 0; w < worlds; ++w) {
        Chapter chapter;
        chapter.unlocked = Locking::ChapterUnlocked(locking, scores, table, w);
        chapter.completion = Locking::ChapterCompletion(locking, scores, table, w);
        const int last = Locking::LevelsIn(table, w) - 1;
        // WorldChooser's callback: the boss level scored and the chapter still short.
        chapter.warned = last >= 0 && scores.Get(w, last) > 0 && chapter.completion < locking.chapterUnlockPercent;
        if (chapter.unlocked) board.highlighted = w;
        board.chapters.push_back(chapter);
    }
    board.items = worlds;
    return board;
}

Board BuildLevels(const Rules& rules, const Locking::Rules& locking, const Scores::Store& scores,
                  const Chapters::Table& table, int world) {
    Board board;
    board.levels = true;
    board.world = world;
    board.perPage = rules.levels.columns * rules.levels.rows;
    board.completion = Locking::ChapterCompletion(locking, scores, table, world);
    for (std::size_t i = 0; i < table.levels.size(); ++i) {
        const Chapters::Level& level = table.levels[i];
        if (level.world != world) continue;
        Tile tile;
        tile.unlocked = Locking::LevelUnlocked(locking, scores, table, world, level.index);
        tile.medal = scores.Get(world, level.index);
        tile.level = static_cast<int>(i);
        board.tiles.push_back(tile);
    }
    if (!board.tiles.empty()) board.tiles.back().boss = true;
    board.items = static_cast<int>(board.tiles.size());
    // findLastUnlockedLevel: the first locked index minus one, or the last.
    board.highlighted = board.items - 1;
    for (int i = 0; i < board.items; ++i) {
        if (!board.tiles[static_cast<std::size_t>(i)].unlocked) {
            board.highlighted = i - 1;
            break;
        }
    }
    return board;
}

int OpeningPage(const Board& board, int world) {
    const int item = board.levels ? std::max(0, board.highlighted) : std::clamp(world, 0, std::max(0, board.items - 1));
    return std::clamp(item / std::max(1, board.perPage), 0, board.Pages() - 1);
}

void SetPage(State& state, int page) {
    state.offset = static_cast<double>(page - state.page);
    state.page = page;
}

State Open(const Board& board, int page) {
    State state;
    SetPage(state, std::clamp(page, 0, board.Pages() - 1));
    return state;
}

void Step(const Rules& rules, const Board& board, State& state) {
    const int pages = board.Pages();
    const int toward = state.offset > 0.0 ? state.page - 1 : (state.offset < 0.0 ? state.page + 1 : -1);
    const bool neighbour = toward >= 0 && toward < pages;
    const auto decay = [&rules, &state]() {
        state.offset *= rules.pager.decayPerCall;
        if (std::fabs(state.offset) < rules.pager.snapBelow) state.offset = 0.0;
    };
    if (neighbour) {
        if (!state.touching) decay();
        state.currentOffset = state.offset;
        if (!state.touching) decay();
        state.neighbourOffset = state.offset;
        state.neighbour = toward;
        state.rubberBand = false;
    } else {
        // Past the first or the last page: only the current one, a third of the way.
        if (!state.touching) decay();
        state.currentOffset = state.offset * rules.pager.rubberBand;
        state.neighbourOffset = state.currentOffset;
        state.neighbour = -1;
        state.rubberBand = state.offset != 0.0;
    }
    // getGlobalOffset, with m_offset after the frame's calls.
    state.globalOffset = 2.0 * (static_cast<double>(state.page) / pages - state.offset / pages);
    state.forwardAlphaByte = FadeForward(rules, state.forwardAlphaByte, state.page == pages - 1);
}

void TouchDown(State& state, double xUnits) {
    state.touching = true;
    state.touchX = xUnits;
    state.offset = 0.0;
}

void TouchMove(State& state, double xUnits, double viewWidthUnits) {
    if (!state.touching || !(viewWidthUnits > 0.0)) return;
    state.offset = (xUnits - state.touchX) / viewWidthUnits;
}

void TouchUp(const Rules& rules, const Board& board, State& state) {
    if (!state.touching) return;
    state.touching = false;
    if (std::fabs(state.offset) <= rules.pager.swapOffset) return;
    const int target = state.offset > 0.0 ? state.page - 1 : state.page + 1;
    if (target < 0 || target >= board.Pages()) return;
    state.page = target;
    state.offset -= state.offset > 0.0 ? 1.0 : -1.0;
}

double CameraX(const Rules& rules, const State& state, double viewWidthUnits) {
    return std::clamp(state.globalOffset, 0.0, 1.0) * std::max(0.0, rules.background.sizeUnits.x - viewWidthUnits);
}

int FadeForward(const Rules& rules, int alphaByte, bool lastPage) {
    // In float, as FloatColor holds it, and back to a byte by truncation.
    float alpha = static_cast<float>(alphaByte) / 255.0f;
    alpha = lastPage ? alpha * static_cast<float>(rules.pager.arrowFadeOutFactor)
                     : std::min(1.0f, alpha + static_cast<float>(rules.pager.arrowFadeInStep));
    return std::clamp(static_cast<int>(alpha * 255.0f), 0, 255);
}

std::string ChapterMedal(const Rules& rules, int completion) {
    if (completion >= rules.chapters.medalGoldPercent) return rules.chapters.medalGold;
    if (completion < rules.chapters.medalBronzeBelowPercent) return rules.chapters.medalBronze;
    return rules.chapters.medalSilver;
}

std::string LevelMedal(const Rules& rules, int medal) {
    if (medal >= Scores::kGold) return rules.levels.medalGold;
    if (medal == Scores::kSilver) return rules.levels.medalSilver;
    return rules.levels.medalBronze;
}

Hud::Rect ItemRect(const Rules& rules, const Board& board, int item, const glm::dvec2& viewUnits) {
    return Cell(rules, board, item, viewUnits, 0.0);
}

std::vector<Piece> Pieces(const Rules& rules, const Board& board, const State& state, const glm::dvec2& viewUnits,
                          double stateMs, double wallMs, Control held, int heldItem) {
    std::vector<Piece> out;
    const glm::dvec2& view = viewUnits;
    const int pressByte = rules.state.pressTintByte;
    const auto sprite = [&out](const std::string& file, const Hud::Rect& rect, int rgbByte, int alphaByte) {
        Piece piece;
        piece.kind = Kind::Sprite;
        piece.file = file;
        piece.rect = rect;
        piece.rgbBytes = glm::ivec3(rgbByte);
        piece.alphaByte = alphaByte;
        out.push_back(std::move(piece));
    };
    const auto text = [&out](const Rules::Text& how, const std::string& words, const glm::dvec2& at, bool centred,
                             const glm::ivec3& rgb, int alphaByte) {
        Piece piece;
        piece.kind = Kind::Text;
        piece.file = how.font;
        piece.words = words;
        piece.at = at;
        piece.centred = centred;
        piece.unitsPerFontPx = how.unitsPerFontPx;
        piece.rgbBytes = rgb;
        piece.alphaByte = alphaByte;
        out.push_back(std::move(piece));
    };

    // The scene's background, panned by the camera.
    sprite(rules.background.sprite,
           Hud::Rect{glm::dvec2(-CameraX(rules, state, view.x), 0.0), rules.background.sizeUnits}, 255, 255);

    // The grid's corner words, drawn in loop before the layers.
    if (board.levels) {
        const Rules::Levels& l = rules.levels;
        const glm::ivec3 rgb(l.cornerArgb[1], l.cornerArgb[2], l.cornerArgb[3]);
        text(l.cornerChapter, std::to_string(board.world + 1), l.cornerChapter.atUnits, false, rgb, l.cornerArgb[0]);
        text(l.cornerPercent, Percent(board.completion), l.cornerPercent.atUnits, false, rgb, l.cornerArgb[0]);
    }

    // The layer: chapter select's title, then the two arrows, entering and bouncing.
    const int entering = UiLayer::ButtonAlphaByte(rules.layer, stateMs);
    if (!board.levels) sprite(rules.chapters.title.sprite, Entering(rules, rules.chapters.title, view, stateMs), 255, entering);
    const glm::dvec2 bounce = MenuState::BounceScale(rules.arrowBounce, stateMs);
    const UiLayer::Placed& back = BackOf(rules, board);
    const UiLayer::Placed& forward = ForwardOf(rules, board);
    sprite(back.sprite, MenuState::ScaledAbout(Entering(rules, back, view, stateMs), back.origin, bounce),
           held == Control::Back ? pressByte : 255, entering);
    sprite(forward.sprite, MenuState::ScaledAbout(Entering(rules, forward, view, stateMs), forward.origin, bounce),
           held == Control::Forward ? pressByte : 255, entering * state.forwardAlphaByte / 255);

    // Each drawn page's tiles.
    const auto page = [&](int index, double shift) {
        for (int slot = 0; slot < board.perPage; ++slot) {
            const int item = index * board.perPage + slot;
            if (item >= board.items) break;
            const Hud::Rect cell = Cell(rules, board, item, view, shift);
            const int tint = held == Control::Item && heldItem == item ? pressByte : 255;
            if (!board.levels) {
                const Rules::Chapters& c = rules.chapters;
                const Chapter& chapter = board.chapters[static_cast<std::size_t>(item)];
                const glm::dvec2 scale = item == board.highlighted ? MenuState::BounceScale(c.highlight, stateMs)
                                                                   : glm::dvec2(1.0);
                sprite(c.iconPrefix + std::to_string(item) + ".png",
                       MenuState::ScaledAbout(cell, glm::dvec2(0.5), scale), tint, 255);
                if (!chapter.unlocked) {
                    sprite(c.lockSprite, Hud::Rect{cell.Centre() - c.lockSizeUnits * 0.5, c.lockSizeUnits}, 255, 255);
                }
                if (chapter.completion > 0) {
                    sprite(ChapterMedal(rules, chapter.completion),
                           Hud::Rect{cell.min + c.medalCentreUnits - c.medalSizeUnits * 0.5, c.medalSizeUnits}, 255,
                           255);
                    text(c.percent, Percent(chapter.completion), cell.min + c.percent.atUnits, true, glm::ivec3(255),
                         255);
                }
                if (chapter.warned) {
                    // computeAccomplishmentWarningBlinkColor: a rising sawtooth on the wall clock.
                    const double phase = std::fmod(std::max(0.0, wallMs), c.warningSawtoothMs) / c.warningSawtoothMs;
                    text(c.warning, c.warningWords, cell.min + c.warning.atUnits, true, glm::ivec3(255),
                         static_cast<int>(phase * 255.0));
                }
                continue;
            }
            const Rules::Levels& l = rules.levels;
            const Tile& tile = board.tiles[static_cast<std::size_t>(item)];
            const glm::dvec2 scale =
                item == board.highlighted ? MenuState::BounceScale(l.highlight, stateMs) : glm::dvec2(1.0);
            sprite(tile.unlocked ? l.tileSprite : l.lockedTileSprite, MenuState::ScaledAbout(cell, glm::dvec2(0.5), scale),
                   tint, 255);
            if (tile.unlocked) {
                text(l.number, std::to_string(item + 1), cell.Centre(), true, glm::ivec3(255), 255);
            }
            if (tile.boss) sprite(l.bossSprite, cell, 255, 255);
            if (tile.medal > 0) {
                sprite(LevelMedal(rules, tile.medal), Hud::Rect{cell.min + l.medalAtUnits, l.medalSizeUnits}, 255, 255);
            }
        }
    };
    page(state.page, state.currentOffset);
    if (state.neighbour >= 0) {
        page(state.neighbour, state.neighbourOffset + (state.neighbour > state.page ? 1.0 : -1.0));
    }

    // Chapter select's page counter: the current page's digit, a dot for each other.
    if (!board.levels) {
        const Rules::Chapters::Counter& k = rules.chapters.counter;
        const int pages = board.Pages();
        const glm::dvec2 row = view * k.atScreen - glm::dvec2(k.slotUnits.x * pages * 0.5, 0.0);
        for (int i = 0; i < pages; ++i) {
            const int frame = i == state.page ? i : k.dotFrame;
            Piece piece;
            piece.kind = Kind::Sprite;
            piece.file = k.sprite;
            piece.rect = Hud::Rect{row + glm::dvec2(k.slotUnits.x * i, 0.0), k.slotUnits};
            const glm::dvec2 cells(k.columns, k.rows);
            piece.uvMin = glm::dvec2(frame % k.columns, frame / k.columns) / cells;
            piece.uvMax = piece.uvMin + glm::dvec2(1.0) / cells;
            piece.rgbBytes = glm::ivec3(0);
            piece.alphaByte = k.tintAlphaByte;
            out.push_back(std::move(piece));
        }
    }
    return out;
}

Hit HitAt(const Rules& rules, const Board& board, const State& state, const glm::dvec2& viewUnits, double stateMs,
          const glm::dvec2& point) {
    Hit hit;
    for (int slot = 0; slot < board.perPage; ++slot) {
        const int item = state.page * board.perPage + slot;
        if (item >= board.items) break;
        if (Cell(rules, board, item, viewUnits, state.currentOffset).Contains(point)) {
            hit.control = Control::Item;
            hit.item = item;
            return hit;
        }
    }
    if (Entering(rules, BackOf(rules, board), viewUnits, stateMs).Contains(point)) {
        hit.control = Control::Back;
        return hit;
    }
    if (state.page < board.Pages() - 1 && Entering(rules, ForwardOf(rules, board), viewUnits, stateMs).Contains(point)) {
        hit.control = Control::Forward;
    }
    return hit;
}

} // namespace MagicPortals::Selector
