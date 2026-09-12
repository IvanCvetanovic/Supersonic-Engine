#pragma once

// Portals: portal_system.gd's placement and traversal (:69-282), on the transit
// arithmetic in Portal.hpp.
//
// - A tap fires a shot (Shot.hpp) from the player's centre toward the tap. It
//   flies at shot.json's speed, and fails on the first solid body or
//   projectile blocker it meets. Where it arrives a portal opens, a circle,
//   against a budget. The budget is the level's max_portals or portals.json's
//   default, and never more than a pair. At the cap the oldest portal gives
//   way, or with that switched off the shot fails. A shot that arrives inside
//   a no-portal zone fails too. A failed shot costs nothing. One shot flies at
//   a time.
// - A level may also ship static portals (portal_static), which pair by index.
// - Whatever comes into a portal goes out of its partner, as the original sends
//   it (transit.json, decoded): at the partner itself, its velocity turned back,
//   a body's whole and the player's in y only (Portal::ExitVelocity). Placed
//   ends are then spent; static ends stay. For a while after each traversal,
//   entries are ignored.
// - Only the player travels, and bodies whose entity says `teleportable` 1. The
//   inline crate says 0.
// - Entries are per portal and per body, like an Area2D's body_entered. So a
//   portal placed over a crate sees the crate come in on the next tick, and a
//   traveller put down inside its exit does not go back until it has left.

#include "sim/LevelBuilder.hpp"
#include "sim/Mover.hpp"
#include "sim/Portal.hpp"
#include "sim/Roles.hpp"
#include "sim/Shot.hpp"
#include "sim/Trigger.hpp"
#include "sim/Tscn.hpp"

#include <optional>
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
    int defaultMaxPortals = 0; // owner-confirmed, despite its block's _guess

    // TWO RADII, because the original has two and they are unrelated. The port
    // carried one - the remake's collision_radius_px, a guess of 16 - and used
    // it for both, which made every no-portal field a quarter of its size.
    // Both of these are decoded, from the port's own placement.json.
    //
    // A placed portal's own circle: g_portalCollisionRadius, 14.
    double entryRadiusPx = 0.0;
    // And the circle an antiportal refuses a tap in, TIMES the node's own scale:
    // half of white_ring.png's 128 px frame, because the original's test is
    // GetSize().x * 0.5 on the field entity itself. placement.json says why.
    double antiportalRadiusPx = 0.0;

    bool consumeOnTraverse = false;    // owner-confirmed
    bool staticPortalsPersist = false; // owner-confirmed 11 September: they stay
    bool recycleOldestAtCap = false;   // _guess
    Portal::Transit transit;           // _guess, every field
};

// The remake's portals.json: its placement, consumption and transit objects.
// Its collision_radius_px is read and checked so a malformed file is still
// refused, but nothing plays by it - LoadPlacement replaces it.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// The port's own placement.json, over the top: the two decoded radii. Both must
// be there and above zero. A missing one is an error rather than a default,
// because a default here would be a guess written into code - which is the
// mistake this file exists to undo.
bool LoadPlacement(const std::string& path, Rules& out, std::string& error);

// An antiportal. A tap within collision_radius_px times its `scale` is refused
// (portal_system.gd:136-146). An anti_portal_agent with a speed and a stride
// patrols: it swings as a moving platform does, on the axis its `direction`
// names, vertical where it names none (behaviours.gd:48-51, 113-127). Three in
// the game do, two in level10 and one in level13b.
//
// In level10 every agent stands on a static antiportal of its own, whose field
// holds the agent's whole swing. So there the patrol never changes where a tap
// is refused. The port keeps the remake's two zones as they are; that the
// original may carry the field with its agent is recorded in the remaster doc,
// not guessed at here.
struct NoPortalZone {
    glm::dvec2 centrePx{0.0}; // where the level places it
    double scale = 1.0;
    bool moving = false;
    Mover::Oscillation motion; // when it patrols
    std::string name;          // last, so {centre, scale} still makes a zone

    glm::dvec2 CentreNowPx() const { return moving ? motion.AtPx() : centrePx; }
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
//
// That partnership runs both ways, which the port missed until the game was
// played: entering the lone placement sends the traveller to the first static
// portal, as PortalManager::teleportToFirstStaticPortal does on a level whose
// maxPortals is 1 (Portals.cpp, where Tick pairs an end with its exit).
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

// A projectile blocker: anti_projectile_wall.ent, an invisible box a portal shot
// dies in (ETHCallback_anti_projectile_wall). Its box is its trigger_size, as
// the remake's ProjectileBlocker reads it. The player walks through it.
struct Blocker {
    std::string name;
    Trigger::Box box;
};

// A reflector: reflect_agent, a marker with no body that a portal shot comes
// off, mirrored across its plane (the owner, 11 September). How near a shot has
// to pass, and how many reflectors one shot comes off, are shot.json's guesses.
// The shot goes on for the rest of the distance it was fired, so a flat
// reflector sends it to the tap mirrored across it.
struct Reflector {
    std::string name;
    glm::dvec2 centrePx{0.0};
    bool vertical = false; // its plane: a vertical one turns a shot's x back, a horizontal one its y
};

// A portal shot on its way.
struct Flight {
    glm::dvec2 fromPx{0.0}; // the player's centre when it was fired
    glm::dvec2 toPx{0.0};   // where it opens a portal: the tap, mirrored by each reflector it came off
    glm::dvec2 atPx{0.0};   // where it is now
    int reflections = 0;    // how many reflectors it has come off
    int lastReflector = -1; // the last of them, which cannot turn it again at once
};

struct State {
    Rules rules;
    Shot::Rules shot;
    int budget = 0;
    entt::entity shooter = entt::null; // the player: where a shot leaves from, and what it passes through
    std::vector<NoPortalZone> zones;
    std::vector<Blocker> blockers;
    std::vector<Reflector> reflectors;
    std::vector<entt::entity> travellers; // the player, and every teleportable body
    std::vector<Static> statics;          // in the level's order
    std::vector<Placed> placed;           // oldest first
    int portalsUsed = 0;                  // every portal placed: what the golden score counts
    int traversals = 0;
    float lockoutS = 0.0f;
    std::optional<Flight> flight;
    int shotsFired = 0;
    int shotsFailed = 0;
    int reflections = 0;     // every time a shot came off a reflector
    std::string lastFailure; // what stopped the last failed shot

    // A tap at a point in the level: a shot fired toward it. False when none
    // goes, because the level allows no portal or a shot is already flying.
    bool Shoot(entt::registry& registry, const glm::dvec2& atPx);

    // A portal opened at a point at once, as an arriving shot opens one. The
    // suites use it to put portals where a test needs them. False when refused.
    bool TryPlace(const glm::dvec2& atPx);

    const NoPortalZone* FindZone(const std::string& name) const;

    // One tick, after the physics step. The lockout runs down, a patrolling zone
    // swings on, a shot flies on, and a traveller newly inside a portal goes out
    // of its partner.
    void Tick(entt::registry& registry, float dt);
};

// A built level's portals, no-portal zones and projectile blockers. A
// patrolling zone swings at movers.json's rate scale, and the player's shots fly
// by shot.json. False, with `error`, for data that does not read.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built,
          entt::registry& registry, entt::entity player, const Rules& rules, const Mover::Rules& movers,
          const Shot::Rules& shot, State& out, std::string& error);

} // namespace MagicPortals::Portals
