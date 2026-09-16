#pragma once

// The achievements dashboard the main menu's Achievements button opens -
// ScoreDashboard, with DashboardLayer - as ui.json's `dashboard` block and the
// arithmetic of its loop: the rows and their headers, the scroll with its momentum
// and two bands, the scroll bar, the points plaque, the back button, and the start
// button a tap on a row raises.
//
// Pure. What a row SAYS comes from sim/Achievements.hpp's content, which is not
// this repository's and may be absent; a Board is that content, and what the save
// unlocks of it, laid out once as the dashboard opens. Every picture is then a
// function of the board, the view, the state's clock, the layer's clock (the
// "new!" label reads the wall clock) and a State the layer keeps between ticks and
// steps once a tick (the remake's ui3 spec 8.1, P1).
//
// Units are design units on the VIEW, (0, 0) its top-left, +y down.

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "sim/Achievements.hpp"
#include "sim/Chapters.hpp"
#include "sim/Hud.hpp"
#include "sim/Locking.hpp"
#include "sim/MenuState.hpp"
#include "sim/Scores.hpp"
#include "sim/UiLayer.hpp"

namespace MagicPortals::Dashboard {

struct Rules {
    UiLayer::Rules layer;
    MenuState::Rules state;

    struct Background {
        std::string sprite; // named within the original's assets
        glm::dvec2 sizeUnits{0.0};
        glm::dvec2 centreOfScreen{0.5};
    } background;

    UiLayer::Placed back; // its normalized place and origin are negative: off the left edge

    struct Rows {
        double tileUnits = 0.0;
        double lineFactor = 0.0;   // the row pitch, of a tile
        double columnFactor = 0.0; // the icon column's left edge, of a tile
        double barLines = 0.0;     // the bar is at most this many pitches wide
        std::string iconDirectory;
        std::string barSprite;
        std::string lockSprite;
        int lockedIconAlphaByte = 255;
        int lockedBarAlphaByte = 255;
        int lockedTextAlphaByte = 255;
        int lockedPointsAlphaByte = 255;
    } rows;

    struct Text {
        std::string font;
        double unitsPerFontPx = 0.0;
        glm::dvec2 atUnits{0.0}; // a centre or a top-left, as each says, from what it is placed on
    };
    Text header; // centred, from the row's top-left
    std::string headerPrefix;
    Text title;       // from the bar's top-left
    Text description; // from the bar's top-left
    Text points;      // centred, from the icon's top-left
    Text total;       // centred, on the view
    struct NewLabel {
        Text text;
        std::string words;
        glm::ivec3 rgbBytes{255};
        int alphaFloorByte = 0;
        int alphaPeriodMs = 1;
    } newLabel;

    struct Plaque {
        std::string sprite;
        glm::dvec2 atUnits{0.0};
        glm::dvec2 sizeUnits{0.0};
    } plaque;

    struct Scroll {
        double momentumDecayPerTick = 0.0;
        double topBandPerTick = 0.0;
        double bottomBandPerTick = 0.0;
        double wheelUnitsPerNotch = 0.0;
    } scroll;

    struct Bar {
        std::string sprite;
        glm::dvec2 sizeUnits{0.0};
        double fromRightUnits = 0.0;
        int alphaByte = 255;
    } bar;

    struct Start {
        std::string sprite;
        glm::dvec2 sizeUnits{0.0};
        glm::dvec2 offsetUnits{0.0}; // from the bar's top-left
        glm::dvec2 origin{0.0};
        double dismissAfterUnits = 0.0;
    } start;
};

// ui.json's `ui_layer`, `menu_state` and `dashboard`, strictly.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// The loop's own numbers on a view: the row pitch (lineOffset), the icon column's
// left edge (columnAdvance), the icon's size and the bar's (itemBgSize).
struct Layout {
    double lineOffset = 0.0;
    double columnAdvance = 0.0;
    glm::dvec2 iconSize{0.0};
    glm::dvec2 barSize{0.0};
};
Layout LayOut(const Rules& rules, const glm::dvec2& viewUnits);

// One row of the list: a chapter's header, or an achievement.
struct Line {
    bool header = false;
    int world = 0;
    int level = -1;
    int entry = -1; // in the content's order; -1 for a header
    bool unlocked = false;
    bool levelUnlocked = false; // isLevelUnlocked(world, level)
    std::string title;
    std::string description;
    std::string icon; // within rules.rows.iconDirectory
    int points = 0;
    bool isNew = false;
};

struct Board {
    std::vector<Line> lines;
    int achievements = 0; // getNumAchievements
    int worlds = 0;       // ScoreManager::getNumWorlds
    int points = 0;       // AchievementManager::getScore
};

// The rows `content` makes, in its order with a header wherever the world changes,
// and what the save unlocks of them. A null content is a board with no rows.
Board Build(const Achievements::Content* content, const Locking::Rules& locking, const Scores::Store& scores,
            const Chapters::Table& chapters);

// doScrolling's stride - lineOffset * (achievements + worlds) - and the lowest the
// list scrolls to before the bottom band pulls it back.
double Stride(const Rules& rules, const Board& board, const glm::dvec2& viewUnits);
double MinScroll(const Rules& rules, const Board& board, const glm::dvec2& viewUnits);

// What the layer keeps between ticks.
struct State {
    double scroll = 0.0;      // m_scroll: the rows' offset, 0 at the top, negative further down
    double moveSpeed = 0.0;   // m_moveSpeedY
    double accumulated = 0.0; // m_accumulatedScroll
    int lastClicked = -1;     // the line tapped last, -1 for none
    int currentWorld = -1;    // the row tapped last's world and level
    int currentLevel = -1;

