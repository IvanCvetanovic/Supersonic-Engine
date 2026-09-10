#pragma once

#include <string>

#include <glm/glm.hpp>

#include "sim/Damageable.hpp"
#include "sim/EventBus.hpp"
#include "sim/GameState.hpp"
#include "sim/ResourceNode.hpp"
#include "sim/SaveIds.hpp"
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
// SCALARS ARE DOUBLES HERE, AND THAT IS NOT AN ACCIDENT.
//
// GDScript's `float` is 64-bit. Godot's `Vector2` is not - it holds 32-bit
// `real_t` - so the original runs its POSITIONS in single precision and
// everything else in double, and a port that picks one width for both is wrong
// wherever a scalar accumulates.
//
// It cost a real disagreement to find. A raider left with the Town Hall for
// twelve seconds took it to 934 in the original and 928 here: one extra hit.
// The reason is a cooldown of 1.0 decremented by 0.1 at every step - which
// reaches zero or below after ELEVEN decrements in double and TEN in float,
// because 0.1 is not representable in either and the errors accumulate in
// opposite directions. Eleven steps of 0.1 is an attack every 1.1 seconds
// rather than every 1.0, for a unit whose data says one per second.
//
// So: `delta`, cooldowns, timers and accumulators are `double`; positions and
// sizes stay `glm::vec2`. That is the same split the original has, and it is
// what makes the numbers match.
//
// Four behaviours, three thinking routines. A soldier and an archer decide the
// same way and differ only in how they close the distance - which is why the
// original dispatches both to _tick_defender and branches inside the attack
// step, and why doing it the other way round would duplicate the acquisition
// logic twice with one copy quietly drifting.
class Unit final : public Damageable {
public:
    enum class State { Idle, Moving, Gathering, Delivering, Building, Attacking, Fleeing, Dead };

    // Behaviours, as authored in units.json.
    static constexpr const char* kWorker = "worker";
    static constexpr const char* kSoldier = "soldier";
    static constexpr const char* kRanged = "ranged";
    static constexpr const char* kAggressor = "aggressor";

    // ~8 Hz. A performance knob, not a game stat - which is why it is a
    // constant here and not a number in the data files.
    static constexpr double kAiTickInterval = 0.125;

    // Close enough to count as arrived. Without a threshold a unit oscillates
    // around its target forever, one sub-pixel step at a time.
    static constexpr float kArriveThreshold = 3.0f;

    Unit(const UnitStats& stats, GameState& state, EventBus& bus, World& world)
        : m_stats(stats), m_state(&state), m_bus(&bus), m_world(&world), m_hp(stats.maxHp) {}

    // One step. `delta` is seconds, in double - see the note above.
    void Step(double delta);

    // --- Orders ------------------------------------------------------------

    // An explicit reposition. Drops any acquired target, which is what makes a
    // move order a way to pull a unit OUT of a fight rather than a suggestion
    // it ignores while something is in range.
    //
    // Both axes, with y clamped onto the walkable band, so a click on the sky
    // still lands on real ground. It PARKS a worker: once arrived, the worker
    // holds there instead of seeking work, until a gather, build or attack
    // order.
    void CommandMoveTo(const glm::vec2& target);

    // Attack a specific thing, because the player said so.
    //
    // Sets the ordered-to-attack flag, which is what makes a WORKER fight
    // instead of running: a worker told to attack has been told, and taking a
    // hit does not change the order. Any other order clears it, so the flee
    // reflex comes back the moment the player re-tasks them.
    void CommandAttack(Damageable* target);

    // Send a worker to finish a building.
    //
    // Clears any attack the way a move order does, so a worker pulled off a
    // fight to build gets its flee reflex back with it.
    void CommandBuild(Building* site);

    // Send a worker to gather this node, and take it off park. A null or
    // empty node is ignored.
    void CommandGather(ResourceNode* node);

    // --- Damage ------------------------------------------------------------

    void TakeDamage(int amount) override;

