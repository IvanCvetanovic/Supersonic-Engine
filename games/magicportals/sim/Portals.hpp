#pragma once

// Portals: portal_system.gd's placement and traversal (:69-282), on the transit
// arithmetic in Portal.hpp.
//
// - A tap places a portal, a circle, against a budget. The budget is the level's
//   max_portals or portals.json's default, and never more than a pair. At the
//   cap the oldest portal gives way, or with that switched off the tap is
//   refused. A tap inside a no-portal zone is refused.
// - A level may also ship static portals (portal_static), which pair by index.
// - Whatever comes into a portal goes out of its partner. Its velocity is
//   carried through (Portal::ExitVelocity), and it is put clear of the exit
//   (Portal::ExitPosition). Placed ends are then spent; static ends stay. For a
//   while after each traversal, entries are ignored.
// - Only the player travels, and bodies whose entity says `teleportable` 1. The
//   inline crate says 0.
// - Entries are per portal and per body, like an Area2D's body_entered. So a
//   portal placed over a crate sees the crate come in on the next tick, and a
//   traveller put down inside its exit does not go back until it has left.

#include "sim/LevelBuilder.hpp"
#include "sim/Portal.hpp"
#include "sim/Roles.hpp"
#include "sim/Trigger.hpp"
#include "sim/Tscn.hpp"

#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Portals {

// The hard cap on placed portals (portal_system.gd:22-24). A level may allow
// fewer, never more. The owner confirmed it, as for portals.json's default of 2.
inline constexpr int kPairSize = 2;

// portals.json's "placement", "consumption" and "transit" objects, read strictly.
// Their markers differ within a block, so each field says which it has.
struct Rules {
    int defaultMaxPortals = 0;         // owner-confirmed, despite its block's _guess
    double collisionRadiusPx = 0.0;    // _guess
    bool consumeOnTraverse = false;    // owner-confirmed
    bool staticPortalsPersist = false; // owner-confirmed 11 September: they stay
    bool recycleOldestAtCap = false;   // _guess
    Portal::Transit transit;           // _guess, every field
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

// An antiportal. A tap within collision_radius_px times its `scale` is refused
// (portal_system.gd:136-146).
struct NoPortalZone {
    glm::dvec2 centrePx{0.0};
    double scale = 1.0;
};

struct Placed {
    int id = 0; // its place in the count of portals placed, which names it once others go
    glm::dvec2 atPx{0.0};
    Trigger::Circle trigger;
    std::vector<entt::entity> inside; // the travellers overlapping it as of the last tick
};

// A pre-placed portal (portal_system.gd:79-87, 206-227). `destiny` is the index
// it sends a traveller to. When no static portal has that index the traveller
// goes to a placed portal instead: level1 ships one static portal and grants one
// placement, and the placement is its partner (docs/original-gameplay.md 2.3).
struct Static {
    std::string name;
    int index = 0;
    int destiny = 0;
    bool hasDestiny = false;
    std::string colour;   // "red" or "blue", as the level says; for drawing
    glm::dvec2 atPx{0.0}; // the node's position: where a traveller comes out
    Trigger::Box trigger; // its trigger_size box: where one goes in
    bool live = true;     // false once spent, when static portals do not persist
    std::vector<entt::entity> inside;
};

struct State {
    Rules rules;
    int budget = 0;
    std::vector<NoPortalZone> zones;
    std::vector<entt::entity> travellers; // the player, and every teleportable body
    std::vector<Static> statics;          // in the level's order
    std::vector<Placed> placed;           // oldest first
    int portalsUsed = 0;                  // every portal placed: what the golden score counts
    int traversals = 0;
    float lockoutS = 0.0f;

    // A tap at a point in the level. False when refused.
    bool TryPlace(const glm::dvec2& atPx);

    // One tick, after the physics step. The lockout runs down, and a traveller
    // newly inside a portal goes out of its partner.
    void Tick(entt::registry& registry, float dt);
};

// A built level's portals. False, with `error`, for what is not ported: no-portal
// zones that move.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built,
          entt::registry& registry, entt::entity player, const Rules& rules, State& out, std::string& error);

} // namespace MagicPortals::Portals
