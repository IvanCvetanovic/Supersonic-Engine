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

// The ring a level places round a no-portal field, as its own callback redraws it
// every frame (art.json's antiportal; ETHCallback_antiportal). The callback is
// named for the entity less its .ent, so both spellings of a placement run it.
// It blinks the picture between two colours - a triangle wave, each leg a stride
// long - and turns it spinDegPerS degrees a second. The picture's size is not
// this callback's: the manager scales it once (placement.json antiportal.manager).
struct Antiportal {
    std::string entity;    // the entity name, without .ent, whose callback it is
    glm::dvec3 from{1.0};  // blinkColor's colorA, at the start of every leg
    glm::dvec3 to{1.0};    // and its colorB, at the end of one
    double strideMs = 0.0; // a leg: the period is two of them
    double spinDegPerS = 0.0; // AddToAngle(unitsPerSecond(this)), counter-clockwise

    // blinkColor's colour `elapsedMs` into the blink: the leg count says which way
    // this leg runs, and the remainder how far along it is. SetColor writes the rgb
    // alone (ETHEntity.cpp:493-498), so the alpha is the caller's to keep.
    glm::dvec3 ColourAt(double elapsedMs) const;
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
    // antiportal: placed by the levels, blinked and turned by its script
    Antiportal antiportal;
    Timer timer;         // timer.ent, the dial behind a timed crystal
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

} // namespace MagicPortals::Art
