#pragma once

// A level as the port plays it, and the tick it plays on. Both live here, so
// the suites and the layer run the same thing.
//
// The tick is in two halves around the physics step:
//   before the step: the buttons and their doors (Puzzle), the moving platforms
//                    and lifts (Mover), what the launchers throw (Launchers),
//                    what the carrancas spit (Turrets), how fast the beholder's
//                    rocks go (Boss), then the player's steering (Player);
//   after the step:  the crystals and the exit, where the step left the player
//                    (Goals), the hazards (Hazards), where the fireballs flew
//                    and whom they burned (Turrets), what the stones touched
//                    (Demolish), the thrown bodies the launchers take back
//                    (Launchers), what the beholder's rocks ran into (Boss),
//                    then the portals (Portals), and last the beholder's own turn
//                    (Boss). The portals come after what judges where the step
//                    left things, because they move what goes through them. The
//                    beholder looks at where they left its rocks.
// A test runs Tick, which is the two halves with the step between them. The
// engine's app steps physics itself, before each layer's OnFixedUpdate. So a
// layer runs AfterStep and then BeforeStep, and the app's step falls between
// one OnFixedUpdate and the next. It is the same sequence, begun half a tick
// later.

#include "sim/Boss.hpp"
#include "sim/Demolish.hpp"
#include "sim/Fire.hpp"
#include "sim/Goals.hpp"
#include "sim/Hazards.hpp"
#include "sim/Launchers.hpp"
#include "sim/LevelBuilder.hpp"
#include "sim/Player.hpp"
#include "sim/Portals.hpp"
#include "sim/Puzzle.hpp"
#include "sim/Roles.hpp"
#include "sim/Tscn.hpp"
#include "sim/Turrets.hpp"

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
    Portals::Rules portals;       // the remake's portals.json, with the port's transit.json
    Mover::Rules movers;          // the port's own movers.json
    Demolish::Rules demolish;     // and demolish.json
    Launchers::Rules launchers;   // and launchers.json
    Shot::Rules shot;             // and shot.json
    Boss::Rules boss;             // and boss.json
    Turrets::Rules turrets;       // and turrets.json
    Fire::Rules fire;             // and fire.json
    std::filesystem::path prisms; // where LevelBuilder writes the platforms' prisms
};

// A converted level (.tscn), the remake's data directory with its
// entity_roles.json, player.json and portals.json, and the port's own data with
// its movers.json, demolish.json, launchers.json, shot.json, transit.json and boss.json. False, with
// `error`, when any of them is missing or malformed.
bool LoadData(const std::string& levelPath, const std::string& dataDirectory,
              const std::filesystem::path& prismDirectory, Data& out, std::string& error,
              const std::string& portDataDirectory = MAGICPORTALS_PORT_DATA_DIR);

// The remake's world gravity, 980 px/s^2, as the registry's PhysicsSettings.
// Units.hpp says why it is that value.
void UseRemakeGravity(entt::registry& registry);

struct Level {
    LevelBuilder::Built built;
    Puzzle::Channels channels;
    Mover::Movers movers;
    Goals::State goals;
    Hazards::State hazards;
    Demolish::State demolish;
    Launchers::State launchers;
    Portals::State portals;
    Boss::State boss;
    Turrets::State turrets;
    Fire::State fire;
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
