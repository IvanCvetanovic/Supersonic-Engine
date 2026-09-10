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

// Godot's Vector2.normalized divides by the length rather than multiplying by
// an inverse square root, and the last bit can differ - which a steered or
// dashing hero's position then accumulates. So this divides too.
glm::vec2 godotNormalized(const glm::vec2& v) {
    const float lengthSq = v.x * v.x + v.y * v.y;
    if (lengthSq == 0.0f) return v;
    const float length = std::sqrt(lengthSq);
    return glm::vec2(v.x / length, v.y / length);
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
    case State::Controlled: StepControlled(delta); break;
    case State::Healing:    StepHeal(delta); break;
    default: break;
    }

    StepRegen(delta);
    if (m_attackCooldown > 0.0) m_attackCooldown -= delta;
    if (m_invuln > 0.0) m_invuln -= delta;
    for (auto& [id, left] : m_abilityCds) left = std::max(0.0, left - delta);

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
    // The player is steering: no decision of any kind, not even a scan.
    if (m_phase == State::Controlled) return;

    // Dispatched by behaviour. Anything that is not a fighter or a healer
    // thinks like a worker, which is the original's fallback and means a unit
    // authored with no behaviour still does something useful rather than
    // standing still.
    if (m_stats.behavior == kSoldier || m_stats.behavior == kRanged) {
        TickDefender();
    } else if (m_stats.behavior == kAggressor) {
        TickAggressor();
    } else if (m_stats.behavior == kHealer) {
        TickHealer();
    } else {
        TickWorker();
    }
}

// Soldiers and archers. The GARRISON holds its post: it picks up whatever
// comes into aggro while idle and walks back to its post after a fight. The
// WARBAND keeps station on the hero, engages anything in aggro even on the
// march, and breaks off a fight that drags it past the leash.
//
// A player's move order still takes priority: there is no auto-acquire while
// MOVING under orders, so ordering a soldier out of a fight gets it out. That
// is one `case` away from the opposite behaviour, where a retreat order is
// ignored by anything with a target in range. A march the AI gave itself is
// different and stays combat-aware - the original's first build had a
// garrison walk home straight past the raiders hitting it.
void Unit::TickDefender() {
    if (HasValidTarget()) {
        if (WarbandLeashBroken()) {
            // Too far from the hero: drop the fight and run back.
            ClearAttack();
            FollowHero();
            return;
        }
        if (m_phase != State::Attacking) SetState(State::Attacking);
        return;
    }

    // The target died, or there never was one. Dropped here so the flag does
    // not outlive the fight.
    ClearAttack();

    switch (m_phase) {
    case State::Moving:
        if (FollowingHero()) {
            if (Unit* enemy = AcquireEnemy()) {
                m_attackTarget = enemy;
                SetState(State::Attacking);
            } else {
                FollowHero();   // the hero moves, so the slot does
            }
        } else if (Unit* threat = m_aiMarch ? AcquireEnemy() : nullptr) {
            m_attackTarget = threat;
            SetState(State::Attacking);
        } else if (Arrived()) {
            m_aiMarch = false;
            SetState(State::Idle);
        }
        break;

    case State::Attacking:
        // Attacking nothing. Back to idle, where it can acquire again next
        // tick rather than standing in a fighting stance forever.
        SetState(State::Idle);
        break;

    case State::Idle:
        if (Unit* enemy = AcquireEnemy()) {
            m_attackTarget = enemy;
            SetState(State::Attacking);
        } else if (FollowingHero()) {
            FollowHero();
        } else {
            ReturnHome();
        }
        break;

    default:
        break;
    }
}