    // The start button (A13): a UIButton added where the tapped row's bar was.
    struct Start {
        bool present = false;
        glm::dvec2 anchor{0.0};
        double addedMs = 0.0;
        double dismissedMs = -1.0; // when dismissStartButton began, or -1
    } start;

    // The one touch followed (Button::update, hasClickInRect).
    bool down = false;
    glm::dvec2 downAt{0.0};
    glm::dvec2 lastAt{0.0};
    bool downOnBack = false;
    bool downOnStart = false;
    bool backHeld = false;  // held down inside: the press tint
    bool startHeld = false;
};

// A tick's input: the touch as Button::update reads it, and the wheel's notches.
struct Touch {
    bool pressed = false;
    bool held = false;
    bool released = false;
    bool over = false;
    glm::dvec2 at{0.0};
    double wheelNotches = 0.0;
};

// What a tick asks the layer to do.
struct Outcome {
    bool back = false;   // the back button pressed: the main menu
    bool pick = false;   // playAchievementPickSound
    bool denied = false; // playAchievementPickDeniedSound (and the notification not drawn)
    bool start = false;  // the start button pressed: openState(world, level)
    int world = -1;
    int level = -1;
};

// doScrolling alone: a touching tick moves the list by `move`; any other, the
// momentum, then the band.
void DoScrolling(const Rules& rules, State& state, double minScroll, bool moving, double move);

// One tick of ScoreDashboard::loop `stateMs` after the state's first frame, in its
// order: doScrolling, the rows' taps, the start button's dismissal once the list
// has moved, then the layer's buttons on the release.
Outcome Tick(const Rules& rules, const Board& board, State& state, const glm::dvec2& viewUnits, double stateMs,
             const Touch& touch);

// The scroll bar's top-left y: (-scroll / (stride - view)) * (view - its height).
double BarY(const Rules& rules, const Board& board, const glm::dvec2& viewUnits, double scroll);

// A line's top edge at a scroll.
double LineY(const Rules& rules, const glm::dvec2& viewUnits, int line, double scroll);

// isRectInScreen for a line's row: its icon column, one pitch tall.
bool LineShown(const Rules& rules, const glm::dvec2& viewUnits, int line, double scroll);

// hasClickInRect's rectangle for a line: the icon and the bar, one pitch tall.
Hud::Rect LineHitRect(const Rules& rules, const glm::dvec2& viewUnits, int line, double scroll);

// The back button's and the start button's Button::isPointInButton rectangles this
// tick (the start button's empty when there is none).
Hud::Rect BackHitRect(const Rules& rules, const glm::dvec2& viewUnits, double stateMs);
bool StartHitRect(const Rules& rules, const State& state, const glm::dvec2& viewUnits, double stateMs, Hud::Rect& out);

enum class Kind { Sprite, Text };

struct Piece {
    Kind kind = Kind::Sprite;
    std::string file;  // a sprite as ui.json names it (an icon with its directory), or a font
    std::string words; // Text
    Hud::Rect rect;    // Sprite: as drawn
    glm::dvec2 at{0.0};
    bool centred = false; // Text: `at` is the box's centre, else its top-left
    double unitsPerFontPx = 0.0;
    glm::ivec3 rgbBytes{255};
    int alphaByte = 255;
};

// Every picture `stateMs` into the state, in the original's order: the scene's
// background; the scroll bar (doScrolling draws it first); the rows - a header's
// text, or the icon, its lock, the bar, "new!", the title, the description and
// the points - for each row on the view; the plaque and the total; then the
// layer's back button and start button. `wallMs` is the layer's clock.
std::vector<Piece> Pieces(const Rules& rules, const Board& board, const State& state, const glm::dvec2& viewUnits,
                          double stateMs, double wallMs);

} // namespace MagicPortals::Dashboard
