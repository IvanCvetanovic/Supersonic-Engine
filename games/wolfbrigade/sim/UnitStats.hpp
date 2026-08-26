#pragma once

#include <map>
#include <string>

#include <glm/glm.hpp>

#include "core/Json.hpp"

namespace WolfBrigade {

// Faction ids, from `scripts/core/factions.gd`. They match the "faction" field
// in units.json and buildings.json.
namespace Factions {
inline constexpr const char* kPlayer = "player";
inline constexpr const char* kEnemy = "enemy";
} // namespace Factions

// One unit's numbers, from `scripts/entities/unit_stats.gd`.
//
// A row of units.json, read once at spawn and handed to the unit. The point is
// that nothing downstream touches a raw dictionary: a typo in a field name is a
// compile error here instead of a zero somewhere in combat.
//
// Built per spawn, deliberately. Difficulty scales an enemy's HP and damage,
// endless growth multiplies on top of that, and in-run upgrades add to a
// player unit's - all onto THIS instance. Sharing one block between spawns
// would make the tenth raider inherit the ninth's scaling.
struct UnitStats {
    std::string id;
    std::string displayName;
    std::string faction{Factions::kPlayer};
    std::string trainedAt;

    // resource id -> amount. Empty for anything that is not trained.
    std::map<std::string, int> cost;
    float trainTime{0.0f};

    int maxHp{1};
    int damage{0};
    float attacksPerSec{1.0f};
    float attackRange{40.0f};

    // How far it notices an enemy. Zero for a worker, which never picks a
    // fight - a worker that aggroed would abandon the economy the first time
    // a raider walked past the tree line.
    float aggroRange{0.0f};

    float moveSpeed{100.0f};

    // Which behaviour drives it: worker, soldier, ranged, aggressor. A string
    // rather than an enum because it is authored in the JSON, and an unknown
    // one has to be survivable.
    std::string behavior{"worker"};

    float projectileSpeed{700.0f};

    glm::vec2 bodySize{26.0f, 34.0f};
    std::string color{"#ffffff"};

    // Worker economy. Zero on anything that does not gather.
    float gatherRate{0.0f};
    int carryCapacity{0};
    float gatherRange{46.0f};
    float depositRange{110.0f};

    // Reads a row. Every field has a fallback, so a partially-authored unit is
    // a weak unit rather than a crash - which is what lets a designer add a
    // unit and fill it in over an afternoon.
    static UnitStats FromJson(const std::string& unitId, const Supersonic::Json::Value& row);

    // Adds an upgrade's delta to the field named by the data.
    //
    // GDScript does this with reflection - `stats.set(field, stats.get(field) +
    // delta)` - and C++ has none, so the mapping is written out. That is worse
    // in one way and better in two: adding a field to this struct means
    // remembering to add it here, but an effect naming a field nobody has
    // returns FALSE instead of silently doing nothing, and the set of upgradable
    // fields becomes something a test can enumerate.
    //
    // Flat additive, like every effect in this game - in-run research and
    // persistent meta both land here, and because they only ever add, the order
    // they are applied in does not matter.
    bool ApplyDelta(const std::string& field, double delta);

    // Whether a field can be upgraded at all. Lets the data check itself: every
    // effect field in upgrades.json and meta.json must be one of these, and a
    // typo is a balance change nobody made.
    static bool HasField(const std::string& field);
};

} // namespace WolfBrigade
