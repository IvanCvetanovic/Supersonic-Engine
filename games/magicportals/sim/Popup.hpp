#pragma once

// The tutorial and help popups the original raises over a level - ETHFramework's
// Popup, the game's HelpPopup and its LevelHelp*Popup classes - as ui.json's
// `popups` block and the arithmetic that turns it into rectangles, alphas, turns
// and texture frames.
//
// Pure, like Pause.hpp and LevelEnd.hpp: every picture is a function of the view's
// size, the class of popup and an Open - its clock, whether a close has been
// asked for, and where each of its waypoint loops has got to. The loops are the
// one piece of state that is not a pure function of the clock (a FrameTimer steps
// at most one waypoint a frame and carries the remainder), so they are stepped
// here, a tick at a time, the way LevelEnd::Counter is. The layer owns everything
// that is not a picture: stopping game time, what opens one, and the tap that
// closes it.
//
// Units are design units and rectangles are on the VIEW, (0, 0) its top-left, +y
// down, as Hud::Rect.

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "sim/Hud.hpp"
#include "sim/UiLayer.hpp"

namespace MagicPortals::Popup {

// A waypoint's easing: the Interpolator filters WaypointManager hands each move.
enum class Filter { Linear, SmoothEnd, SmoothBeginning };

// ETHFramework's filters, on a bias already clamped to 0..1.
double Filtered(Filter filter, double bias);

// One waypoint of a WaypointSprite: where it is, as an offset from the screen's
// centre in fractions of the screen (HelpPopup::computePos), its alpha byte, its
// angle in degrees COUNTER-CLOCKWISE on the screen, and the move to the NEXT
// waypoint: how long it lasts and how it is eased. A move of 0 ms is a jump.
struct Waypoint {
    glm::dvec2 at{0.0};
    int alphaByte = 0;
    double angleDeg = 0.0;
    Filter filter = Filter::Linear;
    double ms = 0.0;
};

// A sprite sheet: `columns` x `rows` equal frames. A static item shows `frame`;
// an animated track steps a frame every frameMs (0 for none).
struct Sheet {
    int columns = 1;
    int rows = 1;
    int frame = 0;
    double frameMs = 0.0;
};

// One thing a class draws after the framework, in its order of drawing.
struct Item {
    enum class Kind { Static, Track };
    Kind kind = Kind::Static;
    std::string name;             // the member the decode draws it through, for the log and the suites
    std::string sprite;           // within the original's assets, e.g. "entities/tap_icon.png"
    glm::dvec2 sizeUnits{0.0};    // one frame's
    glm::dvec2 origin{0.5};       // where on the sprite its point is
    Sheet sheet;
    glm::dvec3 tintRgb{1.0};      // a track's rgb, 0..1
    glm::dvec2 at{0.0};           // a static's place, from the screen's centre in fractions of the screen
    std::vector<Waypoint> waypoints; // a track's loop
};

struct Class {
    std::string name;
    UiLayer::Placed card;   // the popup sprite: help_popup.png, or a class's own
    double loopMs = 0.0;    // what every track's waypoints add up to
    std::vector<Item> items;
};

// What opens a popup on a level: the scene, as chapters.json names it, and the
// class - or no class at all, for a block whose popup the port does not build.
struct Opener {
    std::string scene;
    std::string label;
    std::string popup; // empty: nothing opens
};

struct Rules {
    UiLayer::Rules layer;

    int dimAlphaByte = 0;
    UiLayer::Placed card;        // every class's unless it names its own
    UiLayer::Placed closeButton; // H3

    // setButtonHighlightEffects: the close button's bounce and blink.
    struct Highlight {
        glm::dvec2 bounceA{1.0};
        glm::dvec2 bounceB{1.0};
        double bounceStrideMs = 0.0;
        double blinkA = 1.0;
        double blinkB = 1.0;
        double blinkStrideMs = 0.0;
    } highlight;

    std::vector<Opener> levelStart;

    struct HelpBlock {
        std::string entity;
        double boxScale = 0.0;  // of the entity's collision box
        double maxMovePx = 0.0; // the most a touch may travel and still open one
        std::vector<Opener> levels;
    } helpBlock;

    std::vector<Class> classes;

