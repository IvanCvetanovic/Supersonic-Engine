#pragma once

// The main menu the original opens after its loading screen - PortalMainMenu,
// with MainMenuLayer and SoundPanelLayer - as ui.json's `main_menu` block and
// the arithmetic that turns it into rectangles, colours and alphas.
//
// Pure, like sim/Pause.hpp: every picture is a function of the view's size, the
// two switches, the state's clock and the layer's own clock (the title's bob is
// an absolute GetTimeF read), and of which button a held touch is on. The layer
// draws what this says through the screen overlay and asks it which button a
// point is on; where each button leads is the layer's.

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "sim/Hud.hpp"
#include "sim/MenuState.hpp"
#include "sim/Pause.hpp"
#include "sim/UiLayer.hpp"

namespace MagicPortals::MainMenu {

struct Rules {
    UiLayer::Rules layer;
    MenuState::Rules state;

    struct Background {
        std::string sprite; // named within the original's assets
        glm::dvec2 sizeUnits{0.0};
        glm::dvec2 centreOfScreen{0.5};
    } background;

    UiLayer::Placed play;
    MenuState::Bounce playBounce;
    MenuState::Blink playBlink;

    UiLayer::Placed title;
    double bobUnits = 0.0;
    double bobRadiansPerMs = 0.0;

    UiLayer::Placed credits;
    UiLayer::Placed achievements;
    UiLayer::Placed sound; // sprite is the switch ON
    std::string soundOffSprite;

    // x in UNITS from the left edge, y a fraction of the screen (ui3 spec 0.1).
    struct Music {
        std::string sprite; // ON
        std::string offSprite;
        double atUnitsX = 0.0;
        double atScreenY = 0.0;
        glm::dvec2 origin{0.0};
        glm::dvec2 sizeUnits{0.0};
    } music;
};

// ui.json's `ui_layer`, `menu_state` and `main_menu`, strictly.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

enum class Button { Play, Title, Credits, Achievements, Sound, Music };

// A set of buttons, one bit each: a touch can be inside two at once where the
// title overlaps TAP START, and Button::update follows it in both.
constexpr unsigned Bit(Button button) {
    return 1u << static_cast<unsigned>(button);
}

enum class Element { Background, Button };

struct Piece {
    Element element = Element::Background;
    Button button = Button::Play;
    std::string file; // the sprite, as ui.json names it
    Hud::Rect rect;   // as drawn: bounce and bob included
    int rgbByte = 255;
    int alphaByte = 255;
    bool pressable = false; // a button not on its way out
    Hud::Rect hitRect;      // Button::isPointInButton's: no bounce, no bob
};

// The music switch's anchor: 32 units from the left, on the bottom edge.
glm::dvec2 MusicAnchor(const Rules& rules, const glm::dvec2& viewUnits);

// Every picture of the menu `stateMs` after its first frame, in the original's
// order of drawing: the background (a scene entity), then UILayer::draw's buttons
// in the order they were added - the sound switch SoundPanelLayer's constructor
// adds before MainMenuLayer's, TAP START, the title over it, info, Achievements -
// and the music switch the panel's update adds last. `wallMs` is the layer's own
// clock, which the title's bob reads; `held` the buttons (Bit) a touch that went
// down inside them is still inside, which draw at the press tint.
std::vector<Piece> Pieces(const Rules& rules, const Pause::Switches& switches, const glm::dvec2& viewUnits,
                          double stateMs, double wallMs, unsigned held);

// The buttons (Bit) a point on the view is inside, where each one is this tick;
// none on its way out. Zero on none.
unsigned ButtonsAt(const Rules& rules, const Pause::Switches& switches, const glm::dvec2& viewUnits, double stateMs,
                   const glm::dvec2& point);

// Of a set, the button whose press the original acts on first: MainMenuLayer's
// update (TAP START) runs inside PortalMainMenu::loop before loop's own checks of
// info, the title and Achievements. The switches act on their own update.
std::optional<Button> FirstActed(unsigned buttons);

// TAP START's rectangle once its entrance is over and before any bounce: the
// settled button a suite presses.
Hud::Rect SettledPlayRect(const Rules& rules, const glm::dvec2& viewUnits);

} // namespace MagicPortals::MainMenu
