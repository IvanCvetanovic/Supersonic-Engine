#pragma once

#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/Json.hpp"

namespace WolfBrigade {

// Faction ids, from `scripts/core/factions.gd`. They match the "faction" field
// in units.json and buildings.json.
namespace Factions {
inline constexpr const char* kPlayer = "player";
inline constexpr const char* kEnemy = "enemy";
} // namespace Factions

// Squad ids, from `scripts/systems/squads.gd`: where a trained follower goes.
// The garrison holds its post; the warband keeps station on the hero.
namespace Squads {
inline constexpr const char* kGarrison = "garrison";
inline constexpr const char* kWarband = "warband";
} // namespace Squads

// One unit's numbers, from `scripts/entities/unit_stats.gd`.
//
// A row of units.json, read once at spawn and handed to the unit. The point is
// that nothing downstream touches a raw dictionary: a typo in a field name is a
// compile error here instead of a zero somewhere in combat.
//
// Built per spawn, deliberately. Difficulty scales an enemy's HP and damage,
// the spawn's own queued multipliers apply on top of that, and in-run upgrades
// add to a player unit's - all onto THIS instance. Sharing one block between spawns
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

    // --- The hero-first game (the game's 50741d1) ----------------------------
    //
    // The art fields the original added beside these - sprite, art_size,
    // anim_sheet, anim_frame, anim_cols, gather_hit_col - are display-only by
    // the game's own rule (body_size is every gameplay measurement) and belong
    // with the layer, not here.

    // Can it place and construct buildings? Drives the builder-gated Build
    // menu - a data flag rather than "is this a worker".
    bool canBuild{false};

    // The population this unit OCCUPIES. Farms and the Town Hall provide it.
    int supply{1};

    // Passive hit points a second, once it has been out of combat for
    // economy.hp_regen_delay_s.
    //
    // A DOUBLE, unlike its neighbours, because it feeds an accumulator: the
    // original's 0.8 is a 64-bit float, and 0.8f times a tenth crosses each
    // whole hit point on a different step than 0.8 does.
    double hpRegen{0.0};

    // Can the player take direct control of it? The hero, and only the hero.
    bool controllable{false};

    // Hit points a healer restores per cast. Deliberately separate from
    // damage, so an army-damage bonus or a weapon upgrade never inflates it.
    int healAmount{0};

    // Active abilities, as abilities.json ids, castable only under direct
    // control. The ORDER is the hotkey and on-screen button order.
    std::vector<std::string> abilities;

    // Can it be put in the hero's warband or the garrison? Soldiers, archers
    // and priests. Workers have the shelter bell instead, and the hero leads
    // rather than follows.
    bool canFollow{false};

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

    // The numeric fields that are BAKED at spawn and cannot be recovered from
    // the unit's id alone.
    //
    // Difficulty scales an enemy's hit points, the spawn's own multipliers apply
    // on top, and in-run research plus owned meta levels add to a player unit - all onto
    // the block handed to that one spawn. Units are never retroactively
    // re-upgraded, so a saved unit cannot be rebuilt by asking the data what a
    // raider is; it has to carry its own numbers.
    //
    // Everything NOT here - name, faction, behaviour, body size, colour, cost -
    // comes back from the id unchanged, so it is not stored per unit.
    //
    // Exactly the set ApplyDelta accepts, minus `train_time`, which is a
    // building's business. A test pins that correspondence, so adding an
    // upgradable field and forgetting this list is a failure rather than a
    // silently unsaved stat.
    Supersonic::Json::Value ToBlock() const;
    void ApplyBlock(const Supersonic::Json::Value& block);

    // Whether a field can be upgraded at all. Lets the data check itself: every
    // effect field in upgrades.json and meta.json must be one of these, and a
    // typo is a balance change nobody made.
    static bool HasField(const std::string& field);

    // The block's field names, so a test can assert the set against HasField
    // rather than a human keeping two lists in step.
    static std::vector<std::string> BlockFields();
};

} // namespace WolfBrigade
