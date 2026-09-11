#pragma once

// A level as the port plays it, and the tick it plays on. Both live here, so
// the suites and the layer run the same thing.
//
// The tick is in two halves around the physics step:
//   before the step: the buttons and their doors (Puzzle), then the player's
//                    steering (Player);
//   after the step:  the crystals and the exit, where the step left the player
//                    (Goals), then the portals (Portals).
// A test runs Tick, which is the two halves with the step between them. The
// engine's app steps physics itself, before each layer's OnFixedUpdate. So a
// layer runs AfterStep and then BeforeStep, and the app's step falls between
// one OnFixedUpdate and the next. It is the same sequence, begun half a tick
// later.

#include "sim/Goals.hpp"
#include "sim/LevelBuilder.hpp"
#include "sim/Player.hpp"
#include "sim/Portals.hpp"
#include "sim/Puzzle.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"

#include <filesystem>
#include <string>

#include <entt/entt.hpp>

namespace MagicPortals::Game {

// What a level needs from outside the port: the converted level and the
// remake's own data. Read once and shared.
struct Data {
    Tscn::Scene scene;
    Roles::Table roles;
    Player::Tuning tuning;
    Goals::Rules goals;
    Portals::Rules portals;
    std::filesystem::path prisms; // where LevelBuilder writes the platforms' prisms
};

// A converted level (.tscn), and the remake's data directory with its
// entity_roles.json, player.json and portals.json. False, with `error`, when
// any of them is missing or malformed.
bool LoadData(const std::string& levelPath, const std::string& dataDirectory,
              const std::filesystem::path& prismDirectory, Data& out, std::string& error);

// The remake's world gravity, 980 px/s^2, as the registry's PhysicsSettings.
// Units.hpp says why it is that value.
void UseRemakeGravity(entt::registry& registry);

struct Level {
    LevelBuilder::Built built;
    Puzzle::Channels channels;
    Goals::State goals;
    Portals::State portals;
    entt::entity player = entt::null;
};

// Puts the level into the registry: the remake's gravity, the bodies built with
// the role table, and the player at the level's player_spawn. Then its buttons,
// doors, crystals, exit and portals are found. withStatics false leaves the
// statics out, which is the landing check's mutation. That leaves out the doors
// too, so there is nothing to wire.
bool Start(const Data& data, entt::registry& registry, Level& out, std::string& error, bool withStatics = true);

void BeforeStep(const Data& data, entt::registry& registry, Level& level, float direction, float dt);
void AfterStep(entt::registry& registry, Level& level, float dt);

// One whole tick, for a caller that steps physics itself.
void Tick(const Data& data, entt::registry& registry, Level& level, float direction, float dt);

} // namespace MagicPortals::Game
