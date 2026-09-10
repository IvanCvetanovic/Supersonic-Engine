#pragma once

#include <cmath>
#include <limits>
#include <map>
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
//
// The priest is the fifth behaviour and the fourth routine. It heals rather
// than fights, and it is the only unit a player's attack order is refused by.
class Unit final : public Damageable {
public:
    // The values are a SAVE FORMAT: `state: 2` in a file is Gathering. The
    // original appended CONTROLLED and HEALING after DEAD rather than slotting
    // them in where they read best, and so does this, so every save written
    // before they existed still means what it said.
    enum class State {
        Idle, Moving, Gathering, Delivering, Building, Attacking, Fleeing, Dead,
        Controlled, Healing
    };

    // Behaviours, as authored in units.json.
    static constexpr const char* kWorker = "worker";
    static constexpr const char* kSoldier = "soldier";
    static constexpr const char* kRanged = "ranged";
    static constexpr const char* kAggressor = "aggressor";
    static constexpr const char* kHealer = "healer";

    // ~8 Hz. A performance knob, not a game stat - which is why it is a
    // constant here and not a number in the data files.
    static constexpr double kAiTickInterval = 0.125;

    // Close enough to count as arrived. Without a threshold a unit oscillates
    // around its target forever, one sub-pixel step at a time.
    static constexpr float kArriveThreshold = 3.0f;

    Unit(const UnitStats& stats, GameState& state, EventBus& bus, World& world)
        : m_stats(stats), m_state(&state), m_bus(&bus), m_world(&world), m_hp(stats.maxHp),
          m_facing(stats.faction == Factions::kEnemy ? -1.0 : 1.0) {}

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
    //
    // A healer has no attack at all, so for it the order is a no-op: in a
    // mixed selection the soldiers take it and the priest keeps healing.
    void CommandAttack(Damageable* target);

    // Send a worker to finish a building.
    //
    // Clears any attack the way a move order does, so a worker pulled off a
    // fight to build gets its flee reflex back with it.
    void CommandBuild(Building* site);

    // Send a worker to gather this node, and take it off park. A null or
    // empty node is ignored.
    void CommandGather(ResourceNode* node);

    // --- Thinking ----------------------------------------------------------

    // The ~8 Hz decision, taken once, now. Public because the original's
    // harnesses call _tick_ai() by hand between moves they make themselves,
    // and the squad and healer numbers cannot be reproduced any other way.
    // Step calls it on its own clock; nothing in the game calls it from
    // outside.
    void TickAi();

    // --- Squads ------------------------------------------------------------

    // Squads::kGarrison or kWarband. Only a can_follow unit's squad means
    // anything, and everyone starts in the garrison.
    const std::string& Squad() const { return m_squad; }
    void SetSquad(const std::string& squad) { m_squad = squad; }

    // Where a garrison unit belongs: its rally point, else where it was
    // trained. NoPost() for none - the original's Vector2.INF - which is what
    // every unit the boot or a wave spawns has, and such a unit holds where
    // it stands.
    const glm::vec2& HomePost() const { return m_homePost; }
    void SetHomePost(const glm::vec2& post) { m_homePost = post; }
    static glm::vec2 NoPost() { return glm::vec2(std::numeric_limits<float>::infinity()); }
    static bool HasPost(const glm::vec2& post) { return std::isfinite(post.x); }

    // What spreads a warband's slots so it does not stack on one point. The
    // original uses get_instance_id(); FollowSlot says why that is not
    // reproduced. Whoever owns the unit sets it - the Match uses its index.
    void SetFormationKey(int key) { m_formationKey = key; }

    // +1 facing right, -1 left. Presentation almost everywhere, but a
    // follower's slot hangs behind the hero's facing, so the sim reads it.
    double Facing() const { return m_facing; }

    // --- Healing -----------------------------------------------------------

    // A friendly cast. Clamped at max hp, a no-op on a full or dead unit, and
    // announced on `healed` with what ACTUALLY came back. Deliberately does
    // NOT touch the regen clock: being healed is not being hit.
    void ReceiveHeal(int amount);

    // The ally a priest is channelling on, or null.
    const Unit* HealTarget() const { return m_healTarget; }

    // --- Direct control ----------------------------------------------------
    //
    // The hero, steered by the player. HeroControl drives it and nothing else
    // should. It is transient input: never saved, and a load of CONTROLLED
    // comes back IDLE.

    // On; or off, back to IDLE so the AI re-acquires on its next tick. A dead
    // unit stays dead, and asking for the state it is already in does
    // nothing.
    void SetControlled(bool on);
    bool IsControlled() const { return m_phase == State::Controlled; }

