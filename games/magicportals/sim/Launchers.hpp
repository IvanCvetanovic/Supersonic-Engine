#pragma once

// Launchers: entity_launcher, which drops a rolling stone into levels 23, 24
// and 25 of chapter 1 on a timer. The remake's Launcher only announces what it
// would throw (hazards.gd:416-455), because its converter emits levels, not
// entities a game could spawn. The port builds the body itself.
//
// - A launcher throws the entity its `entity` names, at its node, every
//   `stride` ms. Every placement in the game throws rolling_stone.ent. The first
//   throw comes launchers.json's first delay after the level starts; that delay
//   is a guess.
// - What a thrown entity is comes from launchers.json, which records it from
//   the entity's .ent. rolling_stone.ent is a rigid circle, 30 px in radius,
//   that demolishes and travels. So a thrown stone breaks walls (Demolish) and
//   goes through portals (Portals), as a placed one does. The caller hands each
//   new body to both.
// - A launcher's min/max is a cull box, which is the remake's reading
//   (entity_roles.json). A thrown body whose centre leaves the box is removed.
//   So is one that touches an entity launchers.json names as a despawner: that
//   is destroyier.ent, in the box the port gives it as a hazard (Hazards.hpp).
// - Only thrown bodies are removed. A stone a level places is never culled.

#include "sim/LevelBuilder.hpp"
#include "sim/Roles.hpp"
#include "sim/Trigger.hpp"
#include "sim/Tscn.hpp"

#include <map>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace MagicPortals::Launchers {

struct Throwable {
    double radiusPx = 0.0;
    bool demolisher = false;
    bool teleportable = false;
    std::string sprite; // the image its .ent shows, a file among the converted entities' art
};

// The port's launchers.json.
struct Rules {
    std::map<std::string, Throwable> throwables; // by the entity name a launcher throws
    std::vector<std::string> despawners;         // entity names whose box removes a thrown body
    double firstThrowStrides = 0.0;              // _guess
};

bool LoadRules(const std::string& path, Rules& out, std::string& error);

struct Launcher {
    std::string name;
    glm::dvec2 atPx{0.0};
    std::string entity; // what it throws
    Throwable throws;
    double strideS = 0.0;
    glm::dvec2 minPx{0.0}; // the cull box
    glm::dvec2 maxPx{0.0};
    double leftS = 0.0; // until the next throw
    int thrown = 0;
};

struct Thrown {
    std::string name; // "<launcher>#<n>", counting from 1
    entt::entity body = entt::null;
    std::size_t launcher = 0; // its place in `launchers`
    Throwable is;
};

struct Despawner {
    std::string name;
    Trigger::Box box;
};

struct State {
    std::vector<Launcher> launchers;
    std::vector<Despawner> despawners;
    std::vector<Thrown> live; // thrown and not yet removed, oldest first
    int removed = 0;

    // Before the step: each launcher whose time has come throws. Returns what
    // was thrown this tick, which is also in `live`.
    std::vector<Thrown> Throw(entt::registry& registry, float dt);

    // After the step: a thrown body out of its launcher's box, or in a
    // despawner's, is destroyed. Returns the bodies destroyed, for the caller to
    // forget.
    std::vector<entt::entity> Cull(entt::registry& registry);
};

// A level's launchers and despawners. False, with `error`, for a launcher whose
// data does not read, or one that throws an entity launchers.json does not
// describe.
bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error);

} // namespace MagicPortals::Launchers
