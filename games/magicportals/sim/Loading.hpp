#pragma once

// The loading screen the original opens with - LoadingScreen - as ui.json's
// `loading` block and the arithmetic of its loop.
//
// OWNER RULING R4 (the remake's ui3 spec 8.2): built as the original, before the
// main menu. The original loads resource.list's textures two a frame while its
// character walks to a portal, then holds 1000 ms of frame time and sets the
// menu state. The port loads nothing there - its textures load as they are
// drawn - but keeps the frame count, which is the same on any device, one frame
// a tick. Pure: every function below is of the loop's frame number, counted from
// 1 on the state's first tick, and the tick's length.

#include <string>

#include <glm/glm.hpp>

#include "sim/UiLayer.hpp"

namespace MagicPortals::Loading {

struct Rules {
    struct Background {
        std::string sprite; // named within the original's assets
        glm::dvec2 sizeUnits{0.0};
        glm::dvec2 centreOfScreen{0.5};
    } background;

    glm::dvec2 characterStartOfScreen{0.0};
    int firstFrame = 0; // of the character's sheet, walking
    int lastFrame = 0;
    double strideMs = 0.0;

    glm::dvec2 portalAtScreen{0.0};
    std::string portalEntity;   // whose particle systems it plays
    std::string haloSprite;     // black_halo.ent's picture, multiplied
    double haloScale = 0.0;     // of that picture's own size

    struct Vanish {
        std::string suckEntity;
        double suckOffsetUnits = 0.0; // from the portal towards the character
        double suckAngleOffsetDeg = 0.0;
        std::string sparklesEntity;
    } vanish;

    int resources = 0;         // resource.list's entries
    int resourcesPerFrame = 0; // m_texturesPerFrame
    double holdMs = 0.0;       // frame time after the last, before the menu

    UiLayer::Placed logo;

    struct Dots {
        std::string font;
        double unitsPerFontPx = 0.0;
        glm::dvec2 centreOfScreen{0.0};
        int columns = 0;           // each string's length
        int run = 0;               // how many dots in a row
        int stringsPerPattern = 0; // each pattern is in m_swapStrings this many times
        int trackDots = 0;         // the grey row under them
        int trackAlphaByte = 0;
    } dots;
};

// ui.json's `loading`, strictly.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// How many resources are loaded when loop frame `frame` begins: two a frame,
// never past the list.
int LoadedBefore(const Rules& rules, int frame);

// The frame whose loop loads the last one (81 for 162 two at a time): the
// character is hidden on it, and the hold is counted from the frame after.
int LastLoadingFrame(const Rules& rules);

// Whether frame `frame` still runs the loading branch: moves the character and
// writes the dots.
bool IsLoading(const Rules& rules, int frame);

// Whether the character is drawn after frame `frame`'s loop.
bool CharacterShown(const Rules& rules, int frame);

// The character's place on the view on frame `frame`: interpolate(start, portal,
// loaded / total), as it stands after that frame's loop (and where the last one
// left it after that).
glm::dvec2 CharacterAt(const Rules& rules, const glm::dvec2& viewUnits, int frame);

glm::dvec2 PortalAt(const Rules& rules, const glm::dvec2& viewUnits);

// vanishEffect's two entities, the frame the last texture loads: the suck effect
// `suckOffsetUnits` from the portal towards where the character stands, turned
// getAngle(direction) - 90 degrees (getAngle is atan2(x, y) in [0, 360)); and the
// sparkles on the character.
glm::dvec2 SuckAt(const Rules& rules, const glm::dvec2& viewUnits);
double SuckAngleDeg(const Rules& rules, const glm::dvec2& viewUnits);
glm::dvec2 SparklesAt(const Rules& rules, const glm::dvec2& viewUnits);

// The sheet frame on loop frame `frame`, `tickMs` a frame: FrameTimer::set(first,
// last, stride, repeat) resets to `first` on its first call and steps one on
// each time its accumulated frame time reaches the stride.
int CharacterFrame(const Rules& rules, int frame, double tickMs);

// m_swapStrings[loaded mod 26]: the run of dots drawn while `loaded` are loaded.
std::string DotsText(const Rules& rules, int loaded);
// The grey row drawn after it.
std::string TrackText(const Rules& rules);

// Whether frame `frame` sets the menu state: m_loadedTime, the frame time of every
// frame after the last loading one, is over the hold.
bool HoldOver(const Rules& rules, int frame, double tickMs);

// The first frame on which HoldOver is true.
int MenuFrame(const Rules& rules, double tickMs);

} // namespace MagicPortals::Loading
