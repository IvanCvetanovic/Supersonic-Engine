#pragma once

#include <cmath>
#include <limits>
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

    // --- The hero-first game (the game's 50741d1) ----------------------------
    // (`sprite` joined these in the original; it is display-only and belongs
    // with the layer.)

    // The unit ids automatic production starts ENABLED for - direction B, the
    // village runs itself. Only the seed: the per-building toggle state lives
    // on the Building, and a save carries it.
    std::vector<std::string> autoTrainDefault;

    // The population this building PROVIDES once complete.
    int supply{0};

    // Passive hit points a second while complete; zero for none.
    double hpRegen{0.0};

    // The fallen hero respawns at the nearest COMPLETE building with this flag
    // - the Town Hall, the Waystone. None standing at respawn time loses the run.
    bool heroRespawn{false};

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
    //
    // SUPPLY IS. A full population refuses the order: the button checks first
    // and refuses politely, and this is the backstop. So whoever pays asks
    // Supply::HasRoomFor before taking the money, as the original's bar does.
    bool CanTrain(const std::string& unitId) const;
    void EnqueueTraining(const std::string& unitId);

    int QueueLength() const { return static_cast<int>(m_queue.size()); }
    const std::vector<std::string>& TrainQueue() const { return m_queue; }
    double TrainProgress() const { return m_trainProgress; }

    // Empties the queue without refunding it or touching the progress - what
    // the original's harnesses do to its public list.
    void ClearTrainQueue() { m_queue.clear(); }

    // --- Auto-production (the village runs itself) -------------------------

    // The units this building keeps its queue fed with, one at a time and
    // round-robin, paying the normal price and holding back economy.json's
    // auto_train_reserve so production never starves the player's building.
    // Seeded from the data's auto_train_default, filtered to what it trains;
    // the player's toggles then live here, and a save carries them.
    bool IsAutoTraining(const std::string& unitId) const;
    void SetAutoTrain(const std::string& unitId, bool on);
    const std::vector<std::string>& AutoTrain() const { return m_autoTrain; }

    // --- Research (the Armory, the Storehouse) ------------------------------

    // Research takes TIME: queued here, one at a time from the front, and
    // landed when it finishes. The cost is paid by whoever queues it, at queue
    // time - so a building destroyed mid-research has cost the player the
    // price, which is the original's rule too.
    void EnqueueResearch(const std::string& upgradeId);
    bool HasResearch(const std::string& upgradeId) const;
    const std::vector<std::string>& ResearchQueue() const { return m_researchQueue; }
    double ResearchProgress() const { return m_researchProgress; }

    // --- Where the trained go -----------------------------------------------

    // A rally point, or none. A unit trained here is ordered to it. The
    // original's "none" is Vector2.INF, and so is this one's.
    static glm::vec2 NoRally() { return glm::vec2(std::numeric_limits<float>::infinity()); }
    static bool IsRally(const glm::vec2& point) { return std::isfinite(point.x); }
    const glm::vec2& RallyPoint() const { return m_rally; }
    void SetRallyPoint(const glm::vec2& point) { m_rally = point; }

    // The squad a unit trained here joins: the garrison or the warband.
    const std::string& RecruitSquad() const { return m_recruitSquad; }
    void SetRecruitSquad(const std::string& squad) { m_recruitSquad = squad; }

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

    // --- Save --------------------------------------------------------------

    // No outgoing references: a tower re-acquires its target every tick, so
    // there is nothing here that has to survive as an id.
    Supersonic::Json::Value ToSave() const;

    // Restores the live fields onto a building already built from re-derived
    // stats. The stats are NOT saved - they come back through
    // Upgrades::ForBuilding, which re-sums the researched and owned effects, so
    // a building restored after a Reset Progress reflects the CURRENT profile
    // rather than the one that saved it.
    //
    // Which is exactly why the hit points are clamped: the re-derived maximum
    // can be SMALLER than it was at capture, and a saved 1300 onto a 1000-point
    // Town Hall would leave a building above its own bar.
    void FromSave(const Supersonic::Json::Value& saved);

    // Raise a researched upgrade's delta onto an already-standing building.
    // For max_hp the current hit points rise with the cap, so reinforcing a
    // damaged wall heals it as well as toughening it.
    // Returns false for a field this building does not have, which is a data
    // typo rather than a no-op worth swallowing - the original push_warnings it
    // so the headless run says so. The caller decides how loudly.
    bool ApplyUpgradeEffect(const std::string& field, double delta);

private:
    void CompleteConstruction();
    void StepAutoTrain(double delta);
    bool AffordableOverReserve(const Cost& cost) const;
    void StepTraining(double delta);
    void StepResearch(double delta);
    void StepCombat(double delta);
    void StepRegen(double delta);

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

    // Auto-production: the enabled ids, a round-robin cursor over them, and a
    // ~2 Hz decision clock. The clock RESETS to zero rather than carrying the
    // remainder, as the original's does, and it is not saved.
    std::vector<std::string> m_autoTrain;
    int m_autoIndex{0};
    double m_autoAccumulator{0.0};

    std::vector<std::string> m_researchQueue;
    double m_researchProgress{0.0};

    glm::vec2 m_rally{NoRally()};
    std::string m_recruitSquad{Squads::kGarrison};

    // Passive repair: seconds since the last hit, starting out of combat, and
    // the fraction of a hit point owed.
    double m_sinceDamage{1.0e9};
    double m_regenAccumulator{0.0};
};

} // namespace WolfBrigade
