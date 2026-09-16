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

struct Rules {
    Picture portal;      // portal.ent
    Picture shot;        // projectile.ent
    Character character; // dark_mage.ent, the player
    Beholder beholder;   // beholder.ent, chapter 1's boss
    Spike spike;         // beholder_spike.ent, its spikes
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

} // namespace MagicPortals::Art
