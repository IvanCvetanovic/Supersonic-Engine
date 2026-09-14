#pragma once

// The HUD a level is played under, and the few seconds every level opens with:
// the port's ui.json, and the arithmetic that turns it into rectangles and alphas.
//
// WHY IT IS HERE AND NOT IN THE LAYER. Everything below is a pure function of a
// view's size and a level's age, so every number the original draws with - where
// a button sits, how fast a pad pulses, when the plaque goes - can be pinned by a
// suite with no window, no registry and no renderer. The layer only places quads
// where these say. That is the same split Camera::Clamp and Scores::Store make.
//
// Units are DESIGN UNITS throughout, which are the level's own pixels: the view
// is view.json's height tall. Rectangles are in VIEW space - (0, 0) the view's
// top-left corner, +y down - and the layer adds the camera's corner to them.
//
// Nothing here reaches Game::Level or the state hash. A HUD is a picture, and the
// one thing it may change - a clear-portals press - goes through Portals::State.

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Supersonic {
class BitmapFont;
}

namespace MagicPortals::Hud {

// The screen corner an element is measured from.
enum class Anchor { TopLeft, TopRight, BottomLeft, BottomRight };

struct Rect {
    glm::dvec2 min{0.0};  // top-left, view units, +y down
    glm::dvec2 size{0.0};

    glm::dvec2 Max() const { return min + size; }
    glm::dvec2 Centre() const { return min + size * 0.5; }
    bool Contains(const glm::dvec2& point) const;
};

// A sprite held a stated inset in from a corner. The inset is from the corner's
// own two edges, so {0, 0} is flush whichever corner it is.
struct Placement {
    std::string sprite;
    Anchor anchor = Anchor::TopLeft;
    glm::dvec2 insetUnits{0.0};
    glm::dvec2 sizeUnits{0.0};
};

struct Rules {
    // 0x78FFFFFF: every HUD control's colour in the original.
    int alphaByte = 255;

    Placement restart;
    Placement pause;
    Placement clearPortals;

    struct Pads {
        std::string leftSprite;
        std::string rightSprite;
        double sizeUnits = 0.0;
        double hitRadiusUnits = 0.0;
        double slideFromUnits = 0.0; // how far outside its corner a pad starts
        double slideMs = 0.0;
        double strideMs = 0.0;
        int strides = 0;
        int variationByte = 0;
        std::string tutorialLevel; // the one level whose pads pulse long and ring
        int tutorialStrides = 0;
        int tutorialVariationByte = 0;
        std::string ringSprite;
        double ringStrideMs = 0.0;
        double ringSizeUnits = 0.0;
        double ringMinBias = 0.0;
    } pads;

    struct Overlay {
        double fadeMs = 0.0;
        int layers = 1;
        int layersOverPads = 0; // the rest are drawn under the pads
        // How far into the level's age the blacks' own clock starts: the original
        // times them by wall clock from a point after the load, and everything
        // else by frame time that carries the load. Wholly black until then.
        double startAfterMs = 0.0;
    } overlay;

    struct Caption {
        std::string font;
        std::string prefix;
        double unitsPerFontPx = 0.0;
        glm::dvec2 centreOfView{0.5};
        double fadeMs = 0.0;
    } caption;

    // The no-portal sign: a LEVEL ENTITY whose script pins it to the camera's
    // corner, so it reads as HUD.
    struct Sign {
        std::string entity;       // the .ent a level places it by
        double followMs = 0.0;    // each ease toward the corner
        double retargetMs = 0.0;  // how long before an ease may be aimed again
        double centreInBySize = 0.0; // its centre this many of its own sizes in from the corner
    } sign;

