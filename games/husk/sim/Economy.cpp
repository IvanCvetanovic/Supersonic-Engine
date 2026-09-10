// The essence economy (src/sim/economy.rs): Will, research, and the Siphon
// channels - extraction, construction and repair.

#include "World.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace husk {

namespace {

constexpr float kExtractRate = 7.0f;    // essence per second per worker [TUNE]
constexpr float kChannelRange = 1.0f;   // edge distance for channelling [TUNE]
constexpr float kRetargetRadius = 16.0f; // [TUNE]
constexpr float kRepairRate = 25.0f;    // hp per second [TUNE]
constexpr float kRepairCost = 3.0f;     // essence per second [TUNE]

float edgeDist(Vec2 a, float ra, Vec2 b, float rb) {
    return a.distance(b) - ra - rb;
}

enum class RepairOutcome { Working, OutOfRange, Done, Invalid };

RepairOutcome repairTick(Vec2 pos, float radius, Vec2 tPos, float tRadius, uint8_t tTeam, uint8_t team, Health& hp,
                         PlayerEconomy& economy) {
    if (tTeam != team) return RepairOutcome::Invalid;
    if (hp.cur >= hp.max) return RepairOutcome::Done;
    if (edgeDist(pos, radius, tPos, tRadius) > kChannelRange) return RepairOutcome::OutOfRange;
    const float cost = kRepairCost * kSimDt;
    if (economy.essence < cost) return RepairOutcome::Working; // broke: stand by
    economy.essence -= cost;
    hp.cur = fminr(hp.cur + kRepairRate * kSimDt, hp.max);
    return hp.cur >= hp.max ? RepairOutcome::Done : RepairOutcome::Working;
}

// Swap the front Extract to the nearest live source within reach, or pop it.
// Candidates in ascending SimId order; strict < keeps the lowest id on ties.
void retargetOrPop(World& w, Orders& orders, Vec2 from) {
    std::optional<std::tuple<float, uint32_t, Vec2>> best;
    for (const auto& [id, e] : w.entities) {
        if (!e.isSource() || e.source.husked()) continue;
        const float d = edgeDist(from, 0.0f, e.pos.cur, e.mover.radius);
        if (d <= kRetargetRadius && (!best || d < std::get<0>(*best))) best = {d, id, e.pos.cur};
    }
    if (best) {
        const uint32_t goal = w.grid.nearestOpen(w.grid.cellAt(std::get<2>(*best)));
        if (!orders.empty()) orders.front() = Order::extract(std::get<1>(*best), goal);
    } else if (!orders.empty()) {
        orders.pop_front();
    }
}

} // namespace

void updateWill(World& w) {
    const Catalogs& c = w.cat();
    uint32_t used = 0;
    uint32_t cap = 0;
    for (const auto& [id, e] : w.entities) {
        if (e.isUnit() && e.team == 0) used += c.units.def(e.unitKind).will;
    }
    for (const auto& [id, e] : w.entities) {
        if (!e.isBuilding() || e.team != 0) continue;
        if (e.building.complete()) cap += c.buildings.def(e.building.kind).willProvided;
        // queued production reserves Will up front
        for (uint16_t k : e.building.queue) used += c.units.def(k).will;
    }
    w.will.used = used;
    w.will.cap = std::min(cap, kWillCap);
}

void advanceResearch(World& w) {
    if (!w.research.inProgress || w.tick < w.research.inProgress->readyTick) return;
    const uint16_t upgrade = w.research.inProgress->upgrade;
    w.research.bump(upgrade);
    w.research.inProgress.reset();
    MissionFx fx;
    fx.kind = MissionFx::Kind::Message;
    fx.text = w.cat().upgrades.def(upgrade).name + " researched (Lv " +
              std::to_string(static_cast<int>(w.research.level(upgrade))) + ")";
    w.events.mission.push_back(std::move(fx));
}

void siphonChannels(World& w) {
    const Catalogs& c = w.cat();
    // gather-rate research applies to every extraction this tick
    const float gather = w.research.gatherMult(c.upgrades);

    std::vector<uint32_t> ordered;
    for (const auto& [id, e] : w.entities) {
        if (e.indexed) ordered.push_back(id);
    }

    for (uint32_t workerId : ordered) {
        Entity* worker = w.get(workerId);
        if (!worker || !worker->isUnit()) continue;
        if (worker->orders.empty()) continue;
        const Vec2 pos = worker->pos.cur;
        const float radius = worker->mover.radius;
        const uint8_t team = worker->team;
        const Order front = worker->orders.front();

        switch (front.kind) {
        case OrderKind::Extract: {
            Entity* src = w.indexed(front.id);
            if (!src || !src->isSource()) {
                worker->orders.pop_front();
                break;
            }
            if (src->source.husked()) {
                retargetOrPop(w, worker->orders, pos);
                break;
            }
            if (edgeDist(pos, radius, src->pos.cur, src->mover.radius) > kChannelRange) break; // walking in
            const float take = fminr(kExtractRate * gather * kSimDt, src->source.essence);
            src->source.essence -= take;
            w.economy.essence += take;
            w.affinity.cumulative[static_cast<size_t>(src->source.affinity)] += take;
            const float animaTake = fminr(take * src->source.animaPerEssence, src->source.anima);
            src->source.anima -= animaTake;
            w.economy.anima += animaTake;
            if (src->source.husked()) retargetOrPop(w, worker->orders, pos);
            break;
        }
        case OrderKind::Build: {
            Entity* site = w.indexed(front.id);
            if (!site || !site->isBuilding()) {
                worker->orders.pop_front();
                break;
            }
            if (site->building.complete()) {
                worker->orders.pop_front();
                break;
            }
            if (edgeDist(pos, radius, site->pos.cur, site->mover.radius) > kChannelRange) break;
            const float stepFrac = kSimDt / c.buildings.def(site->building.kind).buildTime;
            site->building.progress = fminr(site->building.progress + stepFrac, 1.0f);
            site->health.cur = fminr(site->health.cur + site->health.max * 0.9f * stepFrac, site->health.max);
            if (site->building.complete()) worker->orders.pop_front();
            break;
        }
        case OrderKind::Repair: {
            Entity* t = w.indexed(front.id);
            if (!t) {
                worker->orders.pop_front();
                break;
            }
            RepairOutcome outcome = RepairOutcome::Invalid;
            if (t->hasTeam()) {
                outcome = repairTick(pos, radius, t->pos.cur, t->mover.radius, t->team, team, t->health, w.economy);
            }
            if (outcome == RepairOutcome::Done || outcome == RepairOutcome::Invalid) worker->orders.pop_front();
            break;
        }
        default: break;
        }
    }
}

} // namespace husk
