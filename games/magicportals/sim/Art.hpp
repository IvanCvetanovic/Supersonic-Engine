#pragma once

// What the port draws for what no level pictures: the port's art.json.
//
// The levels picture everything placed in them (Sprites.hpp). What the game
// itself adds - the portals a shot opens, the shot, the player, chapter 1's
// boss and its spikes - the original draws with its own entities, and art.json
// says which image each of those .ent files names, whether it is added, and how
// its sheet is cut. Those are the .ent's facts, and test_mp_sprites pins them.
// How fast a sheet plays is not decoded, so it is carried as a _guess and
// pinned by nothing.
//
// Each also carries its .ent's <EmissiveColor>: the original draws every sprite
// at min(1, ambient + emissive) of its colour (Lighting::AmbientTerm), what no
// level places as much as what it does, so the layer needs it for these too.
//
// And its lighting facts (step 49): static and applyLight, which say which 2D
// lights reach it (Lighting::ReceiverMask), its <Normal> when it names one, and
// its <Light> when it has one - the shot's is the port's one live light. Each is
// the .ent's and required where a default would be a guess: static and
// applyLight on every picture, and a light's every number.
//
// The images are the original's, read from the extracted APK beside the remake
// (MAGICPORTALS_ORIGINAL_DIR) and never committed.

#include <optional>
#include <string>

#include <glm/glm.hpp>

#include "sim/Lighting.hpp"

namespace MagicPortals::Art {

struct Picture {
    std::string sprite;           // an image among the original's entities
    bool additive = false;        // the .ent's blendMode 1
    int columns = 1;              // its SpriteCut
    int rows = 1;
    double framesPerSecond = 0.0; // _guess, for a sheet of more than one frame that plays
    glm::dvec3 emissive{0.0};     // the .ent's <EmissiveColor>, rgb; required, never defaulted
    bool isStatic = false;        // the .ent's static; required
    bool applyLight = false;      // the .ent's applyLight; required
    std::string normal;           // its <Normal>, a file under the original's entities/normalmaps/; empty = none
    // Its <Light>, with no scale to apply (none of these .ent files carries a
    // <Scale> other than 1). `halo` is a file among the original's entities, not
    // yet a path.
    std::optional<Lighting::Light> light;
    double z = 0.0;               // the depth its light's height is measured from; read only with a light

    int Frames() const { return columns * rows; }
};

// The player, as dark_mage.ent draws it: a sheet whose rows are directions and
// whose columns are a walk.
struct Character : Picture {
    int startFrame = 0;    // the .ent's startFrame
    double pivotXPx = 0.0; // its PivotAdjust: the point of the image, from its
    double pivotYPx = 0.0; // centre, that stands on the entity
    int leftRow = 0;       // derived from the decoded DIRECTION enum, not testimony
    int rightRow = 0;
    int idleColumn = 0;    // _guess
};

// How a picture pulses: from one scale to the other and back, each way a
// stride, eased by smoothEnd - the original's bounce().
struct Pulse {
    glm::dvec2 fromScale{1.0};
    glm::dvec2 toScale{1.0};
    double strideMs = 0.0;

    // Its scale `elapsedMs` in.
    glm::dvec2 ScaleAt(double elapsedMs) const;
};

// Chapter 1's boss, as beholder.ent draws it: frame 0 its eye open, frame 1
// shut. Drawn at its frame's size times the pulse of what it is doing; while it
// throws rocks it keeps the last.
struct Beholder : Picture {
    Pulse seeking;
    Pulse hurt;
    Pulse dead;
};

// What it fires, as beholder_spike.ent draws it: the image points down, and it
// is turned to where it flies, standing on its pivot.
struct Spike : Picture {
    double pivotXPx = 0.0;
    double pivotYPx = 0.0;
};

// A static portal as its script redraws it (ETHCallback_portal_static, art.json's
// static_portal). The level pictures the entity; the callback, once, scales it -
// its picture AND its particle systems, which is what ETHEntity::Scale does - and
// tints its picture red or blue by the node's `color`. Its particles are not
// tinted: a particle is drawn in its own colour (ETHParticleManager.cpp:381-389).
struct StaticPortal {
    std::string entity;            // the entity name the callback is named for
    double scale = 1.0;            // ETHEntity::Scale, applied once
    std::string red;               // the `color` value that takes tintRed
    glm::dvec3 tintRed{1.0};       // SetColor when it is
    glm::dvec3 tintOtherwise{1.0}; // and when it is anything else, or absent

    // GetString("color") == red ? tintRed : tintOtherwise. An absent value reads
    // as "" there, which is not red.
    glm::dvec3 TintFor(const std::string& colour) const { return colour == red ? tintRed : tintOtherwise; }
};

// The dial the original adds behind a timed crystal, as timer.ent draws it and
// its script runs it (art.json's timer; addTimerToCrystal and ETHCallback_timer).
// It is added `zOffset` behind the crystal at alpha `alpha`, and every frame
// follows it. While the time lasts it shows one of `frames` cells by the time
// elapsed, and pulses from pulseFrom to pulseTo and back by bounce(), each way
// a leg of max(pulseMinLegMs, the time left): the pulse quickens as time runs
// out. From the frame the time is up, or from the frame after the one that
// finds the crystal taken, it shrinks and fades by shrinkPerFrame a frame and is
// deleted when its scale, as the original stores it, drops below goneBelowScale
// (Gone). The crystal never fades: it goes at its time.
struct Timer : Picture {
    int frames = 1;                 // the cells the time is divided into
    double alpha = 1.0;             // SetAlpha when it is added
    int zOffset = 0;                // its depth from the crystal's
    double pulseFrom = 1.0;         // bounce()'s two scales
    double pulseTo = 1.0;
    double pulseMinLegMs = 0.0;     // the shortest leg of the pulse
    double shrinkPerFrame = 1.0;    // scale and alpha, a frame, once the time is up
    double decayFramesPerSecond = 0.0; // the frames that shrink is counted in
    double goneBelowScale = 0.0;    // deleted once its stored scale x drops below this
    double screenPxPerUnit = 0.0;   // m_scaleFactor: its stored scale over the port's

    // The cell shown `elapsedMs` into a time of `timeMs`: int(elapsed / time x
    // frames) in floats, as the script computes it, clamped to the cells.
    int FrameAt(double elapsedMs, double timeMs) const;
    // A leg of the pulse at that moment: max(pulseMinLegMs, time - elapsed).
    double LegMs(double elapsedMs, double timeMs) const;
    // Its scale at that moment: bounce() with this moment's leg, whose count of
    // legs so far - the elapsed time over the leg - says which way it goes.
    glm::dvec2 PulseAt(double elapsedMs, double timeMs) const;
    // What its scale and alpha are multiplied by over `seconds` of shrinking.
    double DecayOver(double seconds) const;
    // Whether a dial of `scaleX` in the port's units is deleted: ins 184-199 test
    // the entity's stored scale, which bounce set as the pulse x m_scaleFactor.
    bool Gone(double scaleX) const;
};

struct Rules {
    Picture portal;      // portal.ent
    Picture shot;        // projectile.ent
    Picture torchLight;  // light_from_projectile.ent, added where a shot lights a torch
    Character character; // dark_mage.ent, the player
    Beholder beholder;   // beholder.ent, chapter 1's boss
    Spike spike;         // beholder_spike.ent, its spikes
    // portal_static: placed by the levels, redrawn by its script
    StaticPortal staticPortal;
    Timer timer;         // timer.ent, the dial behind a timed crystal
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

} // namespace MagicPortals::Art