// The defender's scan, LEASH-AWARE: a warband unit past the leash sees nothing
// until it is back at the hero's side. Without this the break-off and the
// re-acquire alternated at the tick rate and the unit never disengaged - the
// original's review measured it at 93% of its speed lost.
Unit* Unit::AcquireEnemy() const {
    if (WarbandLeashBroken()) return nullptr;
    return m_world->NearestEnemyUnit(m_stats.faction, m_position.x, m_stats.aggroRange);
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
    // The shelter bell outranks everything a worker was doing.
    if (m_state->WorkersSheltered()) {
        TickSheltered();
        return;
    }

    switch (m_phase) {
    case State::Moving:
        if (Arrived()) SetState(State::Idle);
        break;

    case State::Idle:
        // A worker the player moved holds where it was put. Only a gather,
        // build or attack order takes it off park.
        if (!m_parked) SeekWork();
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
    // node on the map ran dry.
    //
    // Only when the economy lets workers volunteer. `auto_assist_build` ships
    // ON: the hero places a site and the village builds it unprompted. OFF,
    // only the worker placement sent, or one the player orders onto the site,
    // builds it. Read on the tick, as the original reads it, so a level that
    // overrides it takes effect at once.
    if (m_state->Data().Economy()["auto_assist_build"].AsBool(false)) {
        if (Building* site = m_world->NearestUnfinishedBuilding(m_stats.faction, m_position.x)) {
            m_buildTarget = site;
            SetState(State::Building);
            return;
        }
    }

    if (ResourceNode* node = m_world->NearestHarvestable(m_position.x)) {
        m_targetNode = node;
        SetState(State::Gathering);
    }
}

void Unit::BeginDelivering() { SetState(State::Delivering); }

// The shelter bell: drop the task in hand and hole up at the nearest drop-off
// until the bell rings again. It reuses the flee movement. Once there the
// worker just stands - nothing here seeks work, so it is the ordinary tick,
// after the bell is released, that restarts the economy. A load in hand is
// kept, and banked on the first delivery after.
void Unit::TickSheltered() {
    if (m_phase == State::Fleeing) {
        FleeCheck();
        return;
    }
    const int safe = m_world->NearestDeposit(m_position.x);
    if (safe < 0 ||
        glm::distance(m_position, m_world->DepositPosition(safe)) <= m_stats.depositRange) {
        if (m_phase != State::Idle) SetState(State::Idle);
        return;
    }
    m_fleeIndex = safe;
    SetState(State::Fleeing);
}

// Priests. Healing outranks everything; after it a warband priest keeps
// station on the hero, a battle medic, and a garrison priest goes back to its
// post. Never an attack - CommandAttack refuses them.
void Unit::TickHealer() {
    switch (m_phase) {
    case State::Moving:
        if (Unit* hurt = m_world->NearestWoundedAlly(this, m_stats.aggroRange)) {
            m_healTarget = hurt;
            SetState(State::Healing);
        } else if (FollowingHero()) {
            FollowHero();
        } else if (Arrived()) {
            SetState(State::Idle);
        }
        break;

    case State::Idle:
        if (Unit* hurt = m_world->NearestWoundedAlly(this, m_stats.aggroRange)) {
            m_healTarget = hurt;
            SetState(State::Healing);
        } else if (FollowingHero()) {
            FollowHero();
        } else {
            ReturnHome();
        }
        break;

    case State::Healing:
        // The patient is well, or gone, or has dragged the priest past its
        // leash. Idle, and the next tick decides what comes next.
        if (!ValidHealTarget() || HealerLeashed()) {
            m_healTarget = nullptr;
            SetState(State::Idle);
        }
        break;

    default:
        break;
    }
}

// --- Squads ----------------------------------------------------------------

double Unit::LeashPx() const {
    return m_state->Data().Economy()["warband_leash_px"].AsNumber(500.0);
}

// In the warband AND there is a living hero to follow. A warband unit whose
// hero is down is a garrison with no post: it holds where it stands.
bool Unit::FollowingHero() const {
    if (m_squad != Squads::kWarband) return false;
    const Unit* hero = m_world->Hero();
    return hero != nullptr && hero->IsAlive();
}

// Measured along the lane from the hero, as every range in this game is. The
// follow DESTINATION is 2D; the leash is not, and the two are not unified.
bool Unit::WarbandLeashBroken() const {
    if (!FollowingHero()) return false;
    const double dx = static_cast<double>(m_position.x) -
                      static_cast<double>(m_world->Hero()->Position().x);
    return std::fabs(dx) > LeashPx();
}

// A priest mid-chase gives up past its leash. Without this a priest glued to a
// faster wounded target - usually the hero - trailed it across the map, and
// the home horn could never recall one that was channelling. The warband
// measures from the hero and the garrison from its post; both use the same
// leash.
bool Unit::HealerLeashed() const {
    if (FollowingHero()) return WarbandLeashBroken();
    if (m_squad == Squads::kGarrison && HasPost(m_homePost)) {
        const double dx = static_cast<double>(m_position.x) - static_cast<double>(m_homePost.x);
        return std::fabs(dx) > LeashPx();
    }
    return false;
}

// Keep station on the hero: walk to this unit's slot when out of slack, stand
// once inside it.
void Unit::FollowHero() {
    const glm::vec2 slot = FollowSlot();
    const double slack = m_state->Data().Economy()["warband_follow_slack_px"].AsNumber(46.0);
    if (glm::distance(m_position, slot) <= slack) {
        if (m_phase != State::Idle) SetState(State::Idle);
        return;
    }
    m_moveTarget = slot;
    if (m_phase != State::Moving) SetState(State::Moving);
}

// A slot behind the hero's facing, fanned over three columns 38 px apart and
// three rows 42 px apart, so the band does not stack on one point.
//
// THE SLOT IS NOT REPRODUCED, and no test may pin one. The original keys the
// fan-out on get_instance_id(), an allocation counter nobody can predict or
// replay; this puts the same arithmetic on the formation key. The original's
// harness asserts only that a follower marches to the hero's side and closes
// most of the gap, and that is what the port reproduces.
glm::vec2 Unit::FollowSlot() const {
    const Unit* hero = m_world->Hero();
    const double gap = m_state->Data().Economy()["warband_follow_gap_px"].AsNumber(70.0);
    const double col = static_cast<double>(m_formationKey % 3) * 38.0;
    const double row = (static_cast<double>((m_formationKey >> 2) % 3) - 1.0) * 42.0;
    const glm::vec2 at = hero->Position();
    return glm::vec2(static_cast<float>(static_cast<double>(at.x) - hero->Facing() * (gap + col)),
                     ClampToBand(static_cast<double>(at.y) + row));
}

// A garrison unit that has wandered - chased something, was horn-recalled -
// walks back to its post, combat-aware on the way. No post, or already within
// 60 px of it, and it holds. The 60 is the original's own constant.
void Unit::ReturnHome() {
    if (m_squad != Squads::kGarrison || !HasPost(m_homePost)) return;
    const double dx = static_cast<double>(m_position.x) - static_cast<double>(m_homePost.x);
    if (std::fabs(dx) <= 60.0) return;
    m_moveTarget = m_homePost;
    m_aiMarch = true;
    SetState(State::Moving);
}

// --- Healing ---------------------------------------------------------------

// Walk to cast reach - attack_range doubles as it - then restore heal_amount
// once per 1/attacks_per_sec. The cadence shares the attack cooldown, so a
// save and a pause treat it exactly as they treat combat.
void Unit::StepHeal(double delta) {
    if (!ValidHealTarget()) return;   // the tick transitions out
    Face(static_cast<double>(m_healTarget->Position().x) - static_cast<double>(m_position.x));

    const float reach = m_stats.attackRange + m_healTarget->HitHalfWidth();
    if (!ApproachTo(m_healTarget->Position(), reach, delta)) return;
    if (m_attackCooldown <= 0.0) {
        m_attackCooldown = 1.0 / std::max(static_cast<double>(m_stats.attacksPerSec), 0.01);
        m_healTarget->ReceiveHeal(m_stats.healAmount);
    }
}

bool Unit::ValidHealTarget() const {
    return m_healTarget != nullptr && m_healTarget->IsAlive() &&
           m_healTarget->Hp() < m_healTarget->Stats().maxHp;
}

void Unit::ReceiveHeal(int amount) {
    if (m_phase == State::Dead || amount <= 0 || m_hp >= m_stats.maxHp) return;
    const int restored = std::min(amount, m_stats.maxHp - m_hp);
    m_hp += restored;
    if (m_bus) m_bus->healed.Emit(m_position + glm::vec2(0.0f, -m_stats.bodySize.y), restored);
}

// Turn toward where it is heading, or what it is working on. A dead zone of a
// pixel keeps a unit from flickering at a target dead ahead.
void Unit::Face(double dx) {
    if (std::fabs(dx) < 1.0) return;
    m_facing = dx > 0.0 ? 1.0 : -1.0;
}

// --- Direct control --------------------------------------------------------

void Unit::SetControlled(bool on) {
    if (m_phase == State::Dead || on == (m_phase == State::Controlled)) return;
    m_controlDir = glm::vec2(0.0f);
    m_dashLeft = 0.0;   // a dash in flight dies with the mode; i-frames just expire
    if (on) {
        ClearAttack();
        SetState(State::Controlled);
    } else {
        SetState(State::Idle);
    }
}

void Unit::SetControlDir(const glm::vec2& dir) {
    const float length = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    m_controlDir = length > 1.0f ? godotNormalized(dir) : dir;
}

// Full 2D inside the walkable band at the unit's own move_speed - the clamp a
// move order uses. Combat stays on the lane: the strike measures |dx| only.
void Unit::StepControlled(double delta) {
    // A dash in flight overrides the steering until its distance is spent.
    if (m_dashLeft > 0.0) {
        const double step = std::min(m_dashLeft, m_dashSpeed * delta);
        m_dashLeft -= step;
        Face(static_cast<double>(m_dashDir.x));
        MoveClamped(m_dashDir * static_cast<float>(step));
        return;
    }
    if (m_controlDir == glm::vec2(0.0f)) return;
    Face(static_cast<double>(m_controlDir.x));
    MoveClamped(m_controlDir *
                static_cast<float>(static_cast<double>(m_stats.moveSpeed) * delta));
}

// One clamped displacement: x inside the world, y inside the band.
void Unit::MoveClamped(const glm::vec2& step) {
    glm::vec2 next = m_position + step;
    const double width = m_state->Data().World()["width"].AsNumber(6000.0);
    next.x = static_cast<float>(std::clamp(static_cast<double>(next.x), 0.0, width));
    next.y = ClampToBand(static_cast<double>(next.y));
    m_position = next;
}

// A click: face the point first, so the swing lands on the side the player
// aimed at. IsPlaying because keys leak through the game-over overlay, and the
// board is frozen.
void Unit::ControlledAttackAt(const glm::vec2& worldPos) {
    if (m_phase != State::Controlled || m_attackCooldown > 0.0 || !m_state->IsPlaying()) return;
    Face(static_cast<double>(worldPos.x) - static_cast<double>(m_position.x));
    ControlledStrike();
}

// The touch button: face the nearest enemy in reach first, so the button
// works whichever side the threat is on.
void Unit::ControlledAttackAuto() {
    if (m_phase != State::Controlled || m_attackCooldown > 0.0 || !m_state->IsPlaying()) return;
    if (const Unit* target = StrikeTarget()) {
        Face(static_cast<double>(target->Position().x) - static_cast<double>(m_position.x));
    }
    ControlledStrike();
}

// One cooldown-gated swing.
void Unit::ControlledStrike() {
    m_attackCooldown = 1.0 / std::max(static_cast<double>(m_stats.attacksPerSec), 0.01);
    Unit* target = StrikeTarget();
    if (target != nullptr && m_stats.behavior == kRanged) {
        m_attackTarget = target;   // an arrow in flight needs something to fly at
        FireProjectile();
        return;
    }

    // BEFORE the damage and whether or not anything is hit, as the original
    // plays it: the swing is what the player asked for, and it must be heard.
    if (m_bus) m_bus->unitAttacked.Emit(m_position);
    if (target != nullptr) target->TakeDamage(EffectiveDamage());
}

// The nearest enemy within melee reach on the LANE: probed 80 px wider than
// the reach, then held to the reach plus the target's half-width, as AI melee
// is. Symmetric - facing is where the unit looks, not what it can hit.
Unit* Unit::StrikeTarget() const {
    Unit* probe =
        m_world->NearestEnemyUnit(m_stats.faction, m_position.x, m_stats.attackRange + 80.0f);
    if (probe == nullptr) return nullptr;
    const double dx = static_cast<double>(probe->Position().x) - static_cast<double>(m_position.x);
    const double reach =
        static_cast<double>(m_stats.attackRange) + static_cast<double>(probe->HitHalfWidth());
    return std::fabs(dx) > reach ? nullptr : probe;
}

// --- Abilities -------------------------------------------------------------

void Unit::UseAbility(int index) {
    // IsPlaying: Q and E leak through the game-over overlay, and the board is
    // frozen.
    if (m_phase != State::Controlled || index < 0 ||
        index >= static_cast<int>(m_stats.abilities.size()) || !m_state->IsPlaying()) {
        return;
    }
    const std::string& id = m_stats.abilities[static_cast<size_t>(index)];
    if (AbilityCooldownLeft(id) > 0.0) return;
    const Supersonic::Json::Value& def = m_state->Data().Ability(id);
    if (!def.IsObject() || def.AsObject().empty()) return;

    m_abilityCds[id] = def["cooldown"].AsNumber(5.0);
    const std::string kind = def["kind"].AsString("");
    if (kind == "aoe_damage") {
        CastAoeDamage(def);
    } else if (kind == "dash") {
        CastDash(def);
    }
    if (m_bus) m_bus->abilityUsed.Emit(id, this);
}

double Unit::AbilityCooldownLeft(const std::string& abilityId) const {
    const auto it = m_abilityCds.find(abilityId);
    return it == m_abilityCds.end() ? 0.0 : it->second;
}

// Cleave: one heavy swing that hits EVERY enemy within the radius along the
// lane, on both sides - |dx|, the combat invariant. Scaled off the effective
// damage, so a held banner strengthens it like any other blow.
void Unit::CastAoeDamage(const Supersonic::Json::Value& def) {
    const int damage = std::max(
        1, static_cast<int>(std::round(static_cast<double>(EffectiveDamage()) *
                                       def["damage_mult"].AsNumber(1.5))));
    const float radius = static_cast<float>(def["radius"].AsNumber(100.0));
    for (Unit* enemy : m_world->EnemiesWithin(m_stats.faction, m_position.x, radius)) {
        enemy->TakeDamage(damage);
    }
}

// Dash: a burst of distance along the steering, or along the facing when
// standing, with brief i-frames - the dodge. The travel is StepControlled's.
void Unit::CastDash(const Supersonic::Json::Value& def) {
    m_dashDir = m_controlDir != glm::vec2(0.0f) ? godotNormalized(m_controlDir)
                                                : glm::vec2(static_cast<float>(m_facing), 0.0f);
    m_dashLeft = def["distance"].AsNumber(240.0);
    m_dashSpeed = std::max(def["speed"].AsNumber(900.0), 1.0);
    m_invuln = def["invuln_s"].AsNumber(0.2);
}

void Unit::StepToward(const glm::vec2& target, double delta) {
    Face(static_cast<double>(target.x) - static_cast<double>(m_position.x));
    m_position = moveToward(m_position, target, static_cast<double>(m_stats.moveSpeed) * delta);
}

// Walk toward a thing's ACTUAL position; true once within `range` of it in 2D.
// From `_approach`.
//
// Worker economy and melee are the deliberate exception to the lane's x-only
// rule. A worker walks up to the real tree and the real Town Hall door, not to
// a spot on its own row beside them. A raider converges on the building rather
// than stopping on its own row at the building's x. Every SCAN stays x-only -
// which thing is nearest is still measured along the lane - and so do ranged
// attacks and a raider's march.
bool Unit::ApproachTo(const glm::vec2& target, float range, double delta) {
    if (glm::distance(m_position, target) <= range) return true;
    StepToward(target, delta);
    return false;
}

// The walkable band, [ground_y, ground_y + lane.depth], from the world data,
// so a click on the sky or the dirt still lands a unit on real ground.
float Unit::ClampToBand(double y) const {
    const Supersonic::Json::Value& world = m_state->Data().World();
    const double top = world["ground_y"].AsNumber(800.0);
    const double depth = world["lane"]["depth"].AsNumber(0.0);
    return static_cast<float>(std::clamp(y, top, top + depth));
}

void Unit::StepGather(double delta) {
    if (!HasLiveNode()) return;   // the tick transitions out of here

    // Toward the tree even when already in reach, where ApproachTo does not
    // step and so does not turn.
    Face(static_cast<double>(m_targetNode->position.x) - static_cast<double>(m_position.x));

    if (!ApproachTo(m_targetNode->position, m_stats.gatherRange, delta)) return;

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

    if (!ApproachTo(m_world->DepositPosition(m_depositIndex), m_stats.depositRange, delta)) {
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
    // To the real deposit, in 2D, the way a delivery walks there.
    StepToward(m_world->DepositPosition(m_fleeIndex), delta);
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
    Face(static_cast<double>(targetX) - static_cast<double>(m_position.x));

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

    // Melee closes in 2D, on the target's actual position, so attackers
    // converge on the thing rather than lining up top to bottom at its x
    // (the game's e2b5208). The ranged branch above stays on its own row.
    const float reach = m_stats.attackRange + m_attackTarget->HitHalfWidth();
    if (!ApproachTo(m_attackTarget->Position(), reach, delta)) return;
    if (m_attackCooldown <= 0.0) {
        m_attackCooldown = 1.0 / std::max(static_cast<double>(m_stats.attacksPerSec), 0.01);
        m_attackTarget->TakeDamage(EffectiveDamage());

        // AFTER the damage, as `unit.gd:384-386` does: the blow is what makes
        // the sound, so announcing it before would be a claim about something
        // that has not happened.
        if (m_bus) m_bus->unitAttacked.Emit(m_position);
    }
}

void Unit::FireProjectile() {
    ProjectilePool* pool = m_world->Projectiles();
    if (pool == nullptr) return;

    // From the middle of the body rather than its feet, so an arrow leaves the
    // archer's chest.
    const glm::vec2 from = m_position + glm::vec2(0.0f, -m_stats.bodySize.y * 0.5f);
    pool->Spawn(from, m_attackTarget, EffectiveDamage(), m_stats.projectileSpeed);

    if (m_bus) m_bus->projectileFired.Emit(from);
}

int Unit::EffectiveDamage() const {
    if (m_stats.faction != Factions::kPlayer) return m_stats.damage;
    return std::max(1, static_cast<int>(std::round(static_cast<double>(m_stats.damage) *
                                                   m_state->ArmyDamageMult())));
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
    Face(static_cast<double>(m_buildTarget->Position().x) - static_cast<double>(m_position.x));

    const float reach = m_buildTarget->Stats().bodySize.x * 0.5f + m_stats.gatherRange;
    if (!ApproachTo(m_buildTarget->Position(), reach, delta)) return;

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
        glm::distance(m_position, m_world->DepositPosition(m_fleeIndex)) <= m_stats.depositRange) {
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

    // A worker sent to open ground holds there rather than wandering back to
    // the nearest tree, which read as the unit ignoring the order.
    m_parked = true;

    // A player's order keeps its no-auto-acquire contract, even when it
    // interrupts a march home that was combat-aware.
    m_aiMarch = false;

    // Both axes, y clamped onto the walkable band (the game's 2d38d9e). Only
    // the ORDER is 2D: which enemy is nearest stays measured along the lane.
    m_moveTarget = glm::vec2(target.x, ClampToBand(target.y));
    SetState(State::Moving);
}

void Unit::CommandBuild(Building* site) {
    if (m_phase == State::Dead || site == nullptr) return;

    // Re-task: drop any attack order, so a worker sent from a fight to a
    // building has its flee reflex back.
    ClearAttack();
    m_parked = false;   // an explicit build order takes the worker off park

    m_buildTarget = site;
    SetState(State::Building);
}

void Unit::CommandGather(ResourceNode* node) {
    if (m_phase == State::Dead || node == nullptr || node->IsEmpty()) return;

    // How a parked worker is put back to work: a right-click or a tap on a
    // tree. Drops any attack order, as every re-task does.
    ClearAttack();
    m_parked = false;
    m_targetNode = node;
    SetState(State::Gathering);
}

void Unit::CommandAttack(Damageable* target) {
    if (m_phase == State::Dead || target == nullptr || m_stats.behavior == kHealer) return;
    m_attackTarget = target;
    m_orderedToAttack = true;
    m_parked = false;   // an attack order takes a worker off park
    SetState(State::Attacking);
}

// Passive regeneration, from `_process_regen`: once out of combat for
// economy.hp_regen_delay_s, hit points trickle back at hp_regen a second.
// Sim time, accumulated from the step, so a paused game regenerates nothing.
void Unit::StepRegen(double delta) {
    m_sinceDamage += delta;
    if (m_stats.hpRegen <= 0.0 || m_hp >= m_stats.maxHp) return;
    if (m_sinceDamage < m_state->Data().Economy()["hp_regen_delay_s"].AsNumber(4.0)) return;

    m_regenAccumulator += m_stats.hpRegen * delta;
    if (m_regenAccumulator >= 1.0) {
        const int whole = static_cast<int>(m_regenAccumulator);
        m_regenAccumulator -= static_cast<double>(whole);
        m_hp = std::min(m_hp + whole, m_stats.maxHp);
    }
}

void Unit::TakeDamage(int amount) {
    // Dash i-frames: a dodged hit neither hurts nor pauses regen.
    if (m_invuln > 0.0) return;

    // Before the dead check, as the original has it: any hit pauses regen.
    m_sinceDamage = 0.0;
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
    // The early-out is what makes the announcement fire EXACTLY once, and the
    // enemy tally depends on that: a unit killed twice would take the count of
    // living enemies below zero, and a director that is waiting for it to reach
    // zero would never declare victory.
    if (m_phase == State::Dead) return;
    SetState(State::Dead);
    m_bus->unitDied.Emit(this);
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
    out["parked"] = Supersonic::Json::Value(m_parked);
    out["squad"] = Supersonic::Json::Value(m_squad);

    // [] for no post, as the original writes Vector2.INF.
    Supersonic::Json::Array home;
    if (HasPost(m_homePost)) {
        home.push_back(Supersonic::Json::Value(static_cast<double>(m_homePost.x)));
        home.push_back(Supersonic::Json::Value(static_cast<double>(m_homePost.y)));
    }
    out["home"] = Supersonic::Json::Value(std::move(home));

    // Cooldowns ride the save, so a reload cannot be used to skip one.
    Supersonic::Json::Object cds;
    for (const auto& [id, left] : m_abilityCds) cds[id] = Supersonic::Json::Value(left);
    out["ability_cds"] = Supersonic::Json::Value(std::move(cds));

    // The upcast is deliberate and load-bearing - see SaveIds.hpp. A build
    // target is a Building and is upcast the same way its own entry was keyed.
    out["ref_tree"] = Supersonic::Json::Value(static_cast<double>(ids.Of(m_targetNode)));
    out["ref_build"] = Supersonic::Json::Value(
        static_cast<double>(ids.Of(static_cast<const Damageable*>(m_buildTarget))));
    out["ref_attack"] = Supersonic::Json::Value(static_cast<double>(ids.Of(m_attackTarget)));
    out["ref_heal"] = Supersonic::Json::Value(
        static_cast<double>(ids.Of(static_cast<const Damageable*>(m_healTarget))));
    return Supersonic::Json::Value(std::move(out));
}

void Unit::FromSave(const Supersonic::Json::Value& saved) {
    m_hp = static_cast<int>(saved["hp"].AsNumber(static_cast<double>(m_stats.maxHp)));

    // Assigned directly rather than through SetState, which would announce a
    // transition that did not happen - and NOT through a command, which would
    // clear the very targets Relink is about to restore.
    //
    // Up to Healing, the last state there is. CONTROLLED comes back as IDLE:
    // steering is input rather than state, and a load never resumes it. Two
    // edits, and both are needed - widening the range alone would restore a
    // unit that thinks nothing, waiting for a player who is not holding it.
    const int state = static_cast<int>(saved["state"].AsNumber(0.0));
    m_phase = (state >= 0 && state <= static_cast<int>(State::Healing)) ? static_cast<State>(state)
                                                                        : State::Idle;
    if (m_phase == State::Controlled) m_phase = State::Idle;

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

    // Absent from a save written before parking existed, and false then: that
    // worker goes back to work, as it would have.
    m_parked = saved["parked"].AsBool(false);

    // A save from before squads is a garrison with no post, which holds.
    m_squad = saved["squad"].AsString(Squads::kGarrison);
    const auto& home = saved["home"].AsArray();
    m_homePost = home.size() >= 2 ? glm::vec2(home[0].AsFloat(), home[1].AsFloat()) : NoPost();

    m_abilityCds.clear();
    for (const auto& entry : saved["ability_cds"].AsObject()) {
        m_abilityCds[entry.first] = entry.second.AsNumber(0.0);
    }
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

    // Absent from a save older than priests, which is -1 and re-seeks.
    m_healTarget = resolve("ref_heal", [&resolver](int sid) { return resolver.AsUnit(sid); });
}

void Unit::SetState(State next) {
    if (m_phase == next) return;
    m_phase = next;
}

} // namespace WolfBrigade