    // The steering intent, clamped to unit length so a diagonal never outruns
    // move_speed. Zero stands still.
    void SetControlDir(const glm::vec2& dir);
    const glm::vec2& ControlDir() const { return m_controlDir; }

    // A manual strike: toward a point (a click), or at the nearest enemy in
    // reach (the touch button). Cooldown-gated, and refused on a frozen
    // board. It swings even with nothing in reach, so the input always reads
    // back; damage lands only on an enemy within lane reach.
    void ControlledAttackAt(const glm::vec2& worldPos);
    void ControlledAttackAuto();

    // Seconds until the next swing or cast. Read-only: the original's harness
    // zeroes it by hand, and the port's cases step past it instead.
    double AttackCooldown() const { return m_attackCooldown; }

    // --- Abilities ---------------------------------------------------------
    //
    // abilities.json, by slot: stats.abilities lists the ids in hotkey and
    // button order. Only a directly controlled unit casts; the AI never
    // touches these.

    // Cast slot `index`. Refused under AI, out of range, on a frozen board,
    // while that ability cools down, and for an id the data does not define.
    void UseAbility(int index);

    // Seconds left, 0 when ready or unknown. Cooldowns tick in every state,
    // so leaving and re-entering control never resets one, and they ride the
    // save, so a reload cannot skip one.
    double AbilityCooldownLeft(const std::string& abilityId) const;

    // Dash i-frames: while they last a hit is dodged entirely - it neither
    // hurts nor pauses regen.
    bool IsInvulnerable() const { return m_invuln > 0.0; }

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

    // A raw write, as the original's public `hp` field takes one. Its
    // harnesses wound and restore units this way; nothing here is announced
    // and nothing dies of it.
    void SetHp(int hp) { m_hp = hp; }
    const UnitStats& Stats() const { return m_stats; }

    // What a blow or an arrow of this unit deals: its damage times the army
    // bonus from held capture points, rounded, and never under one. The
    // player's units only - an enemy holding a point is denial, not a buff.
    int EffectiveDamage() const;
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
    void TickWorker();
    void TickSheltered();
    void TickDefender();
    void TickAggressor();
    void TickHealer();
    void SeekWork();
    void BeginDelivering();

    Unit* AcquireEnemy() const;
    bool FollowingHero() const;
    bool WarbandLeashBroken() const;
    bool HealerLeashed() const;
    double LeashPx() const;
    void FollowHero();
    glm::vec2 FollowSlot() const;
    void ReturnHome();

    void StepToward(const glm::vec2& target, double delta);
    bool ApproachTo(const glm::vec2& target, float range, double delta);
    void StepGather(double delta);
    void StepDeliver(double delta);
    void StepFlee(double delta);
    void StepAttack(double delta);
    void StepBuild(double delta);
    void StepHeal(double delta);
    bool ValidHealTarget() const;
    void FireProjectile();
    void Face(double dx);

    void StepControlled(double delta);
    void MoveClamped(const glm::vec2& step);
    void ControlledStrike();
    Unit* StrikeTarget() const;
    void CastAoeDamage(const Supersonic::Json::Value& def);
    void CastDash(const Supersonic::Json::Value& def);

    void StepRegen(double delta);

    void FleeCheck();
    bool HasValidTarget() const;
    void ClearAttack();
    void AfterGathering();
    bool HasLiveNode() const;
    bool Arrived() const;
    float ClampToBand(double y) const;
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

    // Passive regen: seconds since the last hit, starting out of combat, and
    // the fraction of a hit point owed. Neither is saved, as in the original.
    double m_sinceDamage{1.0e9};
    double m_regenAccumulator{0.0};

    // Squads. The squad and the post are saved; the march flag and the
    // formation key are not, as in the original.
    std::string m_squad{Squads::kGarrison};
    glm::vec2 m_homePost{NoPost()};

    // MOVING because the AI sent it - home, or after the hero - rather than
    // the player. Such a march stays combat-aware; a player's order keeps its
    // no-auto-acquire contract, which is why CommandMoveTo clears this.
    bool m_aiMarch{false};
    int m_formationKey{0};

    // The priest's patient. Saved, as ref_heal.
    Unit* m_healTarget{nullptr};

    // Set in the constructor: a raider comes in facing left.
    double m_facing{1.0};

    // The player's steering while CONTROLLED. Input, not state: never saved.
    glm::vec2 m_controlDir{0.0f};

    // Abilities. The cooldowns are saved; a dash in flight and its i-frames
    // are not, as in the original.
    std::map<std::string, double> m_abilityCds;
    double m_invuln{0.0};
    double m_dashLeft{0.0};
    glm::vec2 m_dashDir{1.0f, 0.0f};
    double m_dashSpeed{900.0};
};

} // namespace WolfBrigade
