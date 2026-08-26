#include "sim/Building.hpp"

#include <algorithm>

#include "sim/Projectiles.hpp"
#include "sim/Unit.hpp"

namespace WolfBrigade {

namespace {

glm::vec2 toVec2(const Supersonic::Json::Value& value, const glm::vec2& fallback) {
    const auto& array = value.AsArray();
    if (array.size() < 2) return fallback;
    return glm::vec2(array[0].AsFloat(), array[1].AsFloat());
}

std::vector<std::string> toIds(const Supersonic::Json::Value& value) {
    std::vector<std::string> ids;
    for (const Supersonic::Json::Value& entry : value.AsArray()) ids.push_back(entry.AsString());
    return ids;
}

} // namespace

BuildingStats BuildingStats::FromJson(const std::string& id,
                                      const Supersonic::Json::Value& row) {
    BuildingStats stats;
    stats.id = id;
    stats.displayName = row["display_name"].AsString(id);
    stats.faction = row["faction"].AsString(Factions::kPlayer);

    for (const auto& [resource, amount] : row["cost"].AsObject()) {
        stats.cost[resource] = static_cast<int>(amount.AsNumber());
    }

    stats.buildTime = row["build_time"].AsNumber(0.0);
    stats.maxHp = static_cast<int>(row["max_hp"].AsNumber(1.0));
    stats.prePlaced = row["pre_placed"].AsBool(false);
    stats.isDepositPoint = row["is_deposit_point"].AsBool(false);
    stats.trains = toIds(row["trains"]);
    stats.researches = toIds(row["researches"]);
    stats.spawnOffset = row["spawn_offset"].AsNumber(40.0);
    stats.buildable = row["buildable"].AsBool(false);

    // Absent means a building that does not fight, which is most of them. The
    // Tower is the one row in the file with these filled in.
    stats.damage = static_cast<int>(row["damage"].AsNumber(0.0));
    stats.attacksPerSec = row["attacks_per_sec"].AsNumber(1.0);
    stats.attackRange = row["attack_range"].AsNumber(0.0);
    stats.projectileSpeed = row["projectile_speed"].AsNumber(700.0);

    stats.bodySize = toVec2(row["body_size"], glm::vec2(120.0f, 160.0f));
    stats.color = row["color"].AsString("#3b6fa0");
    return stats;
}

Building::Building(const BuildingStats& stats, bool prePlaced, GameState& state, EventBus& bus,
                   World& world)
    : m_stats(stats), m_run(&state), m_bus(&bus), m_world(&world), m_hp(stats.maxHp) {
    // Both conditions, as in the original. A building with no build time is
    // finished the moment it is placed whether or not anybody said pre-placed -
    // otherwise it would sit at zero progress forever with nothing to advance
    // it, because a worker pours progress in and the total is already met.
    if (prePlaced || m_stats.buildTime <= 0.0) {
        m_state = State::Complete;
        m_buildProgress = m_stats.buildTime;
    } else {
        m_state = State::Constructing;
    }
}

void Building::SetTrainTimes(const Supersonic::Json::Value& units) {
    for (const std::string& unitId : m_stats.trains) {
        m_trainTimes[unitId] = units[unitId]["train_time"].AsNumber(0.0);
    }
}

void Building::Step(double delta) {
    if (!m_run->IsPlaying()) return;

    // Nothing happens on a construction site. A barracks that trained while
    // half-built would let a player skip the whole build time by queuing early.
    if (m_state != State::Complete) return;

    StepTraining(delta);
    StepCombat(delta);
}

void Building::AddBuildProgress(double amount) {
    if (m_state != State::Constructing) return;

    m_buildProgress = std::min(m_buildProgress + amount, m_stats.buildTime);
    if (m_buildProgress >= m_stats.buildTime) CompleteConstruction();
}

void Building::CompleteConstruction() {
    m_state = State::Complete;
    m_bus->buildingCompleted.Emit(this);
}

bool Building::CanTrain(const std::string& unitId) const {
    if (m_state != State::Complete) return false;
    return std::find(m_stats.trains.begin(), m_stats.trains.end(), unitId) !=
           m_stats.trains.end();
}

void Building::EnqueueTraining(const std::string& unitId) {
    if (CanTrain(unitId)) m_queue.push_back(unitId);
}

void Building::StepTraining(double delta) {
    if (m_queue.empty()) return;

    const std::string& unitId = m_queue.front();

    // At least a hundredth of a second. A unit authored with no train time
    // would otherwise complete on the step it was queued and every step after
    // it, emptying a queue of ten in one frame.
    const auto it = m_trainTimes.find(unitId);
    const double trainTime = std::max(it == m_trainTimes.end() ? 0.0 : it->second, 0.01);

    m_trainProgress += delta;
    if (m_trainProgress < trainTime) return;

    const std::string finished = unitId;
    m_queue.erase(m_queue.begin());

    // Reset to zero rather than carrying the remainder. The original does the
    // same, and it means a queue of five takes five full train times rather
    // than five minus the accumulated overshoot - which is what the training
    // bar in the HUD is drawing.
    m_trainProgress = 0.0;

    m_bus->unitTrained.Emit(finished, SpawnPoint());
}

void Building::StepCombat(double delta) {
    // A Town Hall has neither, and skips all of this. The check is on the DATA
    // rather than on a type, which is what makes a defensive building a row in
    // a JSON file instead of a class.
    if (m_stats.damage <= 0 || m_stats.attackRange <= 0.0) return;

    // Decremented every step, but acquisition only happens on the tick. The
    // order matters and matches the original: a tower whose cooldown expires
    // between ticks still waits for the next one, so its rate of fire is
    // quantised to 8 Hz rather than to the frame rate.
    if (m_attackCooldown > 0.0) m_attackCooldown -= delta;

    m_combatAccumulator += delta;
    if (m_combatAccumulator < kCombatTick) return;
    m_combatAccumulator -= kCombatTick;

    if (m_attackCooldown > 0.0) return;

    Unit* target = m_world->NearestEnemyUnit(m_stats.faction, m_position.x,
                                             static_cast<float>(m_stats.attackRange));
    if (target == nullptr) return;

    ProjectilePool* pool = m_world->Projectiles();
    if (pool == nullptr) return;

    // From high up the body rather than from its base, so a bolt leaves the
    // top of the tower.
    const glm::vec2 from = m_position + glm::vec2(0.0f, -m_stats.bodySize.y * 0.7f);
    pool->Spawn(from, target, m_stats.damage, static_cast<float>(m_stats.projectileSpeed));

    m_attackCooldown = 1.0 / std::max(m_stats.attacksPerSec, 0.01);
}

glm::vec2 Building::SpawnPoint() const {
    return m_position +
           glm::vec2(m_stats.bodySize.x * 0.5f + static_cast<float>(m_stats.spawnOffset), 0.0f);
}

void Building::TakeDamage(int amount) {
    if (m_state == State::Dead) return;

    m_hp = std::max(m_hp - amount, 0);

    // Before the destroy check, so the finishing hit still shows its number.
    m_bus->damageDealt.Emit(m_position + glm::vec2(0.0f, -m_stats.bodySize.y), amount,
                            m_stats.faction);

    if (m_hp <= 0) Destroy();
}

void Building::Destroy() {
    if (m_state == State::Dead) return;
    m_state = State::Dead;

    // A destroyed Town Hall stops being a deposit point, which IsDepositPoint
    // already answers - it requires Complete, and this is not. Workers walking
    // to it re-acquire on their next thinking tick.
    m_bus->buildingDestroyed.Emit(this);
}

Building::Rect Building::Footprint() const {
    // Origin at the BASE CENTRE, extending upward: y grows downward on screen,
    // so the top of the building is at position.y - height. A footprint
    // centred on the origin instead would put half the building underground
    // and make every placement overlap test wrong by half a body.
    const glm::vec2 size = m_stats.bodySize;
    Rect rect;
    rect.min = glm::vec2(m_position.x - size.x * 0.5f, m_position.y - size.y);
    rect.max = glm::vec2(m_position.x + size.x * 0.5f, m_position.y);
    return rect;
}

bool Building::Rect::Overlaps(const Rect& other) const {
    return min.x < other.max.x && other.min.x < max.x && min.y < other.max.y &&
           other.min.y < max.y;
}

bool Building::Rect::Contains(const glm::vec2& point) const {
    return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
}

bool Building::ApplyUpgradeEffect(const std::string& field, double delta) {
    if (field == "max_hp") {
        const int raise = static_cast<int>(delta);
        m_stats.maxHp += raise;

        // The current total rises with the cap. Reinforcing a wall that has
        // been chewed on heals it as well as toughening it, which is what the
        // upgrade is for - and a version that only raised the cap would leave
        // the player's Town Hall at the same hit points on a longer bar.
        m_hp += raise;
        return true;
    }
    if (field == "damage") {
        m_stats.damage += static_cast<int>(delta);
        return true;
    }
    if (field == "attack_range") {
        m_stats.attackRange += delta;
        return true;
    }
    if (field == "attacks_per_sec") {
        m_stats.attacksPerSec += delta;
        return true;
    }
    if (field == "build_time") {
        m_stats.buildTime += delta;
        return true;
    }

    // A field this building does not have. Reported rather than swallowed: an
    // upgrade that silently does nothing is a balance change nobody made.
    return false;
}

} // namespace WolfBrigade
