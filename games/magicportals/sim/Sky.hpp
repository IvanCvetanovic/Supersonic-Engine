#pragma once

// The sky the original pins to the camera: StaticSky (Sky.angelscript), decoded
// in data/sky.json.
//
// A level places its sky where its editor left it. The original overwrites that
// position on every frame, so the level's number is never drawn: the sky sits at
// the camera's corner plus half a screen, which is the view's centre, and is one
// screen tall. The port drew the level's number, and wherever its camera moved -
// right along a long level, down a tall one - the black behind the scene showed
// (step 44 counted 34 levels). The original's own frames put its sky at the
// screen's centre on every level measured, whatever the camera had done.
//
// WHAT IS BUILT, and when. Game::preLoop builds StaticSky on every level whose
// `properties` does not set `space_bg`; the 19 that do build SpaceSky, which is
// not ported (sky.json says what it would take). StaticSky finds every entity
// named `sky` or `sky.ent` and one named `satellite`.
//
// THE ARITHMETIC, per sky t, in the level's own pixels (sky.json's _units):
//   scale        view height / picture height, once, at build
//   width        the LAST sky's picture width times its scale (m_width)
//   position     camera corner + pitch + (width * t, 0) + (scrollX, 0), every frame,
//                pitch being view * 0.5 for a still sky and size * 0.5 moved by
//                one size for the first and last of a scrolling strip
//   drawn centre position - pivot, the pivot a scrolling strip's ends are given
//   scroll       scrollX -= scroll * frame, and back to 0 once |scrollX| >= width
//   satellite    camera corner + where the level put it, every frame
//
// Every level places at most one sky, whose picture is as tall as the view, and
// none scrolls: in play this is "the sky at the view's centre". The rest is
// carried because it is decoded and cheap, and tests/test_mp_sky.cpp pins it.
//
// Renderer-free, and free of the registry: the camera is passed in, and what
// comes back is where to draw. The layer places the quads.

#include "sim/Sprites.hpp"
#include "sim/Tscn.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace MagicPortals::Sky {

// The port's sky.json, static_sky.
struct Rules {
    std::string propertiesEntity;       // properties: where space_bg is read
    std::string spaceBgKey;             // space_bg: set, and SpaceSky runs instead
    std::vector<std::string> skyNames;  // sky, sky.ent
    std::string satelliteName;          // satellite
    std::string scrollKey;              // scroll: a sky's own float
    double screenPitch = 0.0;           // 0.5
    double frameCapMs = 0.0;            // 200: unitsPerSecond's min
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

// One sky as scaleSky leaves it.
struct Layer {
    std::string node;          // its entity node
    glm::dvec2 imagePx{0.0};   // its picture's own size
    double scale = 1.0;        // view height / picture height
    double scrollValue = 0.0;  // its `scroll`; 0 when it has none, as GetFloat answers
    double scrollX = 0.0;      // scrollPos.x; its y is never anything but 0
    double pivotX = 0.0;       // the pivot adjust a scrolling strip's ends take, in level px
};

struct Controller {
    // False on a level that sets space_bg, and on one that places neither a sky
    // nor a satellite, where there is nothing for it to move.
    bool running = false;
    std::vector<Layer> skies;  // in the level's file order
    double widthPx = 0.0;      // m_width
    double scrollValue = 0.0;  // m_scrollValue, the last sky's
    bool scroll = false;       // m_scroll: the last sky's scroll above 0

    bool hasSatellite = false;
    std::string satelliteNode;
    glm::dvec2 satelliteOriginalPx{0.0}; // where the level put it: its originalPos
};

// StaticSky's constructor and scaleSky, over a level's scene and the sprites the
// layer draws for it (for each sky's picture size). `viewHeightPx` is the view's
// height in the level's pixels. False, with `error`, only for a sky the scene
// places without a picture, which the original could not have sized either.
bool Build(const Rules& rules, const Tscn::Scene& scene, const std::vector<Sprites::Sprite>& sprites,
           double viewHeightPx, Controller& out, std::string& error);

// Where StaticSky::position puts sky t this frame: its entity's position, in the
// level's pixels. `viewCentrePx` and `viewPx` are the camera as it is drawn.
glm::dvec2 PositionPx(const Rules& rules, const Controller& sky, std::size_t t, const glm::dvec2& viewCentrePx,
                      const glm::dvec2& viewPx);

// Where its picture's centre is drawn: the position less the pivot adjust.
glm::dvec2 DrawnCentrePx(const Rules& rules, const Controller& sky, std::size_t t, const glm::dvec2& viewCentrePx,
                         const glm::dvec2& viewPx);

// Its picture's drawn size: the picture times its scale.
glm::dvec2 DrawnSizePx(const Controller& sky, std::size_t t);

// Where the satellite is drawn this frame: the camera's corner plus where the
// level put it. Only meaningful when hasSatellite.
glm::dvec2 SatellitePx(const Controller& sky, const glm::dvec2& viewCentrePx, const glm::dvec2& viewPx);

// The scroll half of StaticSky::update, run AFTER the frame's positions: the
// original places each sky and only then moves its scrollPos, so a frame draws
// the scroll the previous one left. Nothing on a still sky.
void Advance(const Rules& rules, Controller& sky, double dtS);

} // namespace MagicPortals::Sky
