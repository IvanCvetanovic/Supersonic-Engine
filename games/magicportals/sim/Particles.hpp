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
//     AM_ALPHA_TEST 2, AM_NONE 3, AM_MODULATE 4. Of the 102 systems, 75 are 1,
//     23 are 0 and 4 are 4 (step 59's census). Every AM_ADD bitmap is alpha-less
//     and every AM_PIXEL one carries alpha - which is why sparkles.bmp works at
//     all, a bitmap with no alpha channel whose black ground adds nothing. The
//     four AM_MODULATE systems (black_fade, blood, dragon_knight, ghost_minion)
//     belong to entities no level places.
//   - animationMode is ETHParticleSystem::FRAME_ANIMATION_MODE: PLAY_ANIMATION
//     1, PICK_RANDOM_FRAME 2.
//
// The bitmaps are named bare and live in the original's `particles/` directory
// - a third place, beside its `entities/` and `sprites/` - and, like every
// other image of the original's, they stay outside this repository.
//
// THE ARITHMETIC IS ETHANON'S, line by line (ETHParticleManager.cpp), and pure:
// Reset, Release and Step move one particle, and Drawn, DrawColour, QuadPx,
// WorldRotation and SlotFraction say how it is drawn. What a particle is drawn
// WITH - a quad, a z, a frame of the sheet - is the layer's, per frame, and never
// the simulation's: nothing here reaches Game::Level or the state hash.

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace MagicPortals::Particles {

// gs2d's Video::ALPHA_MODE.
inline constexpr int kAlphaPixel = 0;    // AM_PIXEL: GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
inline constexpr int kAlphaAdd = 1;      // AM_ADD: GL_ONE, GL_ONE (GLES2Video.cpp:792-795)
inline constexpr int kAlphaTest = 2;     // AM_ALPHA_TEST: blending disabled on GLES2 (GLES2Video.cpp:800-804);
                                         // no .ent uses it, and Drawable refuses it
inline constexpr int kAlphaModulate = 4; // AM_MODULATE: GL_ZERO, GL_SRC_COLOR (GLES2Video.cpp:796-799)

// One <ParticleSystem>, exactly as the .ent states it. Times are milliseconds
// and lengths are the original's pixels.
struct System {
    std::string bitmap; // <Bitmap>, an image among the original's particles/

    int count = 0;              // particles: how many the system holds at once
    bool allAtOnce = false;     // else they are released staggered across a lifetime
    int alphaMode = kAlphaAdd;  // Video::ALPHA_MODE, as the file says it
    bool additive = true;       // alphaMode 1, AM_ADD
    int animationMode = 1;      // 1 plays the sheet by age, 2 picks a frame at random
    int repeat = 0;             // how many lives each particle gets; 0 is forever

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
    // <Luminance>, ETHParticleSystem's `emissive` (ETHParticleSystem.cpp:134):
    // what lifts an alpha-blended particle over the scene's ambient.
    glm::dvec3 luminance{1.0};

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

// Whether the port can draw a system's blend: AM_ADD and AM_PIXEL, the two the
// levels' 889 placed systems use (879 and 10). AM_MODULATE has no pipeline, and
// the layer refuses a system that asks for it by name rather than mixing it.
bool Drawable(const System& system);

// What ETHEntity::Scale(s) does to each system its entity carries (ETHEntity.cpp:591-599,
// through ETHSpriteEntity.cpp:512-518 and ETHParticleManager.cpp:444-451):
// ETHParticleSystem::Scale (ETHParticleSystem.cpp:27-40) multiplies every length -
// gravity, direction and its spread, the start point and its spread, the size, its
// spread, its growth and its bounds - and no time, angle or colour. (Its bounding
// sphere too, which the port does not read.) The manager also multiplies each live
// particle's size and velocity (PARTICLE::Scale, ETHParticleManager.h:160-164).
//
// A pool MADE from the scaled system is that pool: Reset's spreads are drawn over
// ranges the scale multiplied, from the same stream, and a first release only
// positions. What the original's order adds is its first frame, moved at the old
// scale before the callback runs (ETHActiveEntityHandler.cpp:157-162): the layer
// scales only portal_static's systems, whose start point, spreads and direction are
// all 0, and for those the two are the same frame.
void Scale(System& system, double scale);

// ---- a particle, moved as ETHParticleManager moves it -------------------------

// Where the entity that carries a system stands this frame: GetPosition() and
// GetAngle() (ETHSpriteEntity.cpp:576). The POSITION, not the centre of its
// picture: a pivot adjust moves the picture and never the emitter.
struct Owner {
    glm::dvec2 atPx{0.0};  // the original's pixels, +y down
    double angleDeg = 0.0; // Ethanon's angle: positive turns counter-clockwise on the screen
};

// ETHParticleManager's PARTICLE, in the port's units.
struct Particle {
    glm::dvec2 atPx{0.0};
    glm::dvec2 velocityPx{0.0}; // `dir`, per frame-speed unit
    glm::dvec2 bornPx{0.0};     // where its current life began (the suites read it)
    double angleDeg = 0.0;      // Ethanon's angle, counter-clockwise on the screen
    double angleDirDeg = 0.0;
    double size = 0.0;
    double lifeMs = 0.0; // its own, spread from the system's
    double elapsedMs = 0.0;
    int repeats = 0;
    int frame = 0;
    bool released = false;
    glm::dvec4 colour{1.0};
};

// Uniform in [min(from, to), max(from, to)], as gs2d's Randomizer::Float(min, max)
// is (Randomizer.cpp). The layer's own seeded generator stands behind it.
using Random = std::function<double(double from, double to)>;

// A point turned by Ethanon's angle, as Multiply(Vector2, RotateZ(DegreeToRadian(a)))
// turns it (GameMath.h:829-838, 897-905): (x cos + y sin, -x sin + y cos) in the
// original's +y-down pixels, so a positive angle turns counter-clockwise on the
// screen.
glm::dvec2 Turn(const glm::dvec2& px, double angleDeg);

// ETHParticleManager::ResetParticle (ETHParticleManager.cpp:473-505): a new
// life - its turn rate, lifetime, size and velocity (direction plus its spread,
// turned by the owner's angle), its colour at birth, its frame - then Release.
// Both draw their random numbers in the original's statement order, x before y,
// so one seed gives one sequence whatever the compiler.
void Reset(const System& system, Particle& particle, const Owner& owner, const Random& random);

// ETHParticleManager::PositionParticle (:507-523): its angle (angleStart, its
// spread, plus the owner's) and its place (startPoint plus its spread, turned by
// the owner's angle, from the owner's position). A particle released for the
// first time takes only this; the rest was drawn when its pool was made.
void Release(const System& system, Particle& particle, const Owner& owner, const Random& random);

// A pool of `count` as CreateParticleSystem makes it (:119-127): each Reset at
// the owner, none released.
std::vector<Particle> MakePool(const System& system, int count, const Owner& owner, const Random& random);

// One frame of one particle, UpdateParticleSystem's loop body (:200-262), with
// `index` its place in a pool of `poolSize`. The elapsed time is the frame's own;
// the motion is capped at 250 ms (:195-196). `killed` is ETHParticleManager::Kill.
// Returns whether it counts as ACTIVE for this frame (:208-213): released and
// bigger than nothing as the last frame left it, before this frame moves it -
// the count a light's brightness and a halo's are the share of.
bool Step(const System& system, Particle& particle, int index, int poolSize, const Owner& owner, double elapsedMs,
          bool killed, const Random& random);

// Whether DrawParticleSystem draws it (:371-378): not spent, released, bigger
// than nothing, its colour's alpha above 0, and not past its life once killed.
bool Drawn(const System& system, const Particle& particle, bool killed);

// The colour it is drawn in (:381-389). An alpha-blended particle is multiplied
// by min(1, luminance + ambient); an added one is not. And AN ADDED ONE'S ALPHA
// IS 1: AM_ADD blends GL_ONE, GL_ONE, so the original never reads the alpha its
// colour lerps (a crystal's sparkle goes 1 -> 0) - the port's added pipeline
// weights by alpha, and would fade it twice.
glm::dvec4 DrawColour(const System& system, const Particle& particle, const glm::dvec3& ambient);

// The quad it is drawn on: SQUARE, size by size, whatever the cell's shape
// (DrawOptimal(pos, colour, angle, Vector2(size, size)), :413). The cell is
// stretched to it.
glm::dvec2 QuadPx(const Particle& particle);

// Its angle as the engine's rotation about +z: Ethanon's positive angle turns
// counter-clockwise on the screen, the converter writes a node's as the negated
// Godot rotation (tscn.py:433), and Units::ToWorldRotation takes a Godot one.
float WorldRotation(const Particle& particle);

// Where system `slot` of an entity's `systems` draws, as a fraction of one
// sprite slot in front of the entity's own picture. Ethanon adds an entity's
// pieces to one map keyed by depth, sprite, halo, then each system in order
// (ETHEntityRenderingManager.cpp:74, 85, 106), so at equal depth they draw in
// that order and before the next entity. The halo takes a quarter (step 49), and
// the half is where the layer puts what stands between two slots (the beholder,
// a thrown stone, a light with no owner): the systems share what lies between,
// evenly, so none of them ties with either.
double SlotFraction(int slot, int systems);

} // namespace MagicPortals::Particles
