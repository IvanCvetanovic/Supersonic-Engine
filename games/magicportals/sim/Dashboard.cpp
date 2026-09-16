#include "sim/Dashboard.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MagicPortals::Dashboard {

namespace {

namespace Json = Supersonic::Json;
using UiLayer::Read::Byte;
using UiLayer::Read::Fraction;
using UiLayer::Read::Pair;
using UiLayer::Read::Positive;
using UiLayer::Read::Size;
using UiLayer::Read::Text;

bool Factor(const Json::Value& block, const char* key, double& out, std::string& why, const std::string& where) {
    const Json::Value& value = block[key];
    if (!value.IsNumber() || !(value.AsNumber() > 0.0) || value.AsNumber() > 1.0) {
        why = where + "." + key + " is missing or not within (0, 1]";
        return false;
    }
    out = value.AsNumber();
    return true;
}

bool Whole(const Json::Value& block, const char* key, int least, int& out, std::string& why,
           const std::string& where) {
    const Json::Value& value = block[key];
    if (!value.IsNumber() || value.AsNumber() != std::floor(value.AsNumber()) ||
        value.AsNumber() < static_cast<double>(least) || value.AsNumber() > 1.0e6) {
        why = where + "." + key + " is missing or not a whole number of at least " + std::to_string(least);
        return false;
    }
    out = static_cast<int>(value.AsNumber());
    return true;
}

bool ReadText(const Json::Value& block, const char* atKey, Rules::Text& out, std::string& why,
              const std::string& where) {
    return Text(block, "font", out.font, why, where) &&
           Positive(block, "units_per_font_px", out.unitsPerFontPx, why, where) &&
           Pair(block, atKey, out.atUnits, why, where);
}

int Alpha(bool unlocked, int lockedByte) {
    return unlocked ? 255 : lockedByte;
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    Rules read;
    if (!UiLayer::LoadRules(path, read.layer, error) || !MenuState::LoadRules(path, read.state, error)) return false;

    Json::Value root;
    if (!UiLayer::Read::File(path, root, error)) return false;
    const Json::Value& board = root["dashboard"];
    const char* const blocks[] = {"background", "back_button", "rows", "header", "title", "description", "points",
                                  "new_label", "plaque", "total", "scroll", "scroll_bar", "start_button"};
    bool shaped = board.IsObject();
    for (const char* block : blocks) shaped = shaped && board[block].IsObject();
    if (!shaped) {
        error = path + ": dashboard, and its background, back_button, rows, header, title, description, points, "
                       "new_label, plaque, total, scroll, scroll_bar and start_button, are each an object";
        return false;
    }
    std::string why;
    const auto fail = [&error, &path, &why]() {
        error = path + ": " + why;
        return false;
    };

    const Json::Value& background = board["background"];
    const Json::Value& back = board["back_button"];
    if (!Text(background, "sprite", read.background.sprite, why, "dashboard.background") ||
        !Size(background, "size_units", read.background.sizeUnits, why, "dashboard.background") ||
        !Fraction(background, "centre_of_screen", read.background.centreOfScreen, why, "dashboard.background") ||
        // Pairs, not fractions: BackButtonLayer's (-0.025, 0.5) is off the left edge.
        !Text(back, "sprite", read.back.sprite, why, "dashboard.back_button") ||
        !Pair(back, "at_screen", read.back.atScreen, why, "dashboard.back_button") ||
        !Pair(back, "origin", read.back.origin, why, "dashboard.back_button") ||
        !Size(back, "size_units", read.back.sizeUnits, why, "dashboard.back_button")) {
        return fail();
    }

    const Json::Value& rows = board["rows"];
    if (!Positive(rows, "tile_units", read.rows.tileUnits, why, "dashboard.rows") ||
        !Positive(rows, "line_factor", read.rows.lineFactor, why, "dashboard.rows") ||
        !Positive(rows, "column_factor", read.rows.columnFactor, why, "dashboard.rows") ||
        !Positive(rows, "bar_lines", read.rows.barLines, why, "dashboard.rows") ||
        !Text(rows, "icon_directory", read.rows.iconDirectory, why, "dashboard.rows") ||
        !Text(rows, "bar_sprite", read.rows.barSprite, why, "dashboard.rows") ||
        !Text(rows, "lock_sprite", read.rows.lockSprite, why, "dashboard.rows") ||
        !Byte(rows, "locked_icon_alpha_byte", read.rows.lockedIconAlphaByte, why, "dashboard.rows") ||
        !Byte(rows, "locked_bar_alpha_byte", read.rows.lockedBarAlphaByte, why, "dashboard.rows") ||
        !Byte(rows, "locked_text_alpha_byte", read.rows.lockedTextAlphaByte, why, "dashboard.rows") ||
        !Byte(rows, "locked_points_alpha_byte", read.rows.lockedPointsAlphaByte, why, "dashboard.rows")) {
        return fail();
    }

    const Json::Value& label = board["new_label"];
    if (!ReadText(board["header"], "centre_units", read.header, why, "dashboard.header") ||
        !Text(board["header"], "prefix", read.headerPrefix, why, "dashboard.header") ||
        !ReadText(board["title"], "offset_units", read.title, why, "dashboard.title") ||
        !ReadText(board["description"], "offset_units", read.description, why, "dashboard.description") ||
        !ReadText(board["points"], "centre_units", read.points, why, "dashboard.points") ||
        !ReadText(board["total"], "centre_units", read.total, why, "dashboard.total") ||
        !ReadText(label, "offset_units", read.newLabel.text, why, "dashboard.new_label") ||
        !Text(label, "text", read.newLabel.words, why, "dashboard.new_label") ||
        !Byte(label, "alpha_floor_byte", read.newLabel.alphaFloorByte, why, "dashboard.new_label") ||
        !Whole(label, "alpha_period_ms", 1, read.newLabel.alphaPeriodMs, why, "dashboard.new_label")) {
        return fail();
    }
    const Json::Value& rgb = label["rgb_bytes"];
    bool rgbRead = rgb.IsArray() && rgb.AsArray().size() == 3;
    for (std::size_t i = 0; rgbRead && i < 3; ++i) {
        const Json::Value& channel = rgb.AsArray()[i];
        rgbRead = channel.IsNumber() && channel.AsNumber() == std::floor(channel.AsNumber()) &&
                  channel.AsNumber() >= 0.0 && channel.AsNumber() <= 255.0;
        if (rgbRead) read.newLabel.rgbBytes[static_cast<glm::length_t>(i)] = static_cast<int>(channel.AsNumber());
    }
    if (!rgbRead) {
        why = "dashboard.new_label.rgb_bytes is not three bytes";
        return fail();
    }

    const Json::Value& plaque = board["plaque"];
    const Json::Value& scroll = board["scroll"];
    const Json::Value& bar = board["scroll_bar"];
    const Json::Value& start = board["start_button"];
    if (!Text(plaque, "sprite", read.plaque.sprite, why, "dashboard.plaque") ||
        !Pair(plaque, "at_units", read.plaque.atUnits, why, "dashboard.plaque") ||
        !Size(plaque, "size_units", read.plaque.sizeUnits, why, "dashboard.plaque") ||
        !Factor(scroll, "momentum_decay_per_tick", read.scroll.momentumDecayPerTick, why, "dashboard.scroll") ||
        !Factor(scroll, "top_band_per_tick", read.scroll.topBandPerTick, why, "dashboard.scroll") ||
        !Factor(scroll, "bottom_band_per_tick", read.scroll.bottomBandPerTick, why, "dashboard.scroll") ||
        !Positive(scroll, "wheel_units_per_notch", read.scroll.wheelUnitsPerNotch, why, "dashboard.scroll") ||
        !Text(bar, "sprite", read.bar.sprite, why, "dashboard.scroll_bar") ||
        !Size(bar, "size_units", read.bar.sizeUnits, why, "dashboard.scroll_bar") ||
        !Positive(bar, "from_right_units", read.bar.fromRightUnits, why, "dashboard.scroll_bar") ||
        !Byte(bar, "alpha_byte", read.bar.alphaByte, why, "dashboard.scroll_bar") ||
        !Text(start, "sprite", read.start.sprite, why, "dashboard.start_button") ||
        !Size(start, "size_units", read.start.sizeUnits, why, "dashboard.start_button") ||
        !Pair(start, "offset_units", read.start.offsetUnits, why, "dashboard.start_button") ||
        !Fraction(start, "origin", read.start.origin, why, "dashboard.start_button") ||
        !Positive(start, "dismiss_after_units", read.start.dismissAfterUnits, why, "dashboard.start_button")) {
        return fail();
    }
    out = std::move(read);
    return true;
}

Layout LayOut(const Rules& rules, const glm::dvec2& viewUnits) {
    Layout layout;
    layout.iconSize = glm::dvec2(rules.rows.tileUnits);
    layout.lineOffset = layout.iconSize.y * rules.rows.lineFactor;
    layout.columnAdvance = rules.rows.tileUnits * rules.rows.columnFactor;
    layout.barSize = glm::dvec2(std::min(layout.lineOffset * rules.rows.barLines,
                                         viewUnits.x - layout.columnAdvance * 2.0),
                                layout.lineOffset);
    return layout;
}

Board Build(const Achievements::Content* content, const Locking::Rules& locking, const Scores::Store& scores,
            const Chapters::Table& chapters) {
    Board board;
    for (const Chapters::Level& level : chapters.levels) board.worlds = std::max(board.worlds, level.world + 1);
    if (content == nullptr) return board;
    const std::vector<int> unlocked = Achievements::Unlocked(*content, locking, scores, chapters);
    board.points = Achievements::Points(*content, unlocked);
    board.achievements = static_cast<int>(content->entries.size());
    // The loop's currentWorld starts at -1, so the first achievement has a header too.
    int currentWorld = -1;
    for (std::size_t i = 0; i < content->entries.size(); ++i) {
        const Achievements::Entry& entry = content->entries[i];
        if (entry.world != currentWorld) {
            currentWorld = entry.world;
            Line header;
            header.header = true;
            header.world = entry.world;
            board.lines.push_back(std::move(header));
        }
        Line line;
        line.world = entry.world;
        line.level = entry.level;
        line.entry = static_cast<int>(i);
        line.unlocked = Achievements::IsUnlocked(unlocked, entry.id);
        line.levelUnlocked = Locking::LevelUnlocked(locking, scores, chapters, entry.world, entry.level);
        line.title = Achievements::TitleOf(*content, entry, line.unlocked);
        line.description = Achievements::DescriptionOf(*content, entry, line.unlocked);
        line.icon = Achievements::IconOf(*content, entry, line.unlocked);
        line.points = entry.points;
        line.isNew = entry.isNew;
        board.lines.push_back(std::move(line));
    }
    return board;
}

double Stride(const Rules& rules, const Board& board, const glm::dvec2& viewUnits) {
    return LayOut(rules, viewUnits).lineOffset * static_cast<double>(board.achievements + board.worlds);
}

double MinScroll(const Rules& rules, const Board& board, const glm::dvec2& viewUnits) {
    return viewUnits.y - Stride(rules, board, viewUnits);
}

void DoScrolling(const Rules& rules, State& state, double minScroll, bool moving, double move) {
    if (moving) {
        state.scroll += move;
        state.accumulated += move;
        state.moveSpeed = move;
        return;
    }
    // In this order: the momentum decays, moves the list, and only then the band.
    state.moveSpeed *= rules.scroll.momentumDecayPerTick;
    state.scroll += state.moveSpeed;
    if (state.scroll > 0.0) {
        state.scroll *= rules.scroll.topBandPerTick;
    } else if (state.scroll < minScroll) {
        state.scroll += std::fabs(state.scroll - minScroll) * rules.scroll.bottomBandPerTick;
    }
}

double BarY(const Rules& rules, const Board& board, const glm::dvec2& viewUnits, double scroll) {
    const double travel = Stride(rules, board, viewUnits) - viewUnits.y;
    if (!(travel > 0.0)) return 0.0;
    return (-scroll / travel) * (viewUnits.y - rules.bar.sizeUnits.y);
}

double LineY(const Rules& rules, const glm::dvec2& viewUnits, int line, double scroll) {
    return LayOut(rules, viewUnits).lineOffset * static_cast<double>(line) + scroll;
}

bool LineShown(const Rules& rules, const glm::dvec2& viewUnits, int line, double scroll) {
    // isRectInScreen(pos, (spriteSize.x, lineOffset), V2_ZERO): out when the far
    // corner is above or left of the view, or the near one past it.
    const Layout layout = LayOut(rules, viewUnits);
    const glm::dvec2 pos(layout.columnAdvance, LineY(rules, viewUnits, line, scroll));
    const glm::dvec2 size(layout.iconSize.x, layout.lineOffset);
    if (pos.x + size.x < 0.0 || pos.y + size.y < 0.0) return false;
    return !(pos.x > viewUnits.x) && !(pos.y > viewUnits.y);
}

Hud::Rect LineHitRect(const Rules& rules, const glm::dvec2& viewUnits, int line, double scroll) {
    const Layout layout = LayOut(rules, viewUnits);
    return Hud::Rect{glm::dvec2(layout.columnAdvance, LineY(rules, viewUnits, line, scroll)),
                     layout.barSize + glm::dvec2(layout.iconSize.x, 0.0)};
}

Hud::Rect BackHitRect(const Rules& rules, const glm::dvec2& viewUnits, double stateMs) {
    const glm::dvec2 at = UiLayer::ButtonAnchorAt(rules.layer, UiLayer::Anchor(rules.back, viewUnits), viewUnits, stateMs);
    return UiLayer::RectAt(rules.back, at);
}

namespace {

bool StartDismissing(const State& state) {
    return state.start.present && state.start.dismissedMs >= 0.0;
}

// The start button's anchor and alpha this tick, entering or leaving.
bool StartPlace(const Rules& rules, const State& state, const glm::dvec2& viewUnits, double stateMs, glm::dvec2& at,
                int& alphaByte) {
    if (!state.start.present) return false;
    if (StartDismissing(state)) {
        const double ms = stateMs - state.start.dismissedMs;
        if (ms >= rules.layer.buttonDismissMs) return false;
        at = UiLayer::ButtonDismissAnchorAt(rules.layer, state.start.anchor, viewUnits, ms);
        alphaByte = UiLayer::ButtonDismissAlphaByte(rules.layer, ms);
        return true;
    }
    const double ms = stateMs - state.start.addedMs;
    at = UiLayer::ButtonAnchorAt(rules.layer, state.start.anchor, viewUnits, ms);
    alphaByte = UiLayer::ButtonAlphaByte(rules.layer, ms);
    return true;
}

} // namespace

bool StartHitRect(const Rules& rules, const State& state, const glm::dvec2& viewUnits, double stateMs, Hud::Rect& out) {
    glm::dvec2 at(0.0);
    int alpha = 0;
    // A button on its way out takes no press.
    if (StartDismissing(state) || !StartPlace(rules, state, viewUnits, stateMs, at, alpha)) return false;
    out = Hud::Rect{at - rules.start.sizeUnits * rules.start.origin, rules.start.sizeUnits};
    return true;
}

Outcome Tick(const Rules& rules, const Board& board, State& state, const glm::dvec2& viewUnits, double stateMs,
             const Touch& touch) {
    Outcome outcome;
    const bool touching = touch.pressed || (touch.held && !touch.released);
    if (touch.pressed) {
        // Where it went down, for hasClickInRect's hit position and Button::update's
        // down-inside, against each button where it is this tick.
        state.down = true;
        state.downAt = touch.at;
        state.lastAt = touch.at;
        state.downOnBack = BackHitRect(rules, viewUnits, stateMs).Contains(touch.at);
        Hud::Rect start;
        state.downOnStart = StartHitRect(rules, state, viewUnits, stateMs, start) && start.Contains(touch.at);
    }

    // doScrolling. A board with no rows has nothing to scroll: its stride is under a
    // view, which the original's 86 rows never are.
    if (!board.lines.empty()) {
        double move = 0.0;
        bool moving = false;
        if (touching) {
            move += touch.at.y - state.lastAt.y;
            moving = true;
        }
        if (touch.wheelNotches != 0.0) {
            move += touch.wheelNotches * rules.scroll.wheelUnitsPerNotch;
            moving = true;
        }
        DoScrolling(rules, state, MinScroll(rules, board, viewUnits), moving, move);
    }
    if (touching) state.lastAt = touch.at;

    // The rows' taps, on the rows the view shows, where this tick has put them.
    if (touch.released && state.down) {
        const Layout layout = LayOut(rules, viewUnits);
        for (std::size_t i = 0; i < board.lines.size(); ++i) {
            const Line& line = board.lines[i];
            const int index = static_cast<int>(i);
            if (line.header || !LineShown(rules, viewUnits, index, state.scroll)) continue;
            const Hud::Rect hit = LineHitRect(rules, viewUnits, index, state.scroll);
            if (!hit.Contains(touch.at) || !hit.Contains(state.downAt)) continue;
            if (line.levelUnlocked) {
                if (state.lastClicked != index) {
                    outcome.pick = true;
                    state.lastClicked = index;
                    // removeButton('start') then addButton at the bar's place: a fresh
                    // entrance, fixed on the view.
                    const glm::dvec2 bar(layout.columnAdvance + layout.iconSize.x, hit.min.y);
                    state.start = State::Start{};
                    state.start.present = true;
                    state.start.anchor = bar + rules.start.offsetUnits;
                    state.start.addedMs = stateMs;
                }
            } else {
                outcome.denied = true;
            }
            state.currentWorld = line.world;
            state.currentLevel = line.level;
        }
    }

    // Once the list has moved further than a tap, the start button goes.
    if (std::fabs(state.accumulated) > rules.start.dismissAfterUnits) {
        state.accumulated = 0.0;
        state.lastClicked = -1;
        if (state.start.present && !StartDismissing(state)) state.start.dismissedMs = stateMs;
    }
    if (StartDismissing(state) && stateMs - state.start.dismissedMs >= rules.layer.buttonDismissMs) {
        state.start = State::Start{}; // removeDismissedButtons
    }

    // The layer's buttons (UILayer::update): the press tint while held inside, the
    // press on the release with the down inside.
    const bool insideBack = touch.over && BackHitRect(rules, viewUnits, stateMs).Contains(touch.at);
    Hud::Rect startRect;
    const bool insideStart =
        touch.over && StartHitRect(rules, state, viewUnits, stateMs, startRect) && startRect.Contains(touch.at);
    state.backHeld = state.down && state.downOnBack && touching && insideBack;
    state.startHeld = state.down && state.downOnStart && touching && insideStart;
    if (touch.released) {
        if (state.down && state.downOnBack && insideBack) outcome.back = true;
        if (state.down && state.downOnStart && insideStart) {
            // updateButtons: openState(m_currentWorld, m_currentLevel).
            outcome.start = true;
            outcome.world = state.currentWorld;
            outcome.level = state.currentLevel;
        }
        state.down = false;
        state.downOnBack = false;
        state.downOnStart = false;
        state.backHeld = false;
        state.startHeld = false;
    }
    return outcome;
}

std::vector<Piece> Pieces(const Rules& rules, const Board& board, const State& state, const glm::dvec2& viewUnits,
                          double stateMs, double wallMs) {
    std::vector<Piece> pieces;
    const auto sprite = [&pieces](std::string file, const Hud::Rect& rect, int alphaByte, int rgbByte = 255) {
        Piece piece;
        piece.kind = Kind::Sprite;
        piece.file = std::move(file);
        piece.rect = rect;
        piece.alphaByte = alphaByte;
        piece.rgbBytes = glm::ivec3(rgbByte);
        pieces.push_back(std::move(piece));
    };
    const auto text = [&pieces](const Rules::Text& style, std::string words, const glm::dvec2& at, bool centred,
                                int alphaByte, const glm::ivec3& rgb = glm::ivec3(255)) {
        Piece piece;
        piece.kind = Kind::Text;
        piece.file = style.font;
        piece.words = std::move(words);
        piece.at = at;
        piece.centred = centred;
        piece.unitsPerFontPx = style.unitsPerFontPx;
        piece.rgbBytes = rgb;
        piece.alphaByte = alphaByte;
        pieces.push_back(std::move(piece));
    };

    // world_select_bg.ent, a scene entity.
    sprite(rules.background.sprite,
           Hud::Rect{rules.background.centreOfScreen * viewUnits - rules.background.sizeUnits * 0.5,
                     rules.background.sizeUnits},
           255);

    // doScrolling draws the bar before the loop draws a row.
    sprite(rules.bar.sprite,
           Hud::Rect{glm::dvec2(viewUnits.x - rules.bar.fromRightUnits, BarY(rules, board, viewUnits, state.scroll)),
                     rules.bar.sizeUnits},
           rules.bar.alphaByte);

    const Layout layout = LayOut(rules, viewUnits);
    for (std::size_t i = 0; i < board.lines.size(); ++i) {
        const Line& line = board.lines[i];
        const int index = static_cast<int>(i);
        const glm::dvec2 pos(layout.columnAdvance, LineY(rules, viewUnits, index, state.scroll));
        if (line.header) {
            // Drawn before the loop's cull test, on or off the view.
            text(rules.header, rules.headerPrefix + std::to_string(line.world + 1), pos + rules.header.atUnits, true,
                 255);
            continue;
        }
        if (!LineShown(rules, viewUnits, index, state.scroll)) continue;
        sprite(rules.rows.iconDirectory + line.icon, Hud::Rect{pos, layout.iconSize},
               Alpha(line.unlocked, rules.rows.lockedIconAlphaByte));
        if (!line.unlocked && !line.levelUnlocked) sprite(rules.rows.lockSprite, Hud::Rect{pos, layout.iconSize}, 255);
        const glm::dvec2 bar = pos + glm::dvec2(layout.iconSize.x, 0.0);
        sprite(rules.rows.barSprite, Hud::Rect{bar, layout.barSize}, Alpha(line.unlocked, rules.rows.lockedBarAlphaByte));
        if (line.isNew) {
            // ARGB(iTOb(max(127, GetTime() % 255)), 100, 255, 100).
            const auto ms = static_cast<unsigned long long>(std::max(wallMs, 0.0));
            const int wave = static_cast<int>(ms % static_cast<unsigned long long>(rules.newLabel.alphaPeriodMs));
            text(rules.newLabel.text, rules.newLabel.words, bar + rules.newLabel.text.atUnits, false,
                 std::min(std::max(rules.newLabel.alphaFloorByte, wave), 255), rules.newLabel.rgbBytes);
        }
        const int words = Alpha(line.unlocked, rules.rows.lockedTextAlphaByte);
        text(rules.title, line.title, bar + rules.title.atUnits, false, words);
        text(rules.description, line.description, bar + rules.description.atUnits, false, words);
        text(rules.points, std::to_string(line.points), pos + rules.points.atUnits, true,
             Alpha(line.unlocked, rules.rows.lockedPointsAlphaByte));
    }

    sprite(rules.plaque.sprite, Hud::Rect{rules.plaque.atUnits, rules.plaque.sizeUnits}, 255);
    text(rules.total, std::to_string(board.points), rules.total.atUnits, true, 255);

    // The layer: the back button, then the start button added after it.
    sprite(rules.back.sprite, BackHitRect(rules, viewUnits, stateMs), UiLayer::ButtonAlphaByte(rules.layer, stateMs),
           state.backHeld ? rules.state.pressTintByte : 255);
    glm::dvec2 at(0.0);
    int alpha = 0;
    if (StartPlace(rules, state, viewUnits, stateMs, at, alpha)) {
        sprite(rules.start.sprite, Hud::Rect{at - rules.start.sizeUnits * rules.start.origin, rules.start.sizeUnits},
               alpha, state.startHeld ? rules.state.pressTintByte : 255);
    }
    return pieces;
}

} // namespace MagicPortals::Dashboard
