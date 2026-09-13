#pragma once

// The diamonds of chapter 3: two pickups that share a carry path and share a
// role, and do entirely different things.
//
// Neither is a body. Both convert to a bare Node2D with a sprite and no collider,
// and nothing in either callback touches velocity - the bounce() an unowned one
// calls is a blink counter in ETHFramework/utilEntityEffect, not physics. So a
// diamond is state with a position and an owner, exactly as a key is, and the
// carry path is literally the key's: Carry.hpp holds it.
//
// SELECTED BY ENTITY NAME. The role table files shock_diamond.ent and
// fire_diamond.ent both under `pickup` and they are different mechanisms, so the
// role cannot choose between them and this module must. Both names are in
// data/diamonds.json rather than compiled in.
//
// WHAT THEY SHARE, and it is nearly everything:
//
// - 30 px, where a key is 26. One range serves being picked up and, for a shock
//   diamond, striking.
// - ONLY A CHARACTER may take one: the candidate loop tests isCharacter and skips
//   anything else before measuring anything. A minion is prey here, not a
//   carrier, and Game expresses that by handing in only the player.
// - The carry arm is the key's instruction for instruction - alpha 0.1, ownerID,
//   ByIDChooser, a bucket seek with a global SeekEntity behind it, and the same
//   scale(40) leash.
// - THE PICKUP AND THE PAYLOAD ARE A FRAME APART. Both callbacks branch on
//   ownerID at the top, and the unowned arm polls for a carrier, writes ownerID
//   and RETURNS - it ends in a jump to the function's exit - so nothing else
//   happens on the frame a diamond is taken. A key does all of its work in one
//   tick; these must not, and the suite pins the difference.
//
// WHAT A SHOCK DIAMOND DOES: the first minion within that same 30 px dies, and
// the diamond goes with it. One diamond, one minion. Removing the minion is NOT
// done here - Minions walks its own list every frame and a body destroyed behind
// its back would sit in it invalid - so Tick returns what it struck and Game
// hands it to Minions::Take.
//
// WHAT A FIRE DIAMOND DOES, which is not what it looks like. Its carry arm calls
// turnProjectilesIntoFireBalls every frame, and that walks every live
// projectile.ent calling burnProjectile, which is killProjectile (a bare
// DeleteEntity) followed by addFireball. So while one is carried a portal shot is
// DELETED and replaced by a fireball a frame after it leaves, and no portal is
// ever opened. It does not change where a portal opens - it takes portals away
// and gives you a ranged igniter instead.
//
// That is also the whole of why PortalManager::computePortalFinalPos has a
// hasFireDiamond arm at all. That arm returns origin + (destPos - origin) * 64,
// and because addProjectile NORMALIZES the direction the 64 cannot change the aim
// by a degree. Its only effect is to make the projectile's stored squaredDistance
// enormous, so the shot cannot reach its destiny and open a portal during the one
// frame it exists before the conversion kills it. The 64 is a guard against
// landing, not reach. The port needs no such guard: it converts the flight before
// the flight advances, which reaches the same end by ordering.
//
// The flag itself is the carrier's. The original does SetUInt('hasFireDiamond', 1)
// on the character every frame it is carried and SetUInt(..., 0) when the diamond
// is destroyed, so it is a state of the diamond and not a possession of the
// player. carrierHasFire is recomputed every tick for that reason.
//
// AND THE GUTTER MOUTH, which is what makes those levels finishable.
// gutter_mouth.ent is filed under `scenery_fx` as a "decorative drip emitter",
// and its callback seeks a fire_diamond.ent neighbour and calls destroy() on it.
// Destroying the diamond clears hasFireDiamond, so the drain is how the player
// gives the fire back and gets portals again. level29b, level30b and level31b
// each place one, and level30b puts it directly under the diamond. level28b
// places none at all, which makes picking that one up a one-way choice.
//
// Not built, and recorded in data/diamonds.json: the alpha, the idle pulse, the
// effect entities, the sounds, the earthquakes, the remake's 28 x 24 trigger box
// that the original does not use, and the drop the original does on shallLeave.

#include "sim/Carry.hpp"
#include "sim/Roles.hpp"
#include "sim/Trigger.hpp"
#include "sim/Tscn.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Diamonds {

// The port's diamonds.json. The four carry numbers are Carry::Rules', read under
// this module's own names, and range_px is the one a shock diamond shares between
// being picked up and striking. Both diamonds carry by the same numbers: the two
// callbacks' carry arms are byte for byte the same.
struct Rules : Carry::Rules {
    std::string shockName;  // the entity name that strikes a minion
    std::string fireName;   // and the one that turns portal shots into fireballs
    std::string gutterName; // gutter_mouth.ent: the only thing that destroys one
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

struct Diamond : Carry::Carried {
    std::string name;  // the node's name
    bool fire = false; // fire_diamond.ent rather than shock_diamond.ent
    bool gone = false; // it struck a minion, or a gutter mouth took it
};

// A gutter mouth: the drain that destroys a fire diamond, and so the way the
// player gets portals back. Its mouth is the node's own trigger_size at its
// trigger_offset, which is 10 x 51 in level30b and 10 x 76 in level31b - a tall
// thin slot under the sprite rather than a box on the node.
struct Gutter {
    std::string name;
    Trigger::Box box;
};

struct State {
    Rules rules;
    std::vector<Diamond> diamonds;
    std::vector<Gutter> gutters;

    int picked = 0;  // diamonds taken up, of either kind
    int struck = 0;  // minions struck, which is also shock diamonds spent
    int drowned = 0; // fire diamonds a gutter mouth destroyed

    // Whether the carrier holds a fire diamond as of this tick. RECOMPUTED every
    // tick rather than latched, because the original rewrites hasFireDiamond on
    // the character every frame the diamond is carried and clears it when the
    // diamond dies. Game reads this to turn a portal shot into a fireball.
    bool carrierHasFire = false;

    // One tick, after the physics step - a diamond follows where the step left
    // whoever carries it, as a key does.
    //
    // `carriers` is everything that may take one, which for a diamond is the
    // player alone. `prey` is every minion still standing. Returns the minion
    // bodies a shock diamond struck, which the caller must hand to Minions::Take
    // - this module never destroys one itself.
    std::vector<entt::entity> Tick(entt::registry& registry, const std::vector<entt::entity>& carriers,
                                   const std::vector<entt::entity>& prey, float dt);

    const Diamond* Find(const std::string& name) const;
    const Gutter* FindGutter(const std::string& name) const;

    // Diamonds that have not yet spent themselves.
    std::size_t Standing() const;
};

// A level's diamonds and its gutter mouths, by entity name. False, with `error`,
// for a diamond carrying no position or a gutter mouth whose trigger does not
// read; a level holding none is not an error, and most hold none.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error);

} // namespace MagicPortals::Diamonds
