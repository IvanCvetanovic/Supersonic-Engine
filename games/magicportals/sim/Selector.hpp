#pragma once

// Chapter select and the level grid the original pages through - WorldSelector
// and PortalLevelSelector, both LevelSelectors over one PageManager - as ui.json's
// `selector` block and the arithmetic of their loops: the pager's slide, finger
// and rubber band, the background's pan, the forward arrow's fade, the page
// counter, and every tile with its lock, number, medal and words (the remake's
// ui3 spec sections 0.5, 5 and 6).
//
// Pure, like sim/Dashboard.hpp: every picture is a function of the view, the
// state's clock, the layer's clock (the "requires 60%" sawtooth reads the wall
// clock), a Board laid out once from the save as the state opens, and a State the
// layer keeps between ticks and steps once a tick (ui3 spec 8.1, P1: the pager's
// per-frame decay once per 60 Hz tick).
//
// OWNER RULINGS (ui3 spec 8.2): R1 two chapters a page, as the original; R3
// locking as the original, through sim/Locking.hpp. A named --level still opens
// any level: nothing here is asked on that path.
//
// Units are design units on the VIEW, (0, 0) its top-left, +y down.

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "sim/Chapters.hpp"
#include "sim/Hud.hpp"
#include "sim/Locking.hpp"
#include "sim/MenuState.hpp"
#include "sim/Scores.hpp"
#include "sim/UiLayer.hpp"

namespace MagicPortals::Selector {

struct Rules {
    UiLayer::Rules layer;
    MenuState::Rules state;

    // PageManager / Swyper (ui3 spec 0.5).
    struct Pager {
        double decayPerCall = 0.0;       // getOffset's x0.92, twice a frame with a neighbour
        double snapBelow = 0.0;          // |offset| below this is 0
        double swapOffset = 0.0;         // a release past this changes the page
        double rubberBand = 0.0;         // x1/3: the drawn offset with no neighbour
        double arrowFadeOutFactor = 0.0; // the forward arrow on the last page, a byte truncated
        double arrowFadeInStep = 0.0;    // and elsewhere, capped at 1
    } pager;

    struct Background {
        std::string sprite; // an entity, named with its directory
        glm::dvec2 sizeUnits{0.0};
    } background;

    struct Text {
        std::string font;
        double unitsPerFontPx = 0.0;
        glm::dvec2 atUnits{0.0}; // a centre or a top-left, as each says
    };

    MenuState::Bounce arrowBounce; // I4, on both screens' arrows

    struct Chapters {
        UiLayer::Placed title; // W1 "Select chapter"
        UiLayer::Placed back;
        UiLayer::Placed forward;
        std::string iconPrefix; // world_icon<w>.png
        glm::dvec2 iconSizeUnits{0.0};
        int perPage = 0;
        std::string lockSprite;
        glm::dvec2 lockSizeUnits{0.0};
        std::string medalGold;
        std::string medalSilver;
        std::string medalBronze;
        glm::dvec2 medalSizeUnits{0.0};
        glm::dvec2 medalCentreUnits{0.0}; // from the icon's top-left
        int medalGoldPercent = 0;         // this or more
        int medalBronzeBelowPercent = 0;  // below this; silver between
        Text percent;                     // centred, from the icon's top-left
        Text warning;                     // W6, centred, from the icon's top-left
        std::string warningWords;
        double warningSawtoothMs = 0.0;  // I9
        struct Counter {
            std::string sprite;
            int columns = 0;
            int rows = 0;
            glm::dvec2 slotUnits{0.0};
            glm::dvec2 atScreen{0.0};
            int tintAlphaByte = 0;
            int dotFrame = 0;
        } counter;
        MenuState::Bounce highlight; // I5
    } chapters;

