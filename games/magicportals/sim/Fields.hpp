#pragma once

// Shock fields: the lethal ring of chapter 3, and the only thing in the game that
// kills the player by standing still.
//
// The original writes TWO entities and the port models ONE. A placed shock_agent
// is a one-shot builder - guarded by an areaAdded flag - which spawns a separate
// shock_area.ent at its own position, sizes it to the diameter, and writes the
// radius, speed, stride and direction onto it. The AREA holds the lethal radius,
// the AGENT is the sprite, and from then on both oscillate with the same numbers
// from the same seed. So one moving point serves for both; data/fields.json
// works out the one frame of phase between them, which is 0.03 px at the only
// moving placement in the game.
//
// What matters, and what a careless port gets wrong:
//
// - THE NODE IS THE CENTRE, NOT THE START. angle seeds at 0 and the displacement
//   is cos(angle) * stride * sign(speed), so the ring stands a FULL STRIDE from
//   its node on the first frame and swings through the node to the far side. The
//   suite asserts the displacement, so treating the node as the starting position
//   cannot pass.
// - ONE range, areaRadius, per placement - 28 to 105 across the game. The kill is
//   squaredDistance(player, ring) < that, then hp = 0.
// - It POLLS EVERY FRAME. A hazard fires on entry (body_entered) and remembers
//   whether the player was already inside; this does not. A player standing in a
//   ring dies whether it walked in or was put there.
// - MOVEMENT IS ALWAYS VERTICAL. metadata/direction is read off the agent and
//   copied onto the area by the original, and then neither callback ever consults
//   it: both pass a hardcoded flag to linearMotion. Recorded, not implemented.
// - THE CONVERTER'S COLLIDER IS NOT THE RING. Every agent node carries an Area2D
//   with a 30 px CircleShape2D - the same 30 everywhere, against radii of 28 to
//   105. LevelBuilder makes it a trigger, so it is a sensor and not a platform.
//   The port moves that entity with the field so the sensor and the sprite follow
//   it, and takes the radius from areaRadius alone.
//
// Killing is REPORTED here and folded into Hazards by Game::AfterStep, as the
// boss's, the carrancas' and the fire's are, so there is one death in the port
// and not four.
//
// Not built, and recorded in data/fields.json: the spin, the throb, the tint, the
// z offset, the bounce pulse, the sound and the death entity. And the burnable
// flag 11 placements carry, which needs no wiring: Fire sets `burned` on such an
// agent and never removes it, because removal is gated on fire.json's fade_names
// and those are the crates.

#include "sim/LevelBuilder.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Fields {

// The port's fields.json. There is no tuning here and that is the original's
// doing: every number a ring has is on the placement. What this carries is the
// validation floor, for the reason every other LoadRules carries one.
struct Rules {
    double minRadiusPx = 0.0;
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

struct Field {
    std::string name;
    glm::dvec2 centrePx{0.0, 0.0}; // the node: the CENTRE of the swing
    glm::dvec2 atPx{0.0, 0.0};     // where the ring is now
    double radiusPx = 0.0;         // metadata/areaRadius, and the whole of the kill
    double speedRadPerSec = 0.0;   // metadata/speed, radians a second; 0 stands still
    double stridePx = 0.0;         // metadata/stride, the amplitude
    double angleRad = 0.0;         // seeded at 0, as the original seeds it
    bool burnable = false;         // carried, and nothing in the port acts on it
    std::string direction;         // copied by the original and never read by it
    entt::entity body = entt::null; // the 30 px trigger the converter gives it
    // A fireball destroyed it. ETHBeginContactCallback_fireball tests the name
    // shock_agent / shock_agent.ent against what it touched and, alone among the
    // sensors it meets, destroys that one and itself with it. A destroyed ring
    // stops swinging and stops killing.
    bool gone = false;
};

struct State {
    Rules rules;
    std::vector<Field> fields;

    // Reported, not acted on: Game::AfterStep folds this into Hazards.
    bool playerKilled = false;
    std::string killedBy;

    // One tick, after the physics step: each ring swings, then judges where the
    // step left the player.
    void Tick(entt::registry& registry, entt::entity player, float dt);

    // A fireball destroyed a shock agent. Marks it gone and hands back the
    // trigger body the converter gave it, for Game to pass through Forget;
    // entt::null for a name that is not a ring here or is already gone.
    entt::entity Destroy(const std::string& name, entt::registry& registry);

    const Field* FindField(const std::string& name) const;

    // Rings that move at all, which is one placement in every level that starts.
    std::size_t Moving() const;

    // Rings still standing: destroyed ones stay in the list, as spent diamonds do.
    std::size_t Standing() const;
};

// A level's shock agents. False, with `error`, for one with no position, a radius
// under the floor, or a speed and a stride that disagree about whether it moves.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, const Rules& rules,
          State& out, std::string& error);

} // namespace MagicPortals::Fields
