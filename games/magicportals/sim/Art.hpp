#pragma once

// What the port draws for what no level pictures: the port's art.json.
//
// The levels picture everything placed in them (Sprites.hpp). What the game
// itself adds - the portals a shot opens, the shot, the player, chapter 1's
// boss and its spikes - the original draws with its own entities, and art.json
// says which image each of those .ent files names, whether it is added, and how
// its sheet is cut. Those are the .ent's facts, and test_mp_sprites pins them.
// How fast a sheet plays is mostly not decoded, so it is carried as a _guess and
// pinned by nothing - except the player's walk, whose stride and standing
// frame are decoded from its script (Character).
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

// What MainCharacter::linearMotion does to the player on a no_gravity level,
// every update (art.json character.no_gravity_motion): turns the standing column
// on the character's own FrameTimer, and hovers the picture on its pivot.
struct NoGravityMotion {
    double columnStrideMs = 0.0;       // set(0, 3, 280, true)
    double hoverScreenPx = 0.0;        // cos(angle) times this, added to the pivot's y in screen pixels
    double hoverAtScreenPx = 0.0;      // the screen height the port takes those pixels at
    double hoverRadiansPerSecond = 0.0; // unitsPerSecond's argument
    double hoverStartRadians = 0.0;    // PI + PIb, set on the first update
    double hoverWrapRadians = 0.0;     // PI * 2, taken off once the angle is past it

    // The hover's offset to the pivot's y, in units of a view `viewUnitsTall` tall.
    double HoverUnits(float angle, double viewUnitsTall) const;
};

// When the player draws its arm out (art.json character.push):
// SideScrollerCharacter::detectPushing's ray, and the rows findFinalDirection
// swaps in while it meets something.
struct Push {
    double reachFrameWidthShare = 0.0; // 0.6 of getSize().x, the frame's width
    glm::dvec2 offsetPx{0.0};          // scale(-6) on y: added to the ray's far end, in level units
    double airVelocityShare = 0.0;     // off the ground the ray follows the body's x velocity times this
    int leftRow = 0;                   // the row pushing left draws
    int otherRow = 0;                  // the row pushing any other way draws
};

// The player, as dark_mage.ent draws it and its script frames it: a sheet whose
// rows are directions and whose columns are a walk. The row it shows IS its
// direction, and it starts facing initialDirection - not on the .ent's
// startFrame, which its constructor sets and its first update replaces. The
// column is a FrameTimer over the whole row, stepped a stride at a time only
// on the updates it walks, so it carries across a stop and a turn (art.json) -
// except on a no_gravity level, where it wears another sheet and the same timer
// turns the column it stands on (NoGravityMotion).
struct Character : Picture {
    double pivotXPx = 0.0;     // its PivotAdjust: the point of the image, from its
    double pivotYPx = 0.0;     // centre, that stands on the entity
    int initialDirection = 0;  // GameCharacter's direction when it is made: a row
    int leftRow = 0;           // derived from the decoded DIRECTION enum, not testimony
    int rightRow = 0;
    int idleColumn = 0;        // the column it stands on
    double strideMs = 0.0;     // frameStride: a column's time; framesPerSecond is 1000 / strideMs
    Push push;                 // the arm out against what it walks into
    // The sheet MainCharacter's constructor swaps in on a no_gravity level. Only
    // the image: the cut, the frame, the pivot and the normal map stay dark_mage.ent's.
    std::string noGravitySprite;
    NoGravityMotion noGravity;
};

// The sheet the player wears on a level: the suit where it sets no_gravity.
const std::string& PlayerSheet(const Character& mage, bool noGravity);

// The ETHFramework FrameTimer (FrameTimer.angelscript) a character keeps, from
// its constructor's first and last of 0. Set is FrameTimer::set (bytes
// 23254..23650): the time is added; a changed first or last resets it to first
// with no time; else it steps ONE frame once the time reaches the stride, the
// stride taken off, past last back to first when it repeats and held on last
// with no time when it does not.
struct FrameTimer {
    int first = 0;
    int last = 0;
    int frame = 0;
    double timeMs = 0.0;

    int Set(int firstFrame, int lastFrame, double strideMs, bool repeat, double elapsedMs);
};

// The player's own state on a no_gravity level, as MainCharacter keeps it.
struct NoGravityPlayer {
    FrameTimer timer;
    int idleColumn = 0;   // the column the last update left for the next to stand on
    float angle = 0.0f;   // linearMotionAngle, a float as the entity's custom data is
    bool moved = false;   // linearMotion has run: the angle is set, and the pivot hovers
};

// One update of MainCharacter on a no_gravity level, from updateFrame to
// linearMotion, `elapsedMs` of frame time. Returns the column the update draws:
// the walk's, set(0, 3, stride, true), on a walking update; else the idle column
// the update before left. Then linearMotion turns the angle and sets the idle
// column, set(0, 3, columnStrideMs, true).
int UpdateNoGravity(const Character& mage, NoGravityPlayer& player, bool walking, double elapsedMs);

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

struct Rules {
    Picture portal;      // portal.ent
    Picture shot;        // projectile.ent
    Picture torchLight;  // light_from_projectile.ent, added where a shot lights a torch
    Character character; // dark_mage.ent, the player
    Beholder beholder;   // beholder.ent, chapter 1's boss
    Spike spike;         // beholder_spike.ent, its spikes
    // portal_static: placed by the levels, redrawn by its script
    StaticPortal staticPortal;
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

} // namespace MagicPortals::Art