    struct Plaque {
        std::string sprite;
        glm::dvec2 centreUnits{0.0};
        glm::dvec2 sizeUnits{0.0};
        glm::dvec2 medalCentreUnits{0.0};
        glm::dvec2 medalSizeUnits{0.0};
        std::string medalBronze;
        std::string medalSilver;
        std::string medalGold;
        double appearMs = 0.0;
        double dismissAfterMs = 0.0;
        double dismissMs = 0.0;
    } plaque;
};

// From ui.json, strictly: a missing or malformed number is refused rather than
// defaulted, because a HUD a unit off its corner looks almost right.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// The original's smoothEnd, sin(v * PI / 2): every Interpolator's filter.
double SmoothEnd(double v);

// The control alpha as a fraction.
double Opacity(const Rules& rules);

// Where a placement sits on a view `viewUnits` in size.
Rect Place(const Placement& placement, const glm::dvec2& viewUnits);

enum class Side { Left, Right };

// How far outside its corner a pad still is, `ageMs` into a level: the whole of
// slideFromUnits at 0, none once the slide is over.
double PadSlideUnits(const Rules& rules, double ageMs);

// A walk pad's drawn rectangle, slid in as far as the level's age says.
Rect PadRect(const Rules& rules, Side side, const glm::dvec2& viewUnits, double ageMs);

// The screen corner a pad is drawn from: where its hit circle and its ring are
// centred, sliding with it.
glm::dvec2 PadCorner(const Rules& rules, Side side, const glm::dvec2& viewUnits, double ageMs);

// Whether a point is on a pad: within the hit radius of its corner.
bool OnPad(const Rules& rules, Side side, const glm::dvec2& viewUnits, double ageMs, const glm::dvec2& point);

// The pads' alpha, pulsing for the first strides of a level and flat after.
double PadOpacity(const Rules& rules, double ageMs, bool tutorial);

// The ring the tutorial pads emit. `shown` is false outside the tutorial and
// once the pulse has stopped.
struct Ring {
    bool shown = false;
    double sizeUnits = 0.0;
    double alpha = 0.0;
};
Ring RingAt(const Rules& rules, double ageMs, bool tutorial);

// How dark the level's opening black is, as an alpha over everything beneath
// every layer of it - the world, the buttons, the plaque: all the stacked layers
// composed. Whole until overlay.startAfterMs of the level's age, then fading over
// fadeMs; zero once that is over.
double OverlayAlpha(const Rules& rules, double ageMs);

// The same for `count` of the layers alone: what the pads, drawn between them,
// are covered by.
double OverlayLayersAlpha(const Rules& rules, double ageMs, int count);

// The no-portal sign's place, eased after a target the way the original's
// followUp eases an entity (utilEntityEffect.angelscript): a position
// interpolator over followMs, filtered by smoothEnd, aimed again at the target
// once retargetMs have passed since it was last aimed and only while the sign
// is not already there. `at` is where the sign is; set it to where the level
// placed the entity before the first Tick.
struct Follow {
    glm::dvec2 at{0.0};
    glm::dvec2 from{0.0};
    glm::dvec2 to{0.0};
    double elapsedMs = 0.0;
    double sinceAimedMs = 0.0;
    bool started = false;

    // One frame of it, `dtMs` long, toward `target`. Returns the new `at`.
    glm::dvec2 Tick(const Rules& rules, const glm::dvec2& target, double dtMs);
};

// The frame time a PATH-DEPENDENT frame-time clock - the sign's follow - is handed
// on a tick `dtMs` long that leaves the level `ageMs` old. None while the blacks
// are whole, and all that was held back on the first tick after: in the original
// that stretch is its load, which renders no frame and reaches every frame-time
// clock as ONE long frame's delta. A chase re-aimed every 100 ms runs one 480 ms
// frame very differently from twenty-nine short ones; a clock that is a pure
// function of the level's age (the caption, the pads, the plaque) needs no such
// care. `heldMs` carries what is owed from tick to tick.
double HandOver(const Rules& rules, double ageMs, double dtMs, double& heldMs);

// Where followUp is sent: the camera's top-left corner, in the level's units,
// plus centreInBySize of the sign's own size.
glm::dvec2 SignTarget(const Rules& rules, const glm::dvec2& cameraCorner, const glm::dvec2& signSize);

// The caption's alpha; zero once it has faded.
double CaptionAlpha(const Rules& rules, double ageMs);

// "Part N" for the level at `index` within its world.
std::string CaptionText(const Rules& rules, int index);

// The current-score plaque's alpha, and its medal's, which share it.
double PlaqueAlpha(const Rules& rules, double ageMs);

// The medal art for a Scores tier, or empty for a level never finished.
std::string MedalSprite(const Rules& rules, int medal);

// One glyph of the caption, placed on the view: its rectangle, and the part of
// its font page it shows as a texture transform (scale, then offset, over the
// whole page), which is how a single quad draws one letter of an atlas.
struct Glyph {
    Rect rect;
    int page = 0;
    glm::dvec2 uvScale{1.0};
    glm::dvec2 uvOffset{0.0};
};

// The caption laid out as drawCenteredFadingText and gs2d's DrawBitmapText lay
// it out: the line's box - its summed advances by the font's line height - is
// centred on centreOfView, and each glyph sits at the pen plus its own offsets,
// all at unitsPerFontPx. No kerning, because the original applies none.
std::vector<Glyph> LayOutCaption(const Rules& rules, const Supersonic::BitmapFont& font, const std::string& text,
                                 const glm::dvec2& viewUnits);

} // namespace MagicPortals::Hud
