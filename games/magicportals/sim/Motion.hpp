#pragma once

// The small motions the original's scripts give an entity, decoded in
// data/motions.json. One engine helper drives them all: linearMotion
// (ETHFramework/utilEntityEffect.angelscript, bytes 327527..328407), which a
// callback calls once a frame.
//
// WHAT linearMotion DOES, per call (motions.json's linear_motion._source):
//   first call  originalPos = the entity's position, angle = startAngle
//   every call  angle += speed x min(200, frame ms) / 1000 x m_factor (0 paused)
//               if angle > 2 PI: angle += -2 PI, ONCE (not a modulo)
//               offset = cos(angle) x stride x sign(speed)
//               v = vertical ? (0, offset) : (offset, 0), turned by rotateZ(axis)
//               position = originalPos + v x getScale()
// The angle moves BEFORE the offset is read, in the same call, and the frame
// draws the position that call wrote: the first frame drawn is already at
// cos(startAngle + one frame's step), not at cos(startAngle). getScale() is 1 in
// the port's units (00_order 8.12a).
//
// WHAT CALLS IT here (the visuals plan's 3.2): every crystal while it lives
// (ETHCallback_crystal) and every key while no one carries it and it has not
// found its keyhole (ETHCallback_key), each with speed 2, stride 1.5, vertical,
// axis 0 and a start angle randF(PI). And (3.3) the placed pictures whose own
// callbacks call it with the node's `speed` and `stride` (motions.json `placed`):
// the hint arrow along its own angle, the dashed circle up and down, the tapping
// hand along its angle less 90; each from a start angle of 0. The same rows say
// which of them SetAlpha(0.55) and which turn a set number of degrees a second
// (the dashed circle, AddToAngle(unitsPerSecond(8))). The ghost's hover of 12.2
// takes the same helper with another row.
//
// Renderer-free and free of the registry and of Game::Level: a motion moves a
// PICTURE (and what its entity carries - its particles), never a body, a trigger
// or the state hash. The layer keeps the state, as it keeps the sky's.

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace MagicPortals::Tscn {
struct Node;
}

namespace MagicPortals::Motion {

// One row of motions.json: what one callback passes linearMotion.
struct Row {
    std::vector<std::string> entities; // the entity names, without .ent, whose callback it is
    double speed = 0.0;                // radians a second; its sign is the direction
    double stride = 0.0;               // the amplitude, in the level's units
    bool vertical = true;              // along y before the turn, or along x
    double axisDeg = 0.0;              // linearMotion's `angle` argument: the axis's turn
    double startFrom = 0.0;            // the start angle is drawn uniform on [startFrom, startTo]
    double startTo = 0.0;

    bool Names(const std::string& bareEntity) const;
};

// One row of motions.json's `placed`: what one placed picture's own callback does
// to it every frame. Its linearMotion takes the NODE's speed and stride (the
// custom data the level gives it), so the row holds the rest of the call.
struct Placed {
    std::string entity;          // the entity name, without .ent, whose callback it is
    bool moves = false;          // it calls linearMotion
    bool vertical = true;        // that call's `vertical`
    bool axisFromNode = false;   // its `angle` is GetAngle() (the node's) plus axisAddDeg, or axisAddDeg alone
    double axisAddDeg = 0.0;
    double startAngle = 0.0;     // its `startAngle`, a constant
    bool setsAlpha = false;      // it calls SetAlpha(alpha) every frame
    double alpha = 1.0;
    double spinDegPerS = 0.0;    // AddToAngle(unitsPerSecond(this)): degrees a second, counter-clockwise; 0 none
};

// The port's motions.json.
struct Rules {
    double frameCapMs = 0.0; // unitsPerSecond's min(200, frame)
    double wrapRad = 0.0;    // 2 PI: above it, one turn is taken off
    Row crystal;             // ETHCallback_crystal's
    Row key;                 // ETHCallback_key's, while unowned and not spent
    std::vector<Placed> placed; // the placed pictures' callbacks, one entity each

    // The row whose callback a placement of this entity (without .ent) runs, or null.
    const Placed* FindPlaced(const std::string& bareEntity) const;
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

// One entity's linearMotion: what its custom data holds between calls.
struct Linear {
    double speed = 0.0;
    double stride = 0.0;
    bool vertical = true;
    double axisDeg = 0.0;
    double startAngle = 0.0;
    double angle = 0.0;   // its `angle` datum
    bool started = false; // the first call has run: `originalPos` is written
};

// A motion from its row, with the start angle it was drawn.
Linear Start(const Row& row, double startAngle);

// The angle half of one call, over a frame of `frameMs` milliseconds: the first
// call sets the start angle, and every call then steps it and takes off one turn
// once it is past wrapRad. A frame of 0 (the original's m_factor 0) leaves it.
void Advance(const Rules& rules, Linear& motion, double frameMs);

// Where the call puts the entity, less where it stood when the first call ran,
// in the level's units (+y down). Zero before the first call: until then the
// entity is where the level put it.
glm::dvec2 OffsetPx(const Linear& motion);

// A placed picture's linearMotion, from its row and its node: the node's own
// `speed` and `stride` (quoted numbers, as the converter writes custom data) and,
// for a row whose axis is the node's, the node's angle - Ethanon's, which the
// converter wrote as the Godot rotation negated in radians (tscn.py:432-433).
// False, naming the node, when either datum is missing or not a number, or the
// rotation is not a number.
bool FromNode(const Placed& row, const Tscn::Node& node, Linear& out, std::string& error);

// What AddToAngle(unitsPerSecond(degPerS)) has turned an entity by since the
// level put it down, in Ethanon's degrees (counter-clockwise on the screen).
struct Turn {
    double degPerS = 0.0;
    double turnedDeg = 0.0; // kept within one turn: the same drawn angle as the original's unbounded one
};

// One call's AddToAngle over a frame of `frameMs`: degPerS x min(cap, frame) / 1000,
// unitsPerSecond's step, so a paused frame (0) turns nothing.
void Advance(const Rules& rules, Turn& turn, double frameMs);

// randF(PI)'s stand-in: the original draws each start angle from a generator
// re-seeded from its clock, so its phases are unrecoverable and only their
// distribution is the original's. The port draws them from its own seeded
// stream, in the order the layer asks (the level's drawing order), so a level
// bobs the same on every run and no other generator's draws move.
class Phases {
public:
    explicit Phases(std::uint32_t seed) : m_engine(seed) {}
    double Next(const Row& row);

private:
    std::mt19937 m_engine;
};

// The stream's seed for one level: `base` mixed with the level's name (FNV-1a).
// Every level gets its own start angles, as every run of the original does, where
// one seed for all would give each level's first crystal the same phase as every
// other level's first; and a level's angles never depend on what was played
// before it.
std::uint32_t LevelSeed(std::uint32_t base, const std::string& level);

} // namespace MagicPortals::Motion