    // Kills it outright, and announces it. Emits unitDied EXACTLY once - the
    // phase guard inside is what guarantees that, and the enemy tally depends
    // on it.
    //
    // Does not unregister from the lane and does not remove itself from
    // anything, because it cannot: it does not know who owns it. Whoever does
    // keeps it as a tombstone, which is the rule Damageable.hpp states.
    void Kill();

    bool IsAlive() const override { return m_phase != State::Dead; }

    // Half the body width, so an attacker stops adjacent rather than standing
    // inside what it is hitting.
    float HitHalfWidth() const override { return m_stats.bodySize.x * 0.5f; }

    // --- What it is doing --------------------------------------------------

    State CurrentState() const { return m_phase; }
    int Hp() const { return m_hp; }
    const UnitStats& Stats() const { return m_stats; }
    const std::string& Faction() const { return m_stats.faction; }
    bool IsPlayer() const { return m_stats.faction == Factions::kPlayer; }

    glm::vec2 Position() const override { return m_position; }
    void SetPosition(const glm::vec2& position) { m_position = position; }

    // What it is carrying, and of what. Exposed because conservation - what
    // came out of a tree is either banked or still in hand - is the property
    // the economy is checked against.
    int Carrying() const { return m_carry; }
    const std::string& CarryResource() const { return m_carryResource; }

    // --- Save --------------------------------------------------------------

    // References become ids through the table. Three of the five are saved:
    // the gather target, the build target and the attack target.
    //
    // The deposit and the flee destination are NOT, and that is a deliberate
    // divergence from the original with a stated cost. Both are cached indices
    // into a world-owned list rather than entity references - an idea this port
    // introduced and the GDScript has no analogue for - and saving them would
    // force an id space onto every World implementation. A restored worker
    // stands still for one thinking tick and then re-acquires the NEAREST
    // deposit, which differs from the original only on a board with two Town
    // Halls that the shipped game never builds.
    Supersonic::Json::Value ToSave(const SidTable& ids) const;

    // Scalars only. References come back in a second pass, once every entity
    // exists - see Relink.
    void FromSave(const Supersonic::Json::Value& saved);

    // The second pass. Split from FromSave for the reason the original states:
    // a unit's target may be an entity that has not been rebuilt yet, so
    // nothing can be resolved until all of them are.
    void Relink(const Supersonic::Json::Value& saved, const SidResolver& resolver,
                int* unresolved = nullptr);

    ResourceNode* TargetNode() const { return m_targetNode; }
    const Building* BuildTarget() const { return m_buildTarget; }
    const Damageable* AttackTarget() const { return m_attackTarget; }
    bool OrderedToAttack() const { return m_orderedToAttack; }
    bool Parked() const { return m_parked; }
    const glm::vec2& MoveTarget() const { return m_moveTarget; }

private:
    void TickAi();
    void TickWorker();
    void TickDefender();
    void TickAggressor();
    void SeekWork();
    void BeginDelivering();

    void StepToward(const glm::vec2& target, double delta);
    bool ApproachTo(const glm::vec2& target, float range, double delta);
    void StepGather(double delta);
    void StepDeliver(double delta);
    void StepFlee(double delta);
    void StepAttack(double delta);
    void StepBuild(double delta);
    void FireProjectile();

    void FleeCheck();
    bool HasValidTarget() const;
    void ClearAttack();
    void AfterGathering();
    bool HasLiveNode() const;
    bool Arrived() const;
    float ClampToBand(float y) const;
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
    double m_aiAccumulator{0.0};

    // Worker economy.
    int m_carry{0};
    std::string m_carryResource;
    double m_gatherAccumulator{0.0};
    ResourceNode* m_targetNode{nullptr};
    Building* m_buildTarget{nullptr};
    int m_depositIndex{-1};

    // Set by a move order, cleared by a gather, build or attack order. A
    // parked worker that goes idle stays idle.
    bool m_parked{false};

    // Combat. Only the parts the worker needs are used here: an ordered worker
    // fights instead of fleeing, and that flag is what remembers it.
    Damageable* m_attackTarget{nullptr};
    bool m_orderedToAttack{false};
    int m_fleeIndex{-1};
    double m_attackCooldown{0.0};
};

} // namespace WolfBrigade
