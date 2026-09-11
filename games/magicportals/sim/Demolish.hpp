#pragma once

// Rolling stones, and what they break: the remake's Demolisher
// (hazards.gd:97-120), with one rule the owner settled.
//
// - A stone (role demolisher) breaks whatever breakable thing it touches.
//   Breaking takes the entity away entirely, collision included, as
//   Hazards.destroy frees the node. A stone is not a hazard: it kills nothing,
//   and the player may stand beside one (level 1-9 spawns the player 58 px from
//   one).
// - What is breakable: an entity whose metadata/breakable is set, which is all
//   the remake reads, or one whose name demolish.json lists. Eight
//   breakable_wall placements carry no flag, and that is in the original's own
//   scene data, so in the remake a stone cannot break them. The owner played the
//   original and confirmed, on 11 September, that stones broke those walls.
//   demolish.json says so beside the list.
// - Touching is judged as the port judges its triggers (Trigger.hpp), in its
//   own tick after the step. The test is the stone's circle against the
//   breakable's box, grown by demolish.json's contact margin, because a solver
//   leaves a stone resting against a wall touching it rather than overlapping
//   it. The box is the shape's bounds, so a breakable_wall's octagon is met up to
//   its corners' 1.5 x 6.3 px chamfer early.
// - Quarter turns only. A breakable turned by any other angle is refused,
//   because its box would not be its shape. Every breakable in the game is
//   turned by 0 or by a quarter turn.
// - What the remake's launcher would throw is step 7. Levels 23 and 24 have a
//   breakable wall and no stone until then.

#include "sim/LevelBuilder.hpp"
#include "sim/Roles.hpp"
#include "sim/Trigger.hpp"
#include "sim/Tscn.hpp"

#include <string>
#include <vector>

#include <entt/entt.hpp>

namespace MagicPortals::Demolish {

// The port's demolish.json.
struct Rules {
    std::vector<std::string> breakableNames; // entity names breakable without the flag
    double contactMarginPx = 0.0;
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

struct Stone {
    std::string name;
    entt::entity body = entt::null;
};

struct Breakable {
    std::string name;
    entt::entity body = entt::null;
    Trigger::Box box;     // the shape's bounds, in world metres, not grown
    bool flagged = false; // by metadata/breakable, rather than by name
    bool broken = false;
    std::string brokenBy; // the stone that broke it
};

struct State {
    Rules rules;
    std::vector<Stone> stones;
    std::vector<Breakable> breakables;

    // One tick, after the physics step: each stone breaks what it touches.
    void Tick(entt::registry& registry);

    const Stone* FindStone(const std::string& name) const;
    const Breakable* FindBreakable(const std::string& name) const;
    int Broken() const;
};

// A level's stones and breakables, each with the body LevelBuilder built for it.
// A breakable with no body built is passed over, which is every static one when
// the level is started without its statics. False, with `error`, for a stone
// with no body, or a breakable turned by anything but a quarter turn.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, const Rules& rules,
          State& out, std::string& error);

} // namespace MagicPortals::Demolish
