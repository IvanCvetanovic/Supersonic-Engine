#pragma once

// What kills the player: LevelRuntime's hazard trigger (level_runtime.gd:270-281),
// and what main.gd does about it (:155-158), which is to retry the level at once.
//
// - A hazard's trigger is the box LevelRuntime._attach_trigger builds for it:
//   metadata/trigger_size, or the 16 px fallback (Trigger::FromNode). The remake
//   never reads the Area2D the converter gives a death_area or a destroyier, and
//   neither carries a trigger_size, so each kills only in the fallback box at its
//   node. level5's death_area is a 60 x 16 octagon, and in the remake only its
//   middle 16 x 16 kills. The port does as the remake does, as it does for
//   buttons. The original's sensor would fire on the whole shape; that is
//   recorded in the remaster doc, not guessed at here.
// - Only the player, and on entry, as body_entered fires. So a player put down
//   inside one dies on the first tick.
// - destroyier.ent also despawns what a launcher throws (hazards.gd:420-422). The
//   remake's launcher throws nothing, and the port's has not been built, so here
//   a destroyier is a hazard and nothing more.

#include "sim/Roles.hpp"
#include "sim/Trigger.hpp"
#include "sim/Tscn.hpp"

#include <string>
#include <vector>

#include <entt/entt.hpp>

namespace MagicPortals::Hazards {

// The port's hazards.json: what the remake's role table calls a hazard but the
// original does not. enemy_killer is filed under `hazard` there, beside
// death_area.ent and lava, and it kills no player at all - see the file, and
// Minions.hpp, which is what does act on it.
struct Rules {
    std::vector<std::string> notHazardNames;

    // How far past the level's own extent the player may go before it has
    // fallen out of the world: hazards.json's `bounds.margin_px`, which is the
    // original's `size * 2` at both ends. See that file for the decode.
    glm::dvec2 boundsMarginPx{0.0};
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

struct Hazard {
    std::string name;
    Trigger::Box box;
    bool playerInside = false; // as of the last tick, to find the next entry
};

struct State {
    std::vector<Hazard> hazards;
    bool playerDied = false;
    std::string killedBy; // the hazard the player died in

    // THE LEVEL'S EDGE, which is a death and not a hazard.
    //
    // checkGameLost tests the player against the level's extent every frame,
    // and this port tested nothing - so a player who left a level fell for
    // ever. The extent is the level_bounds marker Camera and Boss already read;
    // the margin is hazards.json's. A level with no marker does not test, which
    // is the same refusal Boss::Find makes rather than inventing an extent.
    glm::dvec2 boundsPx{0.0};
    glm::dvec2 marginPx{0.0};
    bool haveBounds = false;

    // Which of the two deaths it was. The original plays playDieByFallSound the
    // moment it happens and playDeathSound only when the lost screen goes up;
    // an hp death has no sound of its own at all.
    bool diedByFalling = false;

    // One tick, after the physics step: a player newly inside a hazard dies,
    // and so does one that has left the level.
    void Tick(entt::registry& registry, entt::entity player);

    const Hazard* FindHazard(const std::string& name) const;
};

// Every hazard of a level. False, with `error`, for a rotated one, whose box
// would turn with it.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error);

} // namespace MagicPortals::Hazards
