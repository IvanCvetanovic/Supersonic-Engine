#pragma once

// The shock diamond: the pickup a character carries, and the minion that dies to
// it. Chapter 3's answer to a minion you cannot otherwise get past.
//
// It is not a body. shock_diamond.ent converts to a bare Node2D with a sprite and
// no collider, and nothing in its callback touches velocity - the bounce() an
// unowned one calls is a blink counter in ETHFramework/utilEntityEffect, not
// physics. So a diamond is state with a position and an owner, exactly as a key
// is, and the carry path is literally the key's: Carry.hpp holds it.
//
// What this module is, beyond that shared path:
//
// - SELECTED BY ENTITY NAME. The role table files shock_diamond.ent and
//   fire_diamond.ent both under `pickup`, and they are different mechanisms.
//   Selecting on the role would give a fire diamond this behaviour in the four
//   chapter-3 levels that place one. Both names live in data/diamonds.json.
// - 30 px, where a key is 26, and the one value serves both being picked up and
//   striking.
// - ONLY A CHARACTER may take one: the candidate loop tests isCharacter and skips
//   anything else before measuring anything. A minion is prey here, not a
//   carrier, and Game expresses that by handing in only the player.
// - THE PICKUP AND THE STRIKE ARE A FRAME APART. The callback branches on ownerID
//   at the top. The unowned arm polls for a carrier, writes ownerID and returns -
//   it ends in a jump to the function's exit - so nothing else happens on the
//   frame a diamond is taken. The payload lives in the carried arm, which that
//   top-level branch reaches on the next frame. A key does all of its work in one
//   tick; this must not, and the suite pins the difference.
// - The strike destroys the minion and then the diamond deletes itself. One
//   diamond, one minion.
//
// Removal of the minion is NOT done here. The original calls the same destroy()
// the killer floor calls, and in this port minion removal belongs to Minions,
// which walks its own list every frame: a minion destroyed behind its back would
// sit in that list with an invalid body. So Tick returns what it struck and Game
// hands it to Minions::Take.
//
// Not built, and recorded in data/diamonds.json: the alpha, the idle pulse, the
// three effect entities, both sounds, both earthquakes, the remake's 28 x 24
// trigger box that the original does not use, and the fire diamond entire.

#include "sim/Carry.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Diamonds {

// The port's diamonds.json. The four carry numbers are Carry::Rules', read under
// this module's own names, and range_px is the one a shock diamond shares between
// being picked up and striking.
struct Rules : Carry::Rules {
    std::string shockName; // the entity name this module plays
    std::string fireName;  // and the one it deliberately does not
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

struct Diamond : Carry::Carried {
    std::string name;   // the node's name
    bool gone = false;  // it struck a minion, and deleted itself with it
};

struct State {
    Rules rules;
    std::vector<Diamond> diamonds;

    int picked = 0; // diamonds taken up
    int struck = 0; // minions struck, which is also diamonds spent

    // One tick, after the physics step - a diamond follows where the step left
    // whoever carries it, as a key does.
    //
    // `carriers` is everything that may take one, which for a diamond is the
    // player alone. `prey` is every minion still standing. Returns the minion
    // bodies it struck, which the caller must hand to Minions::Take - this
    // module never destroys one itself.
    std::vector<entt::entity> Tick(entt::registry& registry, const std::vector<entt::entity>& carriers,
                                   const std::vector<entt::entity>& prey, float dt);

    const Diamond* Find(const std::string& name) const;

    // Diamonds that have not yet spent themselves.
    std::size_t Standing() const;
};

// A level's shock diamonds, by entity name. False, with `error`, for one carrying
// no position; a level holding none is not an error, and most hold none.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error);

} // namespace MagicPortals::Diamonds
