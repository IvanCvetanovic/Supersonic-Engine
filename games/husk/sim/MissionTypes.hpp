#pragma once

// The mission layer's data (src/sim/mission.rs): what a mission file says, and
// the runtime state of the mission being played. The functions that load,
// install and run missions are in Mission.hpp.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace husk {

struct ObjectiveDef {
    std::string name; // the stable handle triggers use
    std::string text; // what the player reads
    bool optional = false;
};

// Map geometry as authored: flat tuples, (min_x, min_y, max_x, max_y).
struct MapSpec {
    float half = 0.0f;
    std::vector<std::array<float, 4>> obstacles;
    std::vector<std::array<float, 4>> roads;
    std::vector<std::pair<std::array<float, 4>, uint8_t>> plateaus;
    std::vector<std::pair<std::vector<std::pair<float, float>>, uint8_t>> plateauPolys;
    std::vector<std::array<float, 4>> ramps;
    std::vector<std::array<float, 4>> bumps;
};

struct NamedSpot {
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
};

struct NamedSquad {
    std::string name;
    uint32_t count = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct SpawnSpec {
    std::optional<std::pair<float, float>> hero;
    std::vector<NamedSpot> buildings; // spawned complete
    std::vector<NamedSquad> units;    // player squads
    std::vector<NamedSquad> enemies;  // hostile squads, guarding
    std::vector<NamedSpot> sources;
    std::vector<NamedSpot> items;
    float startEssence = 0.0f;
};

// One beat of a staged scene. Client-only; the sim is paused while one plays.
struct SceneStep {
    float at = 0.0f;
    std::string subtitle;
    std::string speaker;
    std::optional<std::array<float, 3>> camera;
    std::vector<std::pair<float, float>> actors;
    float storm = 0.0f;
    float flash = 0.0f;
};

struct TriggerCond {
    enum class Kind : uint8_t {
        TimeAtLeast,
        TotalExtractedAtLeast,
        EssenceAtLeast,
        AnimaAtLeast,
        HeroLevelAtLeast,
        HeroRankAtLeast,
        HeroInArea,
        PlayerUnitsOfAtLeast,
        PlayerBuildingsOfAtLeast,
        ArmyWillAtLeast,
        EnemyUnitsAtMost,
        PlayerWiped,
        ItemsOnGroundAtMost,
        ObjectiveComplete,
        ObjectiveActive,
        ObjectiveActiveForAtLeast,
        AllRequiredObjectivesComplete,
    };

    Kind kind = Kind::PlayerWiped;
    float amount = 0.0f;  // the f32 payload: seconds, essence, anima
    uint32_t count = 0;   // the integer payload: level, will, count
    uint8_t slot = 0;     // HeroRankAtLeast
    uint8_t rank = 0;     // HeroRankAtLeast
    float x = 0.0f;       // HeroInArea
    float y = 0.0f;
    float radius = 0.0f;
    std::string name;     // unit, building or objective name
};

struct TriggerAction {
    enum class Kind : uint8_t {
        Message,
        Say,
        SpawnSquad,
        SpawnPlayerSquad,
        SpawnPlayerBuilding,
        SpawnItem,
        AddObjective,
        CompleteObjective,
        FailObjective,
        GrantEssence,
        GrantAnima,
        GrantHeroXp,
        Victory,
        Defeat,
    };

    Kind kind = Kind::Victory;
    std::string text;    // Message, Say, AddObjective
    std::string speaker; // Say
    std::string name;    // unit, building, item or objective name
    uint32_t count = 0;
    float x = 0.0f;
    float y = 0.0f;
    float amount = 0.0f; // the Grant* payloads
    bool flag = false;   // SpawnSquad's aggressive, AddObjective's optional
};

struct TriggerDef {
    std::string name;
    bool once = true; // fire once and retire
    std::vector<TriggerCond> when;
    std::vector<TriggerAction> actions;
};

struct MissionDef {
    std::string name;
    std::string briefing;
    bool night = false;
    std::optional<MapSpec> map;
    bool rotate180 = false;
    bool dayCycle = false;
    bool rainy = false;
    std::optional<SpawnSpec> spawns;
    std::string biome;
    std::vector<ObjectiveDef> objectives;
    std::vector<TriggerDef> triggers;
    std::vector<SceneStep> intro;
    std::vector<SceneStep> outro;
};

// Declaration order is the hash's encoding.
enum class MissionOutcome : uint8_t { Playing, Victory, Defeat };
enum class ObjectiveStatus : uint8_t { Active, Complete, Failed };

struct ObjectiveState {
    std::string name;
    std::string text;
    bool optional = false;
    ObjectiveStatus status = ObjectiveStatus::Active;
    uint64_t sinceTick = 0; // the tick it appeared or last changed status
};

struct MissionRuntime {
    MissionDef def;
    uint64_t startTick = 0;
    std::vector<bool> fired; // per trigger, for `once`
    std::vector<ObjectiveState> objectives;
    MissionOutcome outcome = MissionOutcome::Playing;
};

} // namespace husk
