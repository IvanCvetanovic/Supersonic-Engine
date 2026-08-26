#include "sim/Unit.hpp"

#include <algorithm>
#include <cmath>

#include "sim/Building.hpp"
#include "sim/Projectiles.hpp"

namespace WolfBrigade {

namespace {

// Godot's Vector2.move_toward: travel `distance` toward the target and stop
// there, never past it. Overshooting is what makes a unit vibrate on the spot
// once its speed exceeds the gap.
// The distance arrives as a double - speed times delta, both scalars - and is
// applied to a position that is not. Narrowed here, in one place, exactly where
// Godot narrows it: Vector2.move_toward takes a real_t.
glm::vec2 moveToward(const glm::vec2& from, const glm::vec2& to, double distance) {
    const glm::vec2 offset = to - from;
    const float length = glm::length(offset);
    const float travel = static_cast<float>(distance);
    if (length <= travel || length < 1e-6f) return to;
    return from + offset * (travel / length);
}

} // namespace

void Unit::Step(double delta) {
    // The board freezes on victory or defeat. A raider that kept walking after
    // the player lost would keep hitting a Town Hall that is already rubble.
    if (!m_state->IsPlaying()) return;
    if (m_phase == State::Dead) return;

    switch (m_phase) {
    case State::Moving:     StepToward(m_moveTarget, delta); break;
    case State::Gathering:  StepGather(delta); break;
    case State::Delivering: StepDeliver(delta); break;
    case State::Fleeing:    StepFlee(delta); break;
    case State::Attacking:  StepAttack(delta); break;
    case State::Building:   StepBuild(delta); break;
    default: break;
    }

    if (m_attackCooldown > 0.0) m_attackCooldown -= delta;

    // Once per interval, not once per step, and NOT a while loop: a step longer
    // than the interval thinks once rather than catching up. That is the
    // original's behaviour and it is the right one - a unit that ran eight
    // decisions after a hitch would act on seven stale views of the world.
    m_aiAccumulator += delta;
    if (m_aiAccumulator >= kAiTickInterval) {
        m_aiAccumulator -= kAiTickInterval;
        TickAi();
    }
}

void Unit::TickAi() {
    // Dispatched by behaviour. Anything that is not a fighter thinks like a
    // worker, which is the original's fallback and means a unit authored with
    // no behaviour still does something useful rather than standing still.
    if (m_stats.behavior == kSoldier || m_stats.behavior == kRanged) {
        TickDefender();
    } else if (m_stats.behavior == kAggressor) {
        TickAggressor();
    } else {
        TickWorker();
    }
}

// Soldiers and archers hold their ground and pick up whatever comes into aggro.
//
// A player's move order takes priority: there is no auto-acquire while MOVING,
// so ordering a soldier out of a fight actually gets it out. That is one `case`
// away from the opposite behaviour, where a retreat order is ignored by
// anything with a target in range.
void Unit::TickDefender() {
    if (HasValidTarget()) {
        if (m_phase != State::Attacking) SetState(State::Attacking);
        return;
    }

    // The target died, or there never was one. Dropped here so the flag does
    // not outlive the fight.
    ClearAttack();

    switch (m_phase) {
    case State::Moving:
        if (Arrived()) SetState(State::Idle);
        break;

    case State::Attacking:
        // Attacking nothing. Back to idle, where it can acquire again next
        // tick rather than standing in a fighting stance forever.
        SetState(State::Idle);
        break;

    case State::Idle:
        if (Unit* enemy = m_world->NearestEnemyUnit(m_stats.faction, m_position.x,
                                                    m_stats.aggroRange)) {
            m_attackTarget = enemy;
            SetState(State::Attacking);
        }
        break;

    default:
        break;
    }
}

// Raiders hit whatever is in aggro and otherwise walk at the Town Hall.
void Unit::TickAggressor() {
    if (HasValidTarget()) {
        if (m_phase != State::Attacking) SetState(State::Attacking);
        return;
    }
    ClearAttack();

    // ONE building scan per tick, reused for both the attack pick and the walk
    // goal. Asking twice is the same answer twice at twice the cost, on the
    // hottest path in the game.
    Damageable* building = m_world->NearestEnemyBuilding(m_stats.faction, m_position.x);
    Unit* enemy = m_world->NearestEnemyUnit(m_stats.faction, m_position.x, m_stats.aggroRange);

    Damageable* chosen = enemy;

    // Seeded with the aggro range when there is no unit, so a building only
    // wins if it is inside aggro too - a raider does not stop to punch a Town
    // Hall it can see from across the map, it walks to it first.
    float best = (enemy != nullptr) ? std::fabs(enemy->Position().x - m_position.x)
                                    : m_stats.aggroRange;
    if (building != nullptr && std::fabs(building->Position().x - m_position.x) <= best) {
        chosen = building;
    }

    if (chosen != nullptr) {
        m_attackTarget = chosen;
        SetState(State::Attacking);
        return;
    }

    // Nothing in reach: advance. Toward the nearest enemy building, or toward
    // the left edge once they have all fallen - which keeps a raider walking
    // rather than standing in an empty field after it has won.
    const float goalX = (building != nullptr) ? building->Position().x : 0.0f;
    m_moveTarget = glm::vec2(goalX, m_position.y);
    if (m_phase != State::Moving) SetState(State::Moving);
}

void Unit::TickWorker() {
    switch (m_phase) {
    case State::Moving:
        if (Arrived()) SetState(State::Idle);
        break;

    case State::Idle:
        SeekWork();
        break;

    case State::Gathering:
        // The tree ran out, or was taken by somebody else. Bank what is in
        // hand rather than standing over a stump.
        if (!HasLiveNode()) AfterGathering();
        break;

    case State::Building:
        // Finished, or gone, or knocked down. Either way there is nothing left
        // to pour into, and the next tick finds this worker something else.
        //
        // The aliveness check is what Godot's is_instance_valid does here: a
        // destroyed building is freed after its fade, so the original's worker
        // is released by the pointer going stale. Nothing goes stale in this
        // port, so the question has to be asked directly - otherwise a worker
        // stands over rubble pouring time into a building that cannot accept
        // it, forever.
        if (m_buildTarget == nullptr || m_buildTarget->IsComplete() ||
            !m_buildTarget->IsAlive()) {
            m_buildTarget = nullptr;
            SetState(State::Idle);
        }
        break;

    case State::Delivering:
        // Acquired HERE rather than when delivering began, because this is the
        // thinking tick and scans belong on it. StepDeliver simply waits for
        // the one tick it takes.
        if (!m_world->DepositExists(m_depositIndex)) {
            m_depositIndex = m_world->NearestDeposit(m_position.x);
        }
        break;

    case State::Fleeing:
        FleeCheck();
        break;

    case State::Attacking:
        // A worker only attacks under orders, and the order ends when the
        // target does.
        if (!HasValidTarget()) {
            ClearAttack();
            SetState(State::Idle);
        }
        break;

    default:
        break;
    }
}

void Unit::SeekWork() {
    // Bank first. A worker holding wood that wandered off to a fresh tree
    // would carry the same load forever, and the economy would look broken in
    // a way nothing points at.
    if (m_carry > 0) {
        BeginDelivering();
        return;
    }

    // Construction BEFORE gathering, and the order is the point. A worker that
    // preferred a tree would leave a half-built barracks standing until every
    // node on the map ran dry - and since any idle worker picks a site up, the
    // whole village would walk past it together.
    if (Building* site = m_world->NearestUnfinishedBuilding(m_stats.faction, m_position.x)) {
        m_buildTarget = site;
        SetState(State::Building);
        return;
    }

    if (ResourceNode* node = m_world->NearestHarvestable(m_position.x)) {
        m_targetNode = node;
        SetState(State::Gathering);
    }
}

void Unit::BeginDelivering() { SetState(State::Delivering); }

void Unit::StepToward(const glm::vec2& target, double delta) {
    m_position = moveToward(m_position, target, static_cast<double>(m_stats.moveSpeed) * delta);
}

void Unit::StepGather(double delta) {
    if (!HasLiveNode()) return;   // the tick transitions out of here

    const float nodeX = m_targetNode->position.x;
    if (std::fabs(m_position.x - nodeX) > m_stats.gatherRange) {
        StepToward(glm::vec2(nodeX, m_position.y), delta);
        return;
    }

    // Fractional accumulation, integer extraction. gather_rate is a rate per
    // second and wood is a whole number, so the remainder has to be kept: a
    // version that truncated every step would gather nothing at all below one
    // unit per step, which at 1.0/sec and a sixtieth of a second is always.
    m_gatherAccumulator += static_cast<double>(m_stats.gatherRate) * delta;

    const int want = std::min(static_cast<int>(m_gatherAccumulator),
                              m_stats.carryCapacity - m_carry);
    if (want > 0) {
        const int got = m_targetNode->Extract(want);
        m_carry += got;
        m_carryResource = m_targetNode->resource;

        // Only what actually came out. Subtracting what was ASKED for would
        // lose the remainder against a nearly-empty tree, which is the
        // conservation property the whole economy is checked on.
        m_gatherAccumulator -= static_cast<double>(got);
    }

    if (m_carry >= m_stats.carryCapacity || !HasLiveNode()) BeginDelivering();
}

void Unit::StepDeliver(double delta) {
    if (!m_world->DepositExists(m_depositIndex)) return;   // the tick re-acquires

    const float depositX = m_world->DepositPosition(m_depositIndex).x;
    if (std::fabs(m_position.x - depositX) > m_stats.depositRange) {
        StepToward(glm::vec2(depositX, m_position.y), delta);
        return;
    }

    if (m_carry > 0) {
        m_state->Add(m_carryResource, m_carry);
        m_carry = 0;
    }
    SetState(State::Idle);
}

void Unit::StepFlee(double delta) {
    if (!m_world->DepositExists(m_fleeIndex)) return;
    StepToward(glm::vec2(m_world->DepositPosition(m_fleeIndex).x, m_position.y), delta);
}

// Close to reach, then hit once per attack interval.
//
// The two behaviours differ only here. An archer holds at its attack range and
// looses arrows; everything else walks until it is touching and swings. Getting
// the melee reach wrong by the target's half-width is what makes an attacker
// stand inside the Town Hall it is demolishing.
void Unit::StepAttack(double delta) {
    if (!HasValidTarget()) return;   // the tick transitions out

    const float targetX = m_attackTarget->Position().x;

    if (m_stats.behavior == kRanged) {
        if (std::fabs(m_position.x - targetX) > m_stats.attackRange) {
            StepToward(glm::vec2(targetX, m_position.y), delta);
            return;
        }
        if (m_attackCooldown <= 0.0) {
            // Guarded against a zero rate, which would divide by nothing and
            // give a unit that attacks infinitely often. A unit authored with
            // no attack speed shoots once every hundred seconds instead.
            m_attackCooldown = 1.0 / std::max(static_cast<double>(m_stats.attacksPerSec), 0.01);
            FireProjectile();
        }
        return;
    }

    const float reach = m_stats.attackRange + m_attackTarget->HitHalfWidth();
    if (std::fabs(m_position.x - targetX) > reach) {
        StepToward(glm::vec2(targetX, m_position.y), delta);
        return;
    }
    if (m_attackCooldown <= 0.0) {
        m_attackCooldown = 1.0 / std::max(static_cast<double>(m_stats.attacksPerSec), 0.01);
        m_attackTarget->TakeDamage(m_stats.damage);
    }
}

void Unit::FireProjectile() {
    ProjectilePool* pool = m_world->Projectiles();
    if (pool == nullptr) return;

    // From the middle of the body rather than its feet, so an arrow leaves the
    // archer's chest.
    const glm::vec2 from = m_position + glm::vec2(0.0f, -m_stats.bodySize.y * 0.5f);
    pool->Spawn(from, m_attackTarget, m_stats.damage, m_stats.projectileSpeed);
}

bool Unit::HasValidTarget() const {
    return m_attackTarget != nullptr && m_attackTarget->IsAlive();
}

void Unit::ClearAttack() {
    m_attackTarget = nullptr;
    m_orderedToAttack = false;
}

// Walk to the site, then pour time into it.
//
// The reach is the building's half-width plus the worker's GATHER range, not
// its attack range - a worker builds from where it would harvest, and the two
// numbers are different in the data for a reason.
void Unit::StepBuild(double delta) {
    if (m_buildTarget == nullptr || m_buildTarget->IsComplete() || !m_buildTarget->IsAlive()) {
        return;   // the tick clears it
    }

    const float reach = m_buildTarget->Stats().bodySize.x * 0.5f + m_stats.gatherRange;
    const float siteX = m_buildTarget->Position().x;
    if (std::fabs(m_position.x - siteX) > reach) {
        StepToward(glm::vec2(siteX, m_position.y), delta);
        return;
    }

    // A second of a worker's time is a second of build time. The building
    // clamps at its total and announces its own completion.
    m_buildTarget->AddBuildProgress(delta);
}

void Unit::FleeCheck() {
    if (!m_world->DepositExists(m_fleeIndex)) m_fleeIndex = m_world->NearestDeposit(m_position.x);

    // Reached safety, or there is nowhere to run to. Either way, back to work:
    // a worker cowering forever next to a Town Hall that was destroyed is a
    // player watching their economy stop for no visible reason.
    if (m_fleeIndex < 0 ||
        std::fabs(m_position.x - m_world->DepositPosition(m_fleeIndex).x) <= m_stats.depositRange) {
        SetState(State::Idle);
    }
}

void Unit::AfterGathering() {
    if (m_carry > 0) {
        BeginDelivering();
    } else {
        SetState(State::Idle);
    }
}

bool Unit::HasLiveNode() const { return m_targetNode != nullptr && !m_targetNode->IsEmpty(); }

bool Unit::Arrived() const { return glm::distance(m_position, m_moveTarget) <= kArriveThreshold; }

void Unit::CommandMoveTo(const glm::vec2& target) {
    if (m_phase == State::Dead) return;

    // An explicit order drops the acquired target AND the order-to-attack flag,
    // which is what restores a worker's flee reflex: a worker told to fight and
    // then told to go somewhere else is not still under orders to fight.
    ClearAttack();

    m_moveTarget = glm::vec2(target.x, m_position.y);
    SetState(State::Moving);
}

void Unit::CommandBuild(Building* site) {
    if (m_phase == State::Dead || site == nullptr) return;

    // Re-task: drop any attack order, so a worker sent from a fight to a
    // building has its flee reflex back.
    ClearAttack();

    m_buildTarget = site;
    SetState(State::Building);
}

void Unit::CommandAttack(Damageable* target) {
    if (m_phase == State::Dead || target == nullptr) return;
    m_attackTarget = target;
    m_orderedToAttack = true;
    SetState(State::Attacking);
}

void Unit::TakeDamage(int amount) {
    if (m_phase == State::Dead) return;

    m_hp = std::max(m_hp - amount, 0);

    // Announced BEFORE the death check, so a killing blow still shows its
    // number. A floating "12" that never appears on the hit that mattered is
    // the one the player most wants to see.
    m_bus->damageDealt.Emit(m_position + glm::vec2(0.0f, -m_stats.bodySize.y), amount,
                            m_stats.faction);

    if (m_hp <= 0) {
        Kill();
        return;
    }

    // A worker runs for the Town Hall when hit - unless it was told to fight,
    // in which case it fights. Resolved HERE, on the hit, rather than by a
    // per-frame scan: being attacked is an event.
    if (m_stats.behavior == kWorker && !m_orderedToAttack && m_phase != State::Fleeing) {
        m_fleeIndex = m_world->NearestDeposit(m_position.x);
        SetState(State::Fleeing);
    }
}

void Unit::Kill() {
    if (m_phase == State::Dead) return;
    SetState(State::Dead);
}

Supersonic::Json::Value Unit::ToSave(const SidTable& ids) const {
    Supersonic::Json::Object out;
    out["id"] = Supersonic::Json::Value(m_stats.id);
    out["stats"] = m_stats.ToBlock();
    out["hp"] = Supersonic::Json::Value(static_cast<double>(m_hp));
    out["state"] = Supersonic::Json::Value(static_cast<double>(static_cast<int>(m_phase)));

    Supersonic::Json::Array pos;
    pos.push_back(Supersonic::Json::Value(static_cast<double>(m_position.x)));
    pos.push_back(Supersonic::Json::Value(static_cast<double>(m_position.y)));
    out["pos"] = Supersonic::Json::Value(std::move(pos));

    Supersonic::Json::Array target;
    target.push_back(Supersonic::Json::Value(static_cast<double>(m_moveTarget.x)));
    target.push_back(Supersonic::Json::Value(static_cast<double>(m_moveTarget.y)));
    out["move_target"] = Supersonic::Json::Value(std::move(target));

    out["carry"] = Supersonic::Json::Value(static_cast<double>(m_carry));
    out["carry_resource"] = Supersonic::Json::Value(m_carryResource);
    out["attack_cd"] = Supersonic::Json::Value(m_attackCooldown);
    out["ordered_to_attack"] = Supersonic::Json::Value(m_orderedToAttack);

    // The upcast is deliberate and load-bearing - see SaveIds.hpp. A build
    // target is a Building and is upcast the same way its own entry was keyed.
    out["ref_tree"] = Supersonic::Json::Value(static_cast<double>(ids.Of(m_targetNode)));
    out["ref_build"] = Supersonic::Json::Value(
        static_cast<double>(ids.Of(static_cast<const Damageable*>(m_buildTarget))));
    out["ref_attack"] = Supersonic::Json::Value(static_cast<double>(ids.Of(m_attackTarget)));
    return Supersonic::Json::Value(std::move(out));
}

void Unit::FromSave(const Supersonic::Json::Value& saved) {
    m_hp = static_cast<int>(saved["hp"].AsNumber(static_cast<double>(m_stats.maxHp)));

    // Assigned directly rather than through SetState, which would announce a
    // transition that did not happen - and NOT through a command, which would
    // clear the very targets Relink is about to restore.
    const int state = static_cast<int>(saved["state"].AsNumber(0.0));
    m_phase = (state >= 0 && state <= 7) ? static_cast<State>(state) : State::Idle;

    const auto& target = saved["move_target"].AsArray();
    if (target.size() >= 2) {
        m_moveTarget = glm::vec2(target[0].AsFloat(), target[1].AsFloat());
    } else {
        // Its own position, not the origin. A unit reloaded mid-walk with no
        // saved destination would otherwise set off for the left edge of the
        // world.
        m_moveTarget = m_position;
    }

    m_carry = static_cast<int>(saved["carry"].AsNumber(0.0));
    m_carryResource = saved["carry_resource"].AsString("");
    m_attackCooldown = saved["attack_cd"].AsNumber(0.0);
    m_orderedToAttack = saved["ordered_to_attack"].AsBool(false);
}

void Unit::Relink(const Supersonic::Json::Value& saved, const SidResolver& resolver,
                  int* unresolved) {
    const auto resolve = [&saved, unresolved](const char* key, auto lookup) {
        const int sid = static_cast<int>(saved[key].AsNumber(-1.0));
        if (sid < 0) return decltype(lookup(sid)){nullptr};
        auto* found = lookup(sid);

        // An id that resolves to nothing is counted rather than ignored. It
        // means either a referent the capture skipped - legitimate - or a file
        // that has been edited, and the caller has to be able to tell the
        // difference from a clean restore.
        if (found == nullptr && unresolved != nullptr) ++(*unresolved);
        return found;
    };

    m_targetNode = resolve("ref_tree", [&resolver](int sid) { return resolver.AsNode(sid); });
    m_buildTarget =
        resolve("ref_build", [&resolver](int sid) { return resolver.AsBuilding(sid); });

    // The polymorphic one, and the whole reason the id space is single: this
    // may be a Unit or a Building and the file carries no type tag.
    m_attackTarget =
        resolve("ref_attack", [&resolver](int sid) { return resolver.AsDamageable(sid); });
}

void Unit::SetState(State next) {
    if (m_phase == next) return;
    m_phase = next;
}

} // namespace WolfBrigade
