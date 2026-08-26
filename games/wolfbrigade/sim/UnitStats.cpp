#include "sim/UnitStats.hpp"

namespace WolfBrigade {

namespace {

// Godot's UnitStats._to_vec2: a two-element array, or the fallback. An authored
// [26] is a mistake and reads as the default rather than as a zero height.
glm::vec2 toVec2(const Supersonic::Json::Value& value, const glm::vec2& fallback) {
    const auto& array = value.AsArray();
    if (array.size() < 2) return fallback;
    return glm::vec2(array[0].AsFloat(), array[1].AsFloat());
}

} // namespace

UnitStats UnitStats::FromJson(const std::string& unitId, const Supersonic::Json::Value& row) {
    UnitStats stats;
    stats.id = unitId;
    stats.displayName = row["display_name"].AsString(unitId);
    stats.faction = row["faction"].AsString(Factions::kPlayer);
    stats.trainedAt = row["trained_at"].AsString("");

    for (const auto& [resource, amount] : row["cost"].AsObject()) {
        stats.cost[resource] = static_cast<int>(amount.AsNumber());
    }

    stats.trainTime = row["train_time"].AsFloat(0.0f);
    stats.maxHp = static_cast<int>(row["max_hp"].AsNumber(1.0));
    stats.damage = static_cast<int>(row["damage"].AsNumber(0.0));
    stats.attacksPerSec = row["attacks_per_sec"].AsFloat(1.0f);
    stats.attackRange = row["attack_range"].AsFloat(40.0f);
    stats.aggroRange = row["aggro_range"].AsFloat(0.0f);
    stats.moveSpeed = row["move_speed"].AsFloat(100.0f);
    stats.behavior = row["behavior"].AsString("worker");
    stats.projectileSpeed = row["projectile_speed"].AsFloat(700.0f);
    stats.bodySize = toVec2(row["body_size"], glm::vec2(26.0f, 34.0f));
    stats.color = row["color"].AsString("#ffffff");
    stats.gatherRate = row["gather_rate"].AsFloat(0.0f);
    stats.carryCapacity = static_cast<int>(row["carry_capacity"].AsNumber(0.0));
    stats.gatherRange = row["gather_range"].AsFloat(46.0f);
    stats.depositRange = row["deposit_range"].AsFloat(110.0f);
    return stats;
}

namespace {

// The upgradable fields, and which ones are whole numbers.
//
// Integer fields TRUNCATE the delta the way GDScript's `int + float` does not -
// there, `damage` would quietly become a float and the difference would show up
// as a raider taking 8.4 damage. Here the type is fixed, so the rounding is
// explicit and stated once.
bool applyInt(int& target, double delta) {
    target += static_cast<int>(delta);
    return true;
}

} // namespace

bool UnitStats::ApplyDelta(const std::string& field, double delta) {
    if (field == "max_hp") return applyInt(maxHp, delta);
    if (field == "damage") return applyInt(damage, delta);
    if (field == "carry_capacity") return applyInt(carryCapacity, delta);

    if (field == "attacks_per_sec") { attacksPerSec += static_cast<float>(delta); return true; }
    if (field == "attack_range") { attackRange += static_cast<float>(delta); return true; }
    if (field == "aggro_range") { aggroRange += static_cast<float>(delta); return true; }
    if (field == "move_speed") { moveSpeed += static_cast<float>(delta); return true; }
    if (field == "projectile_speed") { projectileSpeed += static_cast<float>(delta); return true; }
    if (field == "gather_rate") { gatherRate += static_cast<float>(delta); return true; }
    if (field == "gather_range") { gatherRange += static_cast<float>(delta); return true; }
    if (field == "deposit_range") { depositRange += static_cast<float>(delta); return true; }
    if (field == "train_time") { trainTime += static_cast<float>(delta); return true; }

    // Not a field a unit has. The original push_warnings here; this says so to
    // the caller, which is what lets the shipped data be checked at startup
    // instead of hoped about.
    return false;
}

bool UnitStats::HasField(const std::string& field) {
    UnitStats probe;
    return probe.ApplyDelta(field, 0.0);
}

} // namespace WolfBrigade
