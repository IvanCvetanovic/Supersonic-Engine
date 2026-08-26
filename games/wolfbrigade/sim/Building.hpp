#pragma once

#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/Json.hpp"
#include "sim/Damageable.hpp"
#include "sim/EventBus.hpp"
#include "sim/GameState.hpp"
#include "sim/UnitStats.hpp"
#include "sim/World.hpp"

namespace WolfBrigade {

// One building's numbers, from `scripts/entities/building_stats.gd`.
//
// Zero damage or zero range means a building that does not fight, which is how
// a Town Hall and a Tower are the same class: the Tower has the fields filled
// in and the Town Hall does not.
struct BuildingStats {
    std::string id;
    std::string displayName;
    std::string faction{Factions::kPlayer};

    std::map<std::string, int> cost;
    double buildTime{0.0};
    int maxHp{1};

    // Already standing when the run starts. The Town Hall is the only one, and
    // it is why setup takes a flag as well as reading this.
    bool prePlaced{false};

    // Workers bank here. A building only counts once it is COMPLETE - a
    // half-built Town Hall is scaffolding, not a warehouse.
    bool isDepositPoint{false};

    std::vector<std::string> trains;
    std::vector<std::string> researches;

    // How far past the building's edge a trained unit appears, so it does not
    // spawn inside the thing that made it.
    double spawnOffset{40.0};

    // Offered in the build menu. Distinct from prePlaced: a Town Hall is
    // neither, and something could in principle be both.
    bool buildable{false};

    int damage{0};
    double attacksPerSec{1.0};
    double attackRange{0.0};
    double projectileSpeed{700.0};

    glm::vec2 bodySize{120.0f, 160.0f};
    std::string color{"#3b6fa0"};

    static BuildingStats FromJson(const std::string& id, const Supersonic::Json::Value& row);

    // The same explicit field mapping the units have, and for the same reason.
    // Building::ApplyUpgradeEffect goes through this and then fixes up the
    // current hit points, which a raw stat change cannot do.
    bool ApplyDelta(const std::string& field, double delta);
    static bool HasField(const std::string& field);
};

// One building, from `scripts/entities/building.gd`.
//
// A Town Hall, a Barracks and a Tower differ only by the stats handed to it -
// the same rule the units follow. A building is either being built, in which
// case it needs a worker to pour progress into it, or complete, in which case
// it can train, bank and (if it has the numbers) shoot.
//
// It is a Damageable, so a raider can attack it through the same interface it
// attacks a soldier through. That is what the GDScript's duck typing was doing
// implicitly.
class Building final : public Damageable {
public:
    enum class State { Constructing, Complete, Dead };

    // ~8 Hz target acquisition, the same rate the units think at and for the
    // same reason.
    static constexpr double kCombatTick = 0.125;

    Building(const BuildingStats& stats, bool prePlaced, GameState& state, EventBus& bus,
             World& world);

    void SetPosition(const glm::vec2& position) { m_position = position; }

    // How long each unit this building trains takes, read once from units.json.
    //
    // Separate from the constructor because it is a lookup into a DIFFERENT
    // file: a building's stats say what it trains, and units.json says how long
    // each of those takes. The original does the same join in setup().
    void SetTrainTimes(const Supersonic::Json::Value& units);

    // One step. Frozen with the rest of the board once the run is decided, and
    // inert while still under construction - a half-built barracks trains
    // nothing and a half-built tower shoots nothing.
    void Step(double delta);

    // --- Construction ------------------------------------------------------

    bool IsComplete() const { return m_state == State::Complete; }
    double BuildProgress() const { return m_buildProgress; }

    // Poured in by a worker, a little each step. Clamped at the total, so a
    // burst of progress from a long step finishes it rather than overshooting
    // into a number the progress bar cannot draw.
    void AddBuildProgress(double amount);

    // --- Training ----------------------------------------------------------

    // The cost is NOT checked here. Whoever enqueues has already paid - which
    // is the original's split, and it is what lets a queue be restored from a
    // save without charging the player twice.
    bool CanTrain(const std::string& unitId) const;
    void EnqueueTraining(const std::string& unitId);

    int QueueLength() const { return static_cast<int>(m_queue.size()); }
    double TrainProgress() const { return m_trainProgress; }

    // Where a trained unit appears: past the building's own edge, plus the
    // authored offset.
    glm::vec2 SpawnPoint() const;

    // --- Damage ------------------------------------------------------------

    void TakeDamage(int amount) override;
    void Destroy();

    bool IsAlive() const override { return m_state != State::Dead; }
    float HitHalfWidth() const override { return m_stats.bodySize.x * 0.5f; }
    glm::vec2 Position() const override { return m_position; }

    int Hp() const { return m_hp; }
    const BuildingStats& Stats() const { return m_stats; }
    const std::string& Faction() const { return m_stats.faction; }
    State CurrentState() const { return m_state; }

    // A deposit only once it is finished.
    bool IsDepositPoint() const { return m_stats.isDepositPoint && IsComplete(); }

    // --- Placement and picking ---------------------------------------------

    // The ground-aligned footprint: origin at the base centre, extending
    // upward. Used for overlap tests when placing and for clicking on one.
    struct Rect {
        glm::vec2 min{0.0f};
        glm::vec2 max{0.0f};

        bool Overlaps(const Rect& other) const;
        bool Contains(const glm::vec2& point) const;
    };
    Rect Footprint() const;
    bool ContainsPoint(const glm::vec2& point) const { return Footprint().Contains(point); }

    // Raise a researched upgrade's delta onto an already-standing building.
    // For max_hp the current hit points rise with the cap, so reinforcing a
    // damaged wall heals it as well as toughening it.
    // Returns false for a field this building does not have, which is a data
    // typo rather than a no-op worth swallowing - the original push_warnings it
    // so the headless run says so. The caller decides how loudly.
    bool ApplyUpgradeEffect(const std::string& field, double delta);

private:
    void CompleteConstruction();
    void StepTraining(double delta);
    void StepCombat(double delta);

    BuildingStats m_stats;

    // `m_run` rather than `m_state`, because a building HAS a state of its own
    // and two members a letter apart is how a bug gets written.
    GameState* m_run{nullptr};
    EventBus* m_bus{nullptr};
    World* m_world{nullptr};

    glm::vec2 m_position{0.0f};
    int m_hp{1};
    State m_state{State::Constructing};

    double m_buildProgress{0.0};
    std::vector<std::string> m_queue;
    double m_trainProgress{0.0};

    // Train times, read once from units.json at construction. Held here rather
    // than looked up per step because it is the same answer every time and the
    // lookup is a map walk on a hot path.
    std::map<std::string, double> m_trainTimes;

    double m_attackCooldown{0.0};
    double m_combatAccumulator{0.0};
};

} // namespace WolfBrigade
