// Target acquisition and attack resolution (src/sim/combat.rs).
//
// Units are preferred over structures when acquiring; ties go to the lower
// SimId. Damage is instant-hit and applied in ascending SimId order, which is
// part of the deterministic contract: two attackers killing the same target
// must resolve the same way every run.

#include "World.hpp"

#include <cmath>
#include <optional>

namespace husk {

namespace {

constexpr float kLeash = 30.0f;          // [TUNE]
constexpr float kHighGroundMult = 0.7f;  // damage kept firing uphill
constexpr float kUnitRegen = 0.4f;       // [TUNE]

float highGroundMult(const NavGrid& grid, Vec2 from, Vec2 to) {
    return grid.levelAt(from) < grid.levelAt(to) ? kHighGroundMult : 1.0f;
}

float edgeDist(Vec2 a, float ra, Vec2 b, float rb) {
    return a.distance(b) - ra - rb;
}

struct Best {
    float d;
    uint32_t id;
};

// One lethal hit's bookkeeping, shared by tick_attacks and tower_attacks.
struct Lethal {
    uint32_t id = 0;
    Vec2 pos;
    float radius = 0.0f;
    uint8_t team = 0;
    float xp = 0.0f;
    std::optional<Hero> deadHero;
    bool isStructure = false;
    uint16_t victimKind = UINT16_MAX;
    std::optional<std::vector<uint32_t>> cells; // buildings only
};

void applyLethal(World& w, Lethal k) {
    w.events.deaths.push_back({k.pos, k.radius, k.team});
    // structures never drop, whatever kind they carry
    w.events.kills.push_back({k.pos, k.team, k.xp, static_cast<uint16_t>(k.isStructure ? UINT16_MAX : k.victimKind)});
    if (k.deadHero) w.heroState = HeroState::dead(*k.deadHero, w.tick);
    w.despawnDeferred(k.id);
    w.unindex(k.id);
    if (k.isStructure) {
        w.statics.erase(k.id);
        // rubble doesn't block: free the cells, drop stale fields
        if (k.cells) w.grid.setBlocked(*k.cells, false);
        w.flowFields.clear();
    }
}

} // namespace

void acquireTargets(World& w) {
    const Catalogs& c = w.cat();
    // Selection reads only other entities' immutable state and the leash
    // logic only the unit's own, so the per-unit result is order-independent.
    for (auto& [id, e] : w.entities) {
        if (!e.isUnit()) continue;
        const std::optional<AttackDef>& maybeAttack = c.units.def(e.unitKind).attack;
        if (!maybeAttack) {
            e.combat.target.reset(); // noncombatant
            continue;
        }
        const AttackDef attack = *maybeAttack;
        const Order* front = e.orders.empty() ? nullptr : &e.orders.front();
        const bool hold = front && front->kind == OrderKind::Hold;

        // Leash anchor: an idle unit guards where it stands; an attack-moving
        // or patrolling unit is tethered to its order's goal.
        std::optional<Vec2> anchor;
        bool isIdle = false;
        if (!front) {
            anchor = e.combat.home;
            isIdle = true;
        } else if ((front->kind == OrderKind::Point && front->attack) || front->kind == OrderKind::Patrol) {
            anchor = front->target;
        }
        // A resting unit's post follows it; once it gives chase the post is
        // frozen, so the leash measures how far it has strayed.
        if (isIdle && !e.combat.target) e.combat.home = e.pos.cur;

        // explicit modes first
        if (front) {
            if (front->kind == OrderKind::Point && !front->attack) {
                e.combat.target.reset();
                continue;
            }
            if (front->kind == OrderKind::Extract || front->kind == OrderKind::Repair ||
                front->kind == OrderKind::Build) {
                e.combat.target.reset(); // workers on channel duty don't fight
                continue;
            }
            if (front->kind == OrderKind::Attack) {
                const uint32_t target = front->id;
                const Entity* t = w.indexed(target);
                const bool alive = t && t->hasTeam() && t->team != e.team && t->health.cur > 0.0f;
                if (alive) {
                    e.combat.target = target;
                } else {
                    e.orders.pop_front();
                    e.combat.target.reset();
                }
                continue;
            }
        }

        const bool beyondLeash = anchor && e.pos.cur.distance(*anchor) > kLeash;

        // validate the current target: dead, out of hold range or past the
        // leash drops it
        if (e.combat.target) {
            const Entity* t = w.indexed(*e.combat.target);
            const bool keep = !beyondLeash && t && t->hasTeam() && t->team != e.team && t->health.cur > 0.0f &&
                              (!hold || edgeDist(e.pos.cur, e.mover.radius, t->pos.cur, t->mover.radius) <= attack.range);
            if (!keep) e.combat.target.reset();
        }
        if (e.combat.target) continue;

        // Past the leash: don't re-acquire. An idle unit walks back to its
        // post; the hero is never force-walked.
        if (beyondLeash) {
            if (isIdle && !e.hero) {
                const uint32_t goal = w.grid.nearestOpen(w.grid.cellAt(e.combat.home));
                e.orders.push_back(Order::point(false, goal, w.grid.cellCenter(goal)));
            }
            continue;
        }

        // nearest enemy by edge distance, units before structures, ties to
        // the lower SimId
        const float reach = hold ? attack.range : attack.acquire;
        std::optional<Best> bestUnit;
        std::optional<Best> bestStructure;
        std::optional<Best> bestTaunt;
        for (const auto& [otherId, o] : w.entities) {
            if (!o.indexed || otherId == id) continue;
            if (!o.hasTeam()) continue;
            if (o.team == e.team || o.health.cur <= 0.0f) continue;
            const float d = edgeDist(e.pos.cur, e.mover.radius, o.pos.cur, o.mover.radius);
            if (d > reach) continue;
            // no acquiring a mobile enemy up on higher ground; structures are
            // exempt
            const bool isStructure = o.isBuilding();
            if (!isStructure && !w.grid.losClear(e.pos.cur, o.pos.cur)) continue;
            // a taunting enemy within its threat radius pulls aggro first
            if (o.isUnit()) {
                const auto& passive = c.units.def(o.unitKind).passive;
                if (passive && passive->kind == UnitPassive::Kind::Taunt && d <= passive->radius &&
                    (!bestTaunt || d < bestTaunt->d)) {
                    bestTaunt = Best{d, otherId};
                }
            }
            std::optional<Best>& slot = isStructure ? bestStructure : bestUnit;
            if (!slot || d < slot->d) slot = Best{d, otherId};
        }
        if (bestTaunt) {
            e.combat.target = bestTaunt->id;
        } else if (bestUnit) {
            e.combat.target = bestUnit->id;
        } else if (bestStructure) {
            e.combat.target = bestStructure->id;
        } else {
            e.combat.target.reset();
        }
    }
}

void tickAttacks(World& w) {
    const Catalogs& c = w.cat();

    // cool everyone down first
    for (auto& [id, e] : w.entities) {
        if (e.isUnit()) e.combat.cooldown = fmaxr(e.combat.cooldown - kSimDt, 0.0f);
    }

    // fire in ascending SimId order
    std::vector<uint32_t> firingOrder;
    for (const auto& [id, e] : w.entities) {
        if (e.indexed) firingOrder.push_back(id);
    }
    for (uint32_t attackerId : firingOrder) {
        Entity* a = w.get(attackerId);
        if (!a || !a->isUnit()) continue;
        if (a->combat.cooldown > 0.0f) continue;
        if (!a->combat.target) continue;
        const uint32_t tid = *a->combat.target;
        // dead units don't fire, even before their despawn applies
        if (a->health.cur <= 0.0f) continue;
        const std::optional<AttackDef>& maybeAttack = c.units.def(a->unitKind).attack;
        if (!maybeAttack) continue;
        const AttackDef attack = *maybeAttack;
        const float damageAdd = a->mods.damageAdd;
        const float cooldownReset = attack.cooldown / fmaxr(a->mods.atkSpeedMult, 0.01f);
        const Vec2 pos = a->pos.cur;
        const uint8_t team = a->team;
        const uint16_t myKind = a->unitKind;
        const float myRadius = a->mover.radius;
        // difficulty scales enemy weapon damage, the chain's bounces included
        const float difficultyMult = team != 0 ? c.difficulty.mults(w.difficulty).enemyDamage : 1.0f;

        Entity* t = w.indexed(tid);
        if (!t || !t->hasTeam()) continue;
        if (t->team == team || t->health.cur <= 0.0f) continue;
        const Vec2 tPos = t->pos.cur;
        if (edgeDist(pos, myRadius, tPos, t->mover.radius) > attack.range) continue;
        const float mult = c.matrix.multiplier(attack.kind, t->armor);
        const float ground = highGroundMult(w.grid, pos, tPos); // uphill is reduced
        t->health.cur -= (attack.damage + damageAdd) * difficultyMult * mult * ground;
        const bool died = t->health.cur <= 0.0f;
        const float xp = t->isUnit() ? c.units.def(t->unitKind).xpValue : 0.0f;

        w.events.attacks.push_back({pos, tPos, attack.ranged, myKind, attackerId, tid});
        if (died) {
            Lethal k;
            k.id = tid;
            k.pos = tPos;
            k.radius = t->mover.radius;
            k.team = t->team;
            k.xp = xp;
            k.deadHero = t->hero;
            k.isStructure = t->isBuilding();
            k.victimKind = t->isUnit() ? t->unitKind : UINT16_MAX;
            if (t->isBuilding()) k.cells = t->building.cells;
            applyLethal(w, std::move(k));
        }

        a->combat.cooldown = cooldownReset;

        // The Arc Wisp's chain: the bolt bounces to the nearest un-hit enemy
        // UNIT within reach of the last link, each hop a falloff'd copy.
        const auto& passive = c.units.def(myKind).passive;
        if (passive && passive->kind == UnitPassive::Kind::ChainArc) {
            const float r2 = passive->radius * passive->radius;
            float arcBase = attack.damage + damageAdd;
            Vec2 chainFrom = tPos;
            std::vector<uint32_t> hit{tid};
            for (uint8_t jump = 0; jump < passive->jumps; ++jump) {
                arcBase *= passive->falloff;
                std::optional<std::pair<float, uint32_t>> best;
                for (const auto& [oid, o] : w.entities) {
                    if (!o.indexed) continue;
                    if (std::find(hit.begin(), hit.end(), oid) != hit.end()) continue;
                    if (!o.hasTeam()) continue;
                    if (o.isBuilding() || o.team == team || o.health.cur <= 0.0f) continue;
                    const float d2 = o.pos.cur.distanceSquared(chainFrom);
                    if (d2 <= r2 && (!best || d2 < best->first)) best = {d2, oid};
                }
                if (!best) break; // the chain fizzles
                const uint32_t nextId = best->second;
                hit.push_back(nextId);
                Entity* n = w.get(nextId);
                const Vec2 nextPos = n->pos.cur;
                const float nMult = c.matrix.multiplier(attack.kind, n->armor);
                const float nGround = highGroundMult(w.grid, pos, nextPos);
                n->health.cur -= arcBase * difficultyMult * nMult * nGround;
                const bool nDied = n->health.cur <= 0.0f;
                w.events.attacks.push_back({chainFrom, nextPos, attack.ranged, myKind, attackerId, nextId});
                if (nDied) {
                    Lethal k;
                    k.id = nextId;
                    k.pos = nextPos;
                    k.radius = n->mover.radius;
                    k.team = n->team;
                    k.xp = n->isUnit() ? c.units.def(n->unitKind).xpValue : 0.0f;
                    k.deadHero = n->hero;
                    k.isStructure = false;
                    k.victimKind = n->isUnit() ? n->unitKind : UINT16_MAX;
                    applyLethal(w, std::move(k));
                }
                chainFrom = nextPos;
            }
        }
    }
}

void towerAttacks(World& w) {
    const Catalogs& c = w.cat();

    // Phase A: cool every defensive building down; collect the ready ones.
    struct Ready {
        uint32_t id;
        Vec2 pos;
        uint8_t team;
        float radius;
        AttackDef attack;
    };
    std::vector<Ready> ready;
    for (auto& [id, e] : w.entities) {
        if (!e.isBuilding()) continue;
        const std::optional<AttackDef>& attack = c.buildings.def(e.building.kind).attack;
        if (!attack) continue; // passive building
        if (!e.building.complete()) continue;
        e.building.attackCooldown = fmaxr(e.building.attackCooldown - kSimDt, 0.0f);
        if (e.building.attackCooldown <= 0.0f) ready.push_back({id, e.pos.cur, e.team, e.mover.radius, *attack});
    }

    // Phase B: each ready tower re-acquires the nearest enemy and fires.
    for (const Ready& r : ready) {
        std::optional<Best> best;
        for (const auto& [oid, o] : w.entities) {
            if (!o.indexed || !o.hasTeam()) continue;
            if (o.team == r.team || o.health.cur <= 0.0f) continue;
            const float d = edgeDist(r.pos, r.radius, o.pos.cur, o.mover.radius);
            if (d <= r.attack.acquire && (o.isBuilding() || w.grid.losClear(r.pos, o.pos.cur)) &&
                (!best || d < best->d)) {
                best = Best{d, oid};
            }
        }
        if (!best) continue;                    // nothing in range; stays ready
        if (best->d > r.attack.range) continue; // acquired but out of reach

        Entity* o = w.get(best->id);
        const Vec2 oPos = o->pos.cur;
        const float mult = c.matrix.multiplier(r.attack.kind, o->armor);
        const float ground = highGroundMult(w.grid, r.pos, oPos);
        o->health.cur -= r.attack.damage * mult * ground;
        const bool died = o->health.cur <= 0.0f;

        w.events.attacks.push_back({r.pos, oPos, true, UINT16_MAX, UINT32_MAX, best->id});
        if (died) {
            Lethal k;
            k.id = best->id;
            k.pos = oPos;
            k.radius = o->mover.radius;
            k.team = o->team;
            k.xp = o->isUnit() ? c.units.def(o->unitKind).xpValue : 0.0f;
            k.deadHero = o->hero;
            k.isStructure = o->isBuilding();
            k.victimKind = o->isUnit() ? o->unitKind : UINT16_MAX;
            if (o->isBuilding()) k.cells = o->building.cells;
            applyLethal(w, std::move(k));
        }
        if (Entity* tower = w.get(r.id)) tower->building.attackCooldown = r.attack.cooldown;
    }
}

void passiveRegen(World& w) {
    const Catalogs& c = w.cat();
    // the living flora a woods-dweller can draw on
    std::vector<Vec2> floraSpots;
    for (const auto& [id, e] : w.entities) {
        if (e.isSource() && e.source.affinity == Affinity::Flora && !e.source.husked()) {
            floraSpots.push_back(e.pos.cur);
        }
    }
    for (auto& [id, e] : w.entities) {
        if (!e.isUnit() || e.hero) continue;
        if (e.health.cur <= 0.0f || e.health.cur >= e.health.max) continue;
        float rate = kUnitRegen;
        const auto& passive = c.units.def(e.unitKind).passive;
        if (passive && passive->kind == UnitPassive::Kind::FloraRegen) {
            const float r2 = passive->radius * passive->radius;
            for (const Vec2& f : floraSpots) {
                if (e.pos.cur.distanceSquared(f) <= r2) {
                    rate = passive->rate;
                    break;
                }
            }
        }
        e.health.cur = fminr(e.health.cur + rate * kSimDt, e.health.max);
    }
}

} // namespace husk