    const Class* FindClass(const std::string& name) const;
};

// ui.json's `ui_layer` and `popups`, strictly: anything missing is refused, and so
// is a track whose waypoints do not add up to its class's loop, or an opener that
// names a class the file does not describe.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// The class the level named `scene` opens as it loads, or at a help block: null
// for none. `hasBlock` says whether the level is listed for a help block at all,
// which it can be with no class: a block that is there and opens nothing.
const Class* LevelStartClass(const Rules& rules, const std::string& scene);
const Class* HelpBlockClass(const Rules& rules, const std::string& scene, bool& hasBlock);

// A FrameTimer as WaypointManager::update drives it over `strides`: the time
// accumulates, and once it reaches the current stride the frame steps ONE on and
// the stride is taken off, the remainder kept; past the last frame it wraps.
struct Loop {
    int frame = 0;
    double timeMs = 0.0;

    void Tick(const std::vector<double>& strides, double dtMs);
};

// Where a track's sprite is: WaypointManager::getCurrentPoint, the current
// waypoint and the next (the first again after the last), mixed by the current
// move's filtered bias, min(time, stride) / max(stride, 1).
struct Point {
    glm::dvec2 at{0.0};
    double alpha = 0.0; // 0..1
    double angleDeg = 0.0;
};
Point PointOf(const Item& track, const Loop& loop);

// The strides a track's loop steps through, and a sheet's.
std::vector<double> TrackStrides(const Item& track);
std::vector<double> SheetStrides(const Item& item);

// A popup that is up.
struct Open {
    std::string className;
    double clockMs = 0.0;     // UI frame time since it was created
    double closedAtMs = -1.0; // when the close came, on that clock; negative until it does
    std::vector<Loop> tracks; // one per item, a static's unused
    std::vector<Loop> sheets; // one per item, only an animated sheet's used
};

// A popup of `cls` as its constructor leaves it: every clock at zero.
Open Start(const Class& cls);

// One tick: the clock, every loop, and the sheets while the close button is not
// dismissed (the class's draw() steps them, and draws nothing once it is).
void Tick(const Class& cls, Open& open, double dtMs);

// Popup::update's close: every sprite and the button dismissed at once, on this
// clock. A second close changes nothing.
void Close(Open& open);
bool Closing(const Open& open);

// Everything dismissed and over: the frame the original sets the last layer
// current again and resumes game time.
bool Gone(const Rules& rules, const Open& open);

// What each piece of a popup is.
enum class Element { Dim, Card, CloseButton, Static, Track };

// One piece, in the original's order of drawing. A rectangle as drawn unturned,
// the part of its texture it shows, a turn in degrees counter-clockwise about the
// rectangle's own centre, and the colour multiplied into it.
struct Piece {
    Element element = Element::Dim;
    std::string name;
    std::string sprite; // empty for the dim, which is drawn plain
    Hud::Rect rect;
    glm::dvec2 uvMin{0.0};
    glm::dvec2 uvMax{1.0};
    double angleDeg = 0.0;
    glm::dvec3 rgb{1.0};
    int alphaByte = 0;
};

// Every picture of the popup: UILayer::draw's sprites (the dim, the card) and its
// button, then the class's own draw(). Pieces at alpha 0 are left out.
std::vector<Piece> Pieces(const Rules& rules, const Class& cls, const Open& open, const glm::dvec2& viewUnits);

// The close button, `ms` into the popup, as Button::draw sizes and colours it:
// the entrance's alpha byte (or the dismissal's), where its anchor is, the
// bounce's scale and the blink's brightness - both frozen from the close on, when
// UIButton::update stops calling Button::update.
struct CloseButtonState {
    int alphaByte = 0;
    glm::dvec2 anchor{0.0};
    glm::dvec2 scale{1.0};
    double brightness = 1.0;
};
CloseButtonState CloseButtonAt(const Rules& rules, const Open& open, const glm::dvec2& viewUnits);

// A help block's touch rectangle on the view: the entity's collision box, scaled,
// centred on the entity's place less the camera's top-left.
Hud::Rect HelpBlockRect(const Rules& rules, const glm::dvec2& entityUnits, const glm::dvec2& collisionUnits,
                        const glm::dvec2& cameraCornerUnits);

} // namespace MagicPortals::Popup
