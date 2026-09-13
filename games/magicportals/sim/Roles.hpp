#pragma once

// The remake's role table: what each of the original's entity names does.
//
// entity_roles.json (the remake's game/data) maps every EntityName the original
// writes onto one gameplay role - `door.ent` is the exit, `door_lift` a switched
// door, `button` a pressure plate. The mapping is the remake's design decision,
// not the converter's, which is why it lives in the remake's data rather than in
// the levels; its LevelRuntime reads it the same way (level_runtime.gd:63-78).
// The port reads the file at run time from SUPERSONIC_MAGICPORTALS_DATA rather
// than copying it.
//
// Stricter than the remake in one place: a name listed under two DIFFERENT roles
// is refused. The remake flattens the table so the last role read wins, and its
// own note in the file says that is how no_portal_sign.ent once went missing. A
// name listed twice under the same role (up_down_arrow.ent is) is harmless.

#include "sim/Tscn.hpp"

#include <string>
#include <unordered_map>

namespace MagicPortals::Roles {

// The roles the port acts on, spelled as the table spells them.
inline constexpr const char* kPlayerSpawn = "player_spawn";
inline constexpr const char* kLevelBounds = "level_bounds";
inline constexpr const char* kLevelProperties = "level_properties";
inline constexpr const char* kExitDoor = "exit_door";
inline constexpr const char* kCollectible = "collectible";
inline constexpr const char* kSwitch = "switch";
inline constexpr const char* kSwitchedDoor = "switched_door";
inline constexpr const char* kLift = "lift";
inline constexpr const char* kMovingPlatform = "moving_platform";
inline constexpr const char* kStaticPortal = "static_portal";
inline constexpr const char* kNoPortalZone = "no_portal_zone";
inline constexpr const char* kHazard = "hazard";
inline constexpr const char* kDemolisher = "demolisher";
inline constexpr const char* kLauncher = "launcher";
inline constexpr const char* kProjectileBlocker = "projectile_blocker";
inline constexpr const char* kReflector = "reflector";
// A carranca: the wall gargoyle that spits fireballs (Turrets.hpp).
inline constexpr const char* kTurret = "turret";
// An open flame, and a barrel bomb (Fire.hpp). The bombs are found by their
// metadata/explosive flag rather than by this role, as the original tests them;
// the role is named here so a level carrying one stops counting as inert.
inline constexpr const char* kFireSource = "fire_source";
inline constexpr const char* kExplosive = "explosive";
// Seesaws and spinning platforms (Hinge.hpp). Found by their joint data rather
// than by this role, as the original finds them; named here so a level carrying
// one stops counting as inert.
inline constexpr const char* kHinge = "hinge";
// A key, the keyhole it opens and the door that vanishes with it (Keys.hpp).
// Paired by their metadata/color rather than by proximity, and only the door is
// solid - the other two are markers the builder gives no body.
inline constexpr const char* kKey = "key";
inline constexpr const char* kKeyhole = "keyhole";
inline constexpr const char* kLockedDoor = "locked_door";
// The invisible marker a minion is spawned from, and the markers it patrols
// between (Minions.hpp). The marker deletes itself in the original once it has
// spawned, and carries the patrol's PREFIX rather than a waypoint's name; the
// waypoints are read through that prefix rather than by this role, which is
// named here so a level carrying one stops counting as inert.
inline constexpr const char* kEnemySpawn = "enemy_spawn";
inline constexpr const char* kWaypoint = "waypoint";
// The lethal ring of chapter 3 (Fields.hpp). One mechanism under one role - both
// spellings name the same entity - so unlike kPickup this IS selected by role.
inline constexpr const char* kShockField = "shock_field";
// Played only for the boss the port has: Boss::Plays says which.
inline constexpr const char* kBossSpawn = "boss_spawn";
// The diamonds (Diamonds.hpp). The role covers shock_diamond.ent and
// fire_diamond.ent, which are DIFFERENT mechanisms - one kills a minion, the
// other takes portals away and gives fireballs instead - so Diamonds selects by
// entity name and never by this role. It was held out of IsPorted while only the
// shock one was built, because IsPorted is per-role and cannot say "served for
// one entity name and not the other": admitting it then would have claimed the
// four fire-diamond levels that start in order to claim the five shock ones. Both
// are built now, so it is admitted.
inline constexpr const char* kPickup = "pickup";

// Roles whose body moves and has to carry what stands on it, so the builder makes
// them kinematic rather than static: the remake's MOVING_ROLES
// (level_runtime.gd:27), which it turns into AnimatableBody2D for the same reason.
bool Moves(const std::string& role);

// A role the port plays, or one with nothing to play: scenery (no role at all),
// hints and decoration, the camera's start and the lifts' end markers. A level
// that carries any other role still starts, but the port leaves those entities
// inert, and test_mp_start lists them against each level. The one list to extend
// as roles land.
bool IsPorted(const std::string& role);

struct Table {
    std::unordered_map<std::string, std::string> roleOf; // entity name -> role
};

bool Load(const std::string& path, Table& out, std::string& error);

// The node's metadata/entity_name, or empty.
std::string EntityName(const Tscn::Node& node);

// The node's role, or empty when its entity is scenery.
std::string RoleOf(const Table& table, const Tscn::Node& node);

} // namespace MagicPortals::Roles
