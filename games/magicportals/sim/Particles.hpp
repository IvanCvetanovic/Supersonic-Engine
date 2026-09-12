#pragma once

// The particle systems an .ent carries: what makes the original's scenery move.
//
// Nothing in a converted level moves. The remake's converter writes a level's
// sprites and drops the rest of each entity, so the port draws a still picture
// of a game whose crystals sparkle, whose torches burn and whose portals turn.
// The motion is not sprite animation - it is a PARTICLE SYSTEM held in each
// entity's own .ent, which the converter never read.
//
// 81 of the original's 190 .ent files carry one, and 21 of those carry TWO -
// portal_static among them, which is why this returns all of an entity's rather
// than its first. Every system states the SAME seventeen attributes and ten
// child elements, so this reader demands all of them and refuses a file missing
// one rather than quietly taking a default.
//
// Two numbers are enumerations of the engine's, decoded rather than guessed:
//   - alphaMode is gs2d's Video::ALPHA_MODE (Video.h): AM_PIXEL 0, AM_ADD 1,
//     AM_ALPHA_TEST 2, AM_NONE 3, AM_MODULATE 4. Every emitter in the game is
//     1, so every particle is ADDED - which is also why sparkles.bmp works at
//     all, a bitmap with no alpha channel whose black ground adds nothing.
//   - animationMode is ETHParticleSystem::FRAME_ANIMATION_MODE: PLAY_ANIMATION
//     1, PICK_RANDOM_FRAME 2.
//
// The bitmaps are named bare and live in the original's `particles/` directory
// - a third place, beside its `entities/` and `sprites/` - and, like every
// other image of the original's, they stay outside this repository.
//
// This reads the files. Nothing here moves: how a particle is released, carried
// and drawn is the layer's, per frame, and never the simulation's.

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace MagicPortals::Particles {

// One <ParticleSystem>, exactly as the .ent states it. Times are milliseconds
// and lengths are the original's pixels.
struct System {
    std::string bitmap; // <Bitmap>, an image among the original's particles/

    int count = 0;          // particles: how many the system holds at once
    bool allAtOnce = false; // else they are released staggered across a lifetime
    bool additive = true;   // alphaMode 1, AM_ADD
    int animationMode = 1;  // 1 plays the sheet by age, 2 picks a frame at random
    int repeat = 0;         // how many lives each particle gets; 0 is forever

    double lifeTimeMs = 0.0;
    double randomLifeTimeMs = 0.0; // spread, applied as +/- half

    double size = 0.0;
    double randomizeSize = 0.0; // spread, +/- half
    double growth = 0.0;        // added to the size each frame-speed unit
    double minSize = 0.0;
    double maxSize = 0.0;

    double angleStart = 0.0;     // degrees
    double randAngleStart = 0.0; // spread, applied as 0..this
    double angleDir = 0.0;       // degrees added each frame-speed unit
    double randAngle = 0.0;      // spread on angleDir, +/- half

    glm::dvec2 gravity{0.0};        // added to the direction each frame-speed unit
    glm::dvec2 direction{0.0};      // the velocity a particle starts with
    glm::dvec2 randomizeDir{0.0};   // spread on it, +/- half
    glm::dvec2 startPoint{0.0};     // from the entity's own position
    glm::dvec2 randStartPoint{0.0}; // spread on it, +/- half

    glm::dvec4 colour0{1.0}; // at birth
    glm::dvec4 colour1{1.0}; // at death, lerped by age

    int columns = 1; // <SpriteCut>, the grid its bitmap is cut into
    int rows = 1;

    int Frames() const { return columns * rows; }
};

// Reads every particle system out of an .ent, in the order the file states
// them.
//
// An empty result is the ordinary case - 109 of the 190 files carry none - and
// is not an error. False, with `error`, when the file cannot be read, is not
// the UTF-16 the original writes, or states a system missing a field.
bool Load(const std::string& path, std::vector<System>& out, std::string& error);

} // namespace MagicPortals::Particles
