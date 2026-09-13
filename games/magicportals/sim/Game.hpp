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
#include "sim/Bounce.hpp"
#include "sim/DarkDragon.hpp"
#include "sim/Demolish.hpp"
#include "sim/Diamonds.hpp"
#include "sim/Dragon.hpp"
#include "sim/Fields.hpp"
#include "sim/Fire.hpp"
#include "sim/Ghost.hpp"
#include "sim/Goals.hpp"
#include "sim/GravityWell.hpp"
#include "sim/Hazards.hpp"
#include "sim/Hinge.hpp"
#include "sim/Launchers.hpp"
#include "sim/Keys.hpp"
#include "sim/LevelBuilder.hpp"
#include "sim/Minions.hpp"
#include "sim/Player.hpp"
#include "sim/Portals.hpp"
#include "sim/Puzzle.hpp"
#include "sim/Roles.hpp"
#include "sim/Torch.hpp"
#include "sim/Tscn.hpp"
#include "sim/Turrets.hpp"
#include "sim/Zerog.hpp"

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
    Hinge::Rules hinge;           // and hinge.json
    Minions::Rules minions;       // and minions.json
    Keys::Rules keys;             // and keys.json
    Diamonds::Rules diamonds;     // and diamonds.json
    Fields::Rules fields;         // and fields.json
    Ghost::Rules ghost;           // and ghost.json
    Dragon::Rules dragon;         // and dragon.json
    DarkDragon::Rules darkDragon; // and darkdragon.json
    Torch::Rules torch;           // and torch.json
    Zerog::Rules zerog;           // and zerog.json
    Bounce::Rules bounce;         // and bounce.json
    GravityWell::Rules wells;     // and gravitywell.json
    Hazards::Rules hazards;       // and hazards.json, which is what the role table got wrong
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

// And none at all, for a level that sets `no_gravity`: the original's
// setGravity(V2_ZERO). Zerog.hpp says what else that flag turns on.
void UseNoGravity(entt::registry& registry);

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
    // Attached when the level is built and then left to the solver: Hinge.hpp
    // says why this one has no tick.
    Hinge::State hinge;
    // Spawned when the level starts, not built with it: Minions.hpp says why the
    // marker is all a level places.
    Minions::State minions;
    // Keys carried by the player or by a minion, and the doors they open.
    Keys::State keys;
    // Shock diamonds, which only the player may carry and which kill a minion.
    Diamonds::State diamonds;
    // The lethal rings, which swing about the node that placed them.
    Fields::State fields;
    // Chapter 3's boss, which the fire diamond is the weapon against.
    Ghost::State ghost;
    // Chapter 2's boss, which is the one that cannot be killed: its level is won
    // by reaching the exit while it strafes the player and its claw eats the
    // floor. It owns a camera of its own, because its level is the only one in
    // the game that sets auto_camera - and sets it to the dragon.
    Dragon::State dragon;
    // Chapter 4's boss, and the last level of the game: summoned by the player's
    // own light, hurt only by a barrel bomb its own fireball sets off, and the
    // holder of the only key its level has.
    DarkDragon::State darkDragon;
    // Chapter 4's torches, and the wall of light one of them drops.
    Torch::State torch;
    // The slabs that bob in chapter 4's weightless rooms. Kinematic, so each
    // carries what stands on it - Bounce.hpp says why that is the whole step.
    Bounce::State bounce;
    // Chapter 4's gravity wells: a solid circle that pulls every dynamic body
    // in, and refuses a portal anywhere inside its reach.
    GravityWell::State wells;
    // The level sets `darkest`: its ambient light is DARKEST_AMBIENT_LIGHT,
    // (0.01, 0.01, 0.01). Carried rather than acted on - this port has no ambient
    // light for it to change - and art.json holds the decode. Its one gameplay
    // consequence is that a minion in such a level is blind, which minion sight
    // must honour when it is built.
    bool darkest = false;
    // The level sets `no_gravity`: the world's gravity is zero, the walking
    // buttons do nothing, and the recoil of a portal shot is how the player
    // moves. Zerog.hpp holds the decode. Unlike `darkest` this one is ACTED ON,
    // in three places - the gravity at Start, the steering BeforeStep does not
    // do, and the shove Portals::Shoot applies.
    bool noGravity = false;
    entt::entity player = entt::null;
};

// Puts the level into the registry: the remake's gravity, the bodies built with
// the role table, and the player at the level's player_spawn. Then its buttons,
// doors, crystals, exit and portals are found. withStatics false leaves the
// statics out, which is the landing check's mutation. That leaves out the doors
// too, so there is nothing to wire.
bool Start(const Data& data, entt::registry& registry, Level& out, std::string& error, bool withStatics = true);

void BeforeStep(const Data& data, entt::registry& registry, Level& level, float direction, float dt);
// Takes the Data, as BeforeStep does. A torch that puts its light wall BACK has
// to rebuild that wall's body, and LevelBuilder::BuildEntity needs the scene to
// do it - the node's body and shape are its children. The alternative was for
// Level to borrow a scene pointer, which is a lifetime hazard introduced for one
// feature; there were three call sites, so the signature moved instead.
void AfterStep(const Data& data, entt::registry& registry, Level& level, float dt);

// One whole tick, for a caller that steps physics itself.
void Tick(const Data& data, entt::registry& registry, Level& level, float direction, float dt);

} // namespace MagicPortals::Game