    struct Levels {
        UiLayer::Placed back;
        UiLayer::Placed forward;
        std::string tileSprite;
        std::string lockedTileSprite;
        std::string bossSprite;
        glm::dvec2 tileSizeUnits{0.0};
        int columns = 0;
        int rows = 0;
        Text number; // centred on the tile
        std::string medalGold;
        std::string medalSilver;
        std::string medalBronze;
        glm::dvec2 medalSizeUnits{0.0};
        glm::dvec2 medalAtUnits{0.0}; // its top-left, from the tile's top-left
        Text cornerChapter;           // from the view's top-left
        Text cornerPercent;
        glm::ivec4 cornerArgb{0};
        MenuState::Bounce highlight; // I6
    } levels;
};

// ui.json's `ui_layer`, `menu_state` and `selector`, strictly.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// ---- what the save says, laid out as the state opens ---------------------------

struct Chapter {
    bool unlocked = false;
    int completion = 0;     // percent
    bool warned = false;    // W6: its last level has a medal and it is below the unlock percent
};

struct Tile {
    bool unlocked = false;
    int medal = Scores::kUnplayed;
    bool boss = false;
    int level = -1; // its place in Chapters::Table::levels
};

struct Board {
    bool levels = false; // the grid, else chapter select
    int world = 0;       // the grid's chapter
    int completion = 0;  // the grid's chapter's percent
    std::vector<Chapter> chapters;
    std::vector<Tile> tiles;
    int highlighted = -1; // the last unlocked chapter or level: its bounce
    int items = 0;
    int perPage = 1;
    int Pages() const { return items > 0 ? (items + perPage - 1) / perPage : 1; }
};

// WorldSelector's: every chapter, open or locked, with its percent; the last
// open one highlighted.
Board BuildChapters(const Rules& rules, const Locking::Rules& locking, const Scores::Store& scores,
                    const Chapters::Table& table);
// PortalLevelSelector's for `world`: each level open when the one before has a
// medal (findLastUnlockedLevel: the last of the unbroken run from level 0).
Board BuildLevels(const Rules& rules, const Locking::Rules& locking, const Scores::Store& scores,
                  const Chapters::Table& table, int world);

// The page a state opens on: the grid's holds its last unlocked level; chapter
// select's holds `world`, the chapter last played (WorldSelector::preLoop).
int OpeningPage(const Board& board, int world);

// ---- the pager ------------------------------------------------------------------

struct State {
    int page = 0;
    double offset = 0.0; // screen widths: + while the page slides in from the right
    bool touching = false;
    double touchX = 0.0; // where the finger went down, in units
    int forwardAlphaByte = 255;
    // Where the last tick drew them (Step).
    double currentOffset = 0.0;
    double neighbourOffset = 0.0;
    int neighbour = -1;
    bool rubberBand = false;
    double globalOffset = 0.0;
};

// Swyper::setCurrentPage: the page now, and the slide from where it was.
void SetPage(State& state, int page);
// A state opening on `page`: setCurrentPage from page 0, sliding in under the black.
State Open(const Board& board, int page);
// One tick of PageManager::draw's two getOffset calls (one with no neighbour),
// and fadeForwardArrows.
void Step(const Rules& rules, const Board& board, State& state);
// The finger: down, where it is now, and up. Up changes the page past
// swapOffset when there is a page that way.
void TouchDown(State& state, double xUnits);
void TouchMove(State& state, double xUnits, double viewWidthUnits);
void TouchUp(const Rules& rules, const Board& board, State& state);
// The camera's x the background pans by: clamp(getGlobalOffset, 0, 1) x (its
// width - the view's).
double CameraX(const Rules& rules, const State& state, double viewWidthUnits);

// ---- pictures and hits ------------------------------------------------------------

enum class Kind { Sprite, Text };
enum class Control { None, Back, Forward, Item };

struct Piece {
    Kind kind = Kind::Sprite;
    std::string file; // a sprite as ui.json names it, or a font
    std::string words;
    Hud::Rect rect;
    glm::dvec2 uvMin{0.0};
    glm::dvec2 uvMax{1.0};
    glm::dvec2 at{0.0};
    bool centred = false;
    double unitsPerFontPx = 0.0;
    glm::ivec3 rgbBytes{255};
    int alphaByte = 255;
};

// Every picture `stateMs` into the state in the original's order (ui3 spec 5.3,
// 6.3): the background, the corner words (grid), the layer's arrows (and chapter
// select's title), each drawn page's tiles with what goes on them, then the page
// counter (chapter select). `held` is the control a touch that went down inside
// it is still inside, drawn at the press tint; `heldItem` its item.
std::vector<Piece> Pieces(const Rules& rules, const Board& board, const State& state, const glm::dvec2& viewUnits,
                          double stateMs, double wallMs, Control held, int heldItem);

struct Hit {
    Control control = Control::None;
    int item = -1;
};

// What a point on the view is on this tick: an arrow where it is now, or a tile of
// the current page where the page is drawn; none past the last page's forward.
Hit HitAt(const Rules& rules, const Board& board, const State& state, const glm::dvec2& viewUnits, double stateMs,
          const glm::dvec2& point);

// A tile's rectangle at rest on its page: for the suites.
Hud::Rect ItemRect(const Rules& rules, const Board& board, int item, const glm::dvec2& viewUnits);

// W4 / L6: which medal a percent or a score draws.
std::string ChapterMedal(const Rules& rules, int completion);
std::string LevelMedal(const Rules& rules, int medal);

// The forward arrow's byte after one fadeForwardArrows (FloatColor::getUInt
// truncates to a byte).
int FadeForward(const Rules& rules, int alphaByte, bool lastPage);

} // namespace MagicPortals::Selector
