#pragma once

#include <string>

#include <glm/glm.hpp>

#include "sim/EventBus.hpp"
#include "sim/GameState.hpp"
#include "sim/ResourceNode.hpp"
#include "sim/UnitStats.hpp"
#include "sim/World.hpp"

namespace WolfBrigade {

// One unit, from `scripts/entities/unit.gd`.
//
// Worker, soldier, archer, raider and brute differ ONLY by the stats handed to
// it - that is the original's fifth architecture rule, and it is why there is
// one class here rather than five. What varies is the `behavior` field, which
// selects which thinking routine runs.
//
// Two clocks, deliberately. Movement and continuous work run EVERY step, so a
// unit interpolates smoothly; decisions and scans run on a ~8 Hz tick, because
// a hundred units scanning a hundred nodes sixty times a second is ten thousand
// distance checks a frame for an answer that has not changed. Mixing the two up
// is the difference between a game that runs on a phone and one that does not.
//
// This is the WORKER half of the port. Soldiers, archers and raiders need the
// lane index and the buildings they fight over, and arrive with those.
class Unit {
public:
    enum class State { Idle, Moving, Gathering, Delivering, Building, Attacking, Fleeing, Dead };

    // Behaviours, as authored in units.json.
    static constexpr const char* kWorker = "worker";
    static constexpr const char* kSoldier = "soldier";
    static constexpr const char* kRanged = "ranged";
    static constexpr const char* kAggressor = "aggressor";

    // ~8 Hz. A performance knob, not a game stat - which is why it is a
    // constant here and not a number in the data files.
    static constexpr float kAiTickInterval = 0.125f;

    // Close enough to count as arrived. Without a threshold a unit oscillates
    // around its target forever, one sub-pixel step at a time.
    static constexpr float kArriveThreshold = 3.0f;

    Unit(const UnitStats& stats, GameState& state, EventBus& bus, World& world)
        : m_stats(stats), m_state(&state), m_bus(&bus), m_world(&world), m_hp(stats.maxHp) {}

    // One step. `delta` is seconds.
    void Step(float delta);

    // --- Orders ------------------------------------------------------------

    // An explicit reposition. Drops any acquired target, which is what makes a
    // move order a way to pull a unit OUT of a fight rather than a suggestion
    // it ignores while something is in range.
    //
    // Only x is taken. This is a single-lane game and a unit that left its
    // ground row would be walking through the sky.
    void CommandMoveTo(const glm::vec2& target);

    // --- Damage ------------------------------------------------------------

    void TakeDamage(int amount);
    void Kill();

    bool IsAlive() const { return m_phase != State::Dead; }

    // Half the body width, so an attacker stops adjacent rather than standing
    // inside what it is hitting.
    float HitHalfWidth() const { return m_stats.bodySize.x * 0.5f; }

    // --- What it is doing --------------------------------------------------

    State CurrentState() const { return m_phase; }
    int Hp() const { return m_hp; }
    const UnitStats& Stats() const { return m_stats; }
    const std::string& Faction() const { return m_stats.faction; }
    bool IsPlayer() const { return m_stats.faction == Factions::kPlayer; }

    glm::vec2 Position() const { return m_position; }
    void SetPosition(const glm::vec2& position) { m_position = position; }

    // What it is carrying, and of what. Exposed because conservation - what
    // came out of a tree is either banked or still in hand - is the property
    // the economy is checked against.
    int Carrying() const { return m_carry; }
    const std::string& CarryResource() const { return m_carryResource; }

    ResourceNode* TargetNode() const { return m_targetNode; }

private:
    void TickAi();
    void TickWorker();
    void SeekWork();
    void BeginDelivering();

    void StepToward(const glm::vec2& target, float delta);
    void StepGather(float delta);
    void StepDeliver(float delta);
    void StepFlee(float delta);

    void FleeCheck();
    void AfterGathering();
    bool HasLiveNode() const;
    bool Arrived() const;
    void SetState(State next);

    UnitStats m_stats;
    GameState* m_state{nullptr};
    EventBus* m_bus{nullptr};
    World* m_world{nullptr};

    int m_hp{1};
    State m_phase{State::Idle};
    glm::vec2 m_position{0.0f};
    glm::vec2 m_moveTarget{0.0f};

    // Staggered across units in the original, by a random offset, so a hundred
    // of them do not all think on the same frame. Left at zero here: nothing
    // in the port creates a hundred units on one frame yet, and a random
    // offset is the one thing that would make a replay diverge from the run it
    // is replaying.
    float m_aiAccumulator{0.0f};

    // Worker economy.
    int m_carry{0};
    std::string m_carryResource;
    float m_gatherAccumulator{0.0f};
    ResourceNode* m_targetNode{nullptr};
    int m_depositIndex{-1};

    // Combat. Only the parts the worker needs are used here: an ordered worker
    // fights instead of fleeing, and that flag is what remembers it.
    bool m_orderedToAttack{false};
    int m_fleeIndex{-1};
    float m_attackCooldown{0.0f};
};

} // namespace WolfBrigade
