#pragma once

// The credits screen the main menu's info button opens - CreditsScreen, with
// CreditsScreenLayer - as ui.json's `credits` block and the arithmetic of its
// update: the back button's place, entrance and bounce, the papyrus, and the one
// image of names that scrolls up it for ever.
//
// Pure, like sim/MainMenu.hpp: every picture is a function of the view's size,
// the state's clock, the scroll and whether the back button is held. The layer
// keeps the scroll between ticks and steps it once a tick (the remake's ui3 spec
// 8.1, P1), and draws these pieces through the screen overlay.

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "sim/Hud.hpp"
#include "sim/MenuState.hpp"
#include "sim/UiLayer.hpp"

namespace MagicPortals::Credits {

struct Rules {
    UiLayer::Rules layer;
    MenuState::Rules state;

    struct Background {
        std::string sprite; // named within the original's assets
        glm::dvec2 sizeUnits{0.0};
        glm::dvec2 centreOfScreen{0.5};
    } background;

    UiLayer::Placed back;
    MenuState::Bounce backBounce;

    struct Papyrus {
        std::string sprite;
        glm::dvec2 xOfWidth{0.0}; // the two factors of the width its left edge is at
        glm::dvec2 sizeUnits{0.0};
    } papyrus;

    struct Strip {
        std::string sprite;
        glm::dvec2 sizeUnits{0.0};
        int alphaByte = 255;
        double screenPxPerSecond = 0.0; // UnitsPerSecond's argument: screen pixels
        double atScreenPx = 0.0;        // the screen height those pixels are taken at (P2)
        double flingDecayPerTick = 0.0;
    } strip;
};

// ui.json's `ui_layer`, `menu_state` and `credits`, strictly.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// P2: the strip's speed in units a second at any view - the original's pixels a
// second as they are on a screen `strip.atScreenPx` tall, 256 units.
double UnitsPerSecond(const Rules& rules, const glm::dvec2& viewUnits);

// The strip's top edge, and the last move a touch gave it.
struct Scroll {
    double y = 0.0;
    double moveSpeed = 0.0;
};

// The constructor's: the strip's top on the view's bottom edge, still.
Scroll Start(const glm::dvec2& viewUnits);

// One tick of CreditsScreenLayer::update, `tickMs` long. While `touching`, the
// strip goes with the touch's move this tick, which becomes the fling; otherwise
// the fling decays by a tick's factor and the strip rises by a tick's speed less
// it. Then the wrap: above its own height off the top, it comes back in at the
// bottom edge; below the bottom edge, back to above the top.
void Step(const Rules& rules, Scroll& scroll, const glm::dvec2& viewUnits, double tickMs, bool touching,
          double moveUnitsY);

// The papyrus's and the strip's left edge.
double ColumnX(const Rules& rules, const glm::dvec2& viewUnits);

enum class Element { Background, Back, Papyrus, Strip };

struct Piece {
    Element element = Element::Background;
    std::string file; // the sprite, as ui.json names it
    Hud::Rect rect;   // as drawn: the bounce included
    int rgbByte = 255;
    int alphaByte = 255;
};

// Every picture `stateMs` after the state's first frame, in the original's order:
// the scene's background, the layer's back button (UILayer::draw), then
// CreditsScreenLayer::draw's papyrus and strip. `backHeld` draws the button at the
// press tint.
std::vector<Piece> Pieces(const Rules& rules, const glm::dvec2& viewUnits, double stateMs, const Scroll& scroll,
                          bool backHeld);

// Button::isPointInButton's rectangle for the back button this tick: where its
// entrance has it, with no bounce.
Hud::Rect BackHitRect(const Rules& rules, const glm::dvec2& viewUnits, double stateMs);

} // namespace MagicPortals::Credits
