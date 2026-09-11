#pragma once

// Placed portals: portal_system.gd's placement and traversal (:101-282), on the
// transit arithmetic in Portal.hpp.
//
// - A tap places a portal, a circle, against a budget. The budget is the level's
//   max_portals or portals.json's default, and never more than a pair. At the
//   cap the oldest portal gives way, or with that switched off the tap is
//   refused. A tap inside a no-portal zone is refused.
// - Whatever comes into a placed portal goes out of the other. Its velocity is
//   carried through (Portal::ExitVelocity), it is put clear of the exit
//   (Portal::ExitPosition), and the pair is spent. For a while after each
//   traversal, entries are ignored.
// - Only the player travels, and bodies whose entity says `teleportable` 1. The
//   inline crate says 0.
// - Entries are per portal and per body, like an Area2D's body_entered. So a
//   portal placed over a crate sees the crate come in on the next tick.

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
    bool staticPortalsPersist = false; // reasoned, not confirmed; no static portal is ported
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
    glm::dvec2 atPx{0.0};
    Trigger::Circle trigger;
    std::vector<entt::entity> inside; // the travellers overlapping it as of the last tick
};

struct State {
    Rules rules;
    int budget = 0;
    std::vector<NoPortalZone> zones;
    std::vector<entt::entity> travellers; // the player, and every teleportable body
    std::vector<Placed> placed;           // oldest first
    int portalsUsed = 0;                  // every portal placed: what the golden score counts
    int traversals = 0;
    float lockoutS = 0.0f;

    // A tap at a point in the level. False when refused.
    bool TryPlace(const glm::dvec2& atPx);

    // One tick, after the physics step. The lockout runs down, and a traveller
    // newly inside a placed portal goes out of the other.
    void Tick(entt::registry& registry, float dt);
};

// A built level's portals. False, with `error`, for what is not ported: static
// portals, and no-portal zones that move. level30 has neither.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built,
          entt::registry& registry, entt::entity player, const Rules& rules, State& out, std::string& error);

} // namespace MagicPortals::Portals
