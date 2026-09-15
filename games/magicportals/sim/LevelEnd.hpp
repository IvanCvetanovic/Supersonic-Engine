#pragma once

// How a level ends, as the original ends one: the 1400 ms beat after the door or
// the death, the HUD going - cut at the door, dismissed at a death, the walk pads
// decaying either way - and the two screens raised over the level that goes on
// running under them, LevelFinishedLayer and LevelLostLayer. ui.json's
// `level_end` block, and the arithmetic that turns it into rectangles, alphas and
// text.
//
// Pure, like Pause.hpp: every picture is a function of the view's size, what the
// level ended with, the counters as they stand and how long the screen has been
// current. The layer holds the clocks and the counters, steps the level under the
// screen, draws what this says and asks it which button a tap is on.
//
// Units are design units and rectangles are on the VIEW, (0, 0) its top-left, +y
// down, as Hud::Rect.

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "sim/Hud.hpp"
#include "sim/UiLayer.hpp"

namespace MagicPortals::LevelEnd {

// A fade_edge.png stretched over the world from the screen's top-left.
struct Veil {
    std::string sprite;
    int tintAlphaByte = 0;
    glm::dvec2 sizeOfScreen{0.0};
};

// A text a draw() writes: its font, its scale, and where it is put from a point
// the screen already has.
struct TextRule {
    std::string font;
    double unitsPerFontPx = 0.0;
    glm::dvec2 offsetUnits{0.0};
};

struct Rules {
    UiLayer::Rules layer;

    double wonDelayMs = 0.0;  // the door to the finished screen, in game time
    double lostDelayMs = 0.0; // the death to the lost screen
    double padDecayFactor = 0.0;

    struct Finished {
        Veil veil;
        UiLayer::Placed title;
        UiLayer::Placed portalsPlaque;
        UiLayer::Placed goldenPlaque;
        int goldenPlaqueBelowScore = 0; // drawn only for a final score under this
        UiLayer::Placed restart;
        UiLayer::Placed next;
        UiLayer::Placed list;
        UiLayer::Placed medal; // no sprite of its own: the tier names it
        std::string medalBronze;
        std::string medalSilver;
        std::string medalGold;
        TextRule counter; // from the medal's anchor
        double counterStrideMs = 0.0;
        TextRule goldenNumber; // from the golden plaque's anchor
        std::string crystalSprite;
        glm::dvec2 crystalOffsetUnits{0.0}; // its top-left, from the medal's anchor
        glm::dvec2 crystalSizeUnits{0.0};
        TextRule crystalCount; // its top-left, from the crystal's
        double crystalStrideMs = 0.0;
    } finished;

    struct Lost {
        Veil veil;
        UiLayer::Placed title;
        UiLayer::Placed restart;
        UiLayer::Placed list;
    } lost;
};

// ui.json's `ui_layer` and `level_end`, strictly: anything missing is refused.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// ScoreManager::computeScore: 3 gold, 2 silver, 1 bronze. A level with crystals
// none of which were collected is bronze; within the golden score, gold with
// every crystal and silver without; within it and two more, silver; else bronze.
int ComputeScore(int portals, int goldenScore, int crystals, int crystalsTotal);

// ScoreCounter: a Timer that adds the frame's time, and each time it reaches the
// stride resets to zero and steps `current` one toward `end`.
struct Counter {
    int current = 0;
    int end = 0;
    double timeMs = 0.0;

    void Tick(double dtMs, double strideMs);
};

// What a finished level is scored on, read when the door was reached.
struct Play {
    int portalsUsed = 0;
    int goldenScore = 0;
    int crystals = 0;      // collected
    int crystalsTotal = 0; // the level's
};

// The medal a screen is showing: the tier the counters' CURRENT values earn,
// recomputed every frame as LevelFinishedLayer::draw recomputes it.
int ShownScore(const Play& play, int portalsShown);

// What each piece of a screen is.
enum class Element {
    Veil,
    Title,
    PortalsPlaque,
    GoldenPlaque,
    Button,
    Medal,
    Counter,
    GoldenNumber,
    Crystal,
    CrystalCount,
};

enum class Button { Restart, Next, List };

// One piece of a screen, in the original's order of drawing. A sprite has a
// file and a rectangle; a text has a font, its words, and the point it is laid
// out from - its centre, or its top-left where `centred` is false.
struct Piece {
    Element element = Element::Veil;
    Button button = Button::Restart; // when element is Button
    std::string file;                // a sprite's file, or a text's font
    Hud::Rect rect;
    std::string words;
    glm::dvec2 at{0.0};
    double unitsPerFontPx = 0.0;
    bool centred = true;
    int alphaByte = 0;
    bool pressable = false;
};

// The finished screen `ms` after it became current, with the counters where
// they stand: UILayer::draw's sprites (the veil, the title, the portals plaque,
// the golden plaque) and buttons, then LevelFinishedLayer::draw's medal, counter,
// golden number, crystal and crystal count.
std::vector<Piece> Finished(const Rules& rules, const Play& play, int portalsShown, int crystalsShown,
                            const glm::dvec2& viewUnits, double ms);

// The lost screen `ms` after it became current: its veil and title, then its
// two buttons.
std::vector<Piece> Lost(const Rules& rules, const glm::dvec2& viewUnits, double ms);

// The button a point on the view is on, as Button::isPointInButton asks it of
// each button's rectangle where it is this frame.
std::optional<Button> ButtonAt(const std::vector<Piece>& pieces, const glm::dvec2& point);

// A texture stretched over `rect` and sampled LINEARLY AND CLAMPED to its edge
// texels, as three quads a renderer that repeats can draw exactly: a strip half a
// texel wide at each end showing only that edge texel's centre, and between them
// the texture from the first texel's centre to the last one's. Only the width is
// split; `texels` is the texture's size.
struct Strip {
    Hud::Rect rect;
    glm::dvec2 uvMin{0.0};
    glm::dvec2 uvMax{1.0};
};
std::vector<Strip> ClampedStrips(const Hud::Rect& rect, const glm::ivec2& texels);

// ---- the HUD as a level ends ------------------------------------------------

// A pad's alpha byte `ticks` after the end, from `startByte`: uint(a * factor),
// once a tick.
int PadDecayByte(const Rules& rules, int startByte, int ticks);

// The point a HUD control is anchored at - the corner of its rectangle that its
// Placement is measured from, which is the origin the original adds it with.
glm::dvec2 HudAnchor(const Hud::Placement& placement, const glm::dvec2& viewUnits);

// A HUD control `ms` into UIButton::dismiss: where it is, and its alpha - its
// own `alphaByte` times the dismiss's (Button::draw multiplies them). `shown` is
// false once the dismiss is over.
struct Dismissed {
    Hud::Rect rect;
    double alpha = 0.0;
    bool shown = false;
};
Dismissed HudDismissed(const Rules& rules, const Hud::Placement& placement, int alphaByte,
                       const glm::dvec2& viewUnits, double ms);

} // namespace MagicPortals::LevelEnd
