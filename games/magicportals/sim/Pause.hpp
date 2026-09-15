#pragma once

// The pause screen the original raises over a frozen level - CustomGameMenuLayer,
// with GameMenuLayer and SoundPanelLayer under it - as ui.json's `pause` block
// and the arithmetic that turns it into rectangles, alphas and text.
//
// Pure, like Hud.hpp and UiLayer.hpp: every picture below is a function of the
// view's size, what the pause was opened over, the state of its two switches and
// how long it has been up. The layer only draws what this says and asks it which
// button a tap is on. The layer owns everything that is not a picture: stopping
// game time, the switches' effect on the sound, and where each button goes.

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "sim/Hud.hpp"
#include "sim/UiLayer.hpp"

namespace MagicPortals::Pause {

struct Rules {
    UiLayer::Rules layer;

    int dimAlphaByte = 0; // the black laid over the frozen level

    UiLayer::Placed goldenPlaque;
    UiLayer::Placed currentPlaque;
    glm::dvec2 medalOrigin{0.0}; // at the current plaque's anchor
    glm::dvec2 medalSizeUnits{0.0};
    std::string medalBronze;
    std::string medalSilver;
    std::string medalGold;

    UiLayer::Placed levels; // back to the level grid
    UiLayer::Placed resume;
    UiLayer::Placed skip;
    UiLayer::Placed achievements;
    UiLayer::Placed sound; // sprite is the switch ON
    std::string soundOffSprite;
    UiLayer::Placed music; // sprite is the switch ON
    std::string musicOffSprite;

    struct Title {
        std::string font;
        std::string prefix;
        double unitsPerFontPx = 0.0;
        glm::dvec2 centreOfScreen{0.0};
    } title;

    struct GoldenNumber {
        std::string font;
        double unitsPerFontPx = 0.0;
        glm::dvec2 offsetUnits{0.0}; // from the golden plaque's anchor
    } goldenNumber;
};

// ui.json's `ui_layer` and `pause`, strictly: anything missing is refused.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// What the pause is opened over, read once when it opens: CustomGameMenuLayer's
// constructor reads the score, and nothing on the screen changes it after.
struct Level {
    int savedMedal = 0; // the Scores tier recorded for this level; 0 never finished
    int goldenScore = 0;
    int index = 0; // within its world, 0-based
};

// The two switches, and the music switch's own clock on the pause's. The
// original adds the music switch with a fresh entrance whenever the sound is on
// and there is none, and dismisses it when the sound goes off
// (SoundPanelLayer::manageMusicSwitch); these are when each last happened.
struct Switches {
    bool soundOn = true;
    bool musicOn = true;
    double musicAddedMs = 0.0;      // its entrance began, on the pause's clock
    double musicDismissedMs = -1.0; // it was dismissed, or negative
};

// Which of the pause's buttons.
enum class Button { Levels, Resume, Skip, Achievements, Sound, Music };

enum class Element { Dim, GoldenPlaque, CurrentPlaque, CurrentMedal, Button };

struct Sprite {
    Element element = Element::Dim;
    Button button = Button::Resume; // when element is Button
    std::string file;               // the sprite's file name; empty for the dim, which is drawn plain
    Hud::Rect rect;
    int alphaByte = 0;
    bool pressable = false; // a button not being dismissed
};

// Every picture of the pause `ms` after it opened, in the original's order of
// drawing: UILayer::draw draws every sprite and then every button, each in the
// order it was added - the switch SoundPanelLayer's constructor adds first, the
// constructors' buttons, and the music switch its first update adds last.
std::vector<Sprite> Sprites(const Rules& rules, const Level& level, const Switches& switches,
                            const glm::dvec2& viewUnits, double ms);

// The button a point on the view is on, as Button::isPointInButton asks it of
// each button's rectangle where it is this frame; none that is being dismissed.
std::optional<Button> ButtonAt(const Rules& rules, const Level& level, const Switches& switches,
                               const glm::dvec2& viewUnits, double ms, const glm::dvec2& point);

// The medal sprite for a recorded tier: getLargeSpriteMedalName, 3 gold, 2
// silver, anything else bronze; empty for a level never finished.
std::string MedalSprite(const Rules& rules, int savedMedal);

// "Part N", and where its box is centred.
std::string TitleText(const Rules& rules, const Level& level);
glm::dvec2 TitleCentre(const Rules& rules, const glm::dvec2& viewUnits);

// The golden score, and where its box is centred: the plaque's anchor plus the
// offset.
std::string GoldenText(const Level& level);
glm::dvec2 GoldenCentre(const Rules& rules, const glm::dvec2& viewUnits);

// Both texts' alpha byte: the buttons' colour, whose rgb the draws set to white.
int TextAlphaByte(const Rules& rules, double ms);

} // namespace MagicPortals::Pause
