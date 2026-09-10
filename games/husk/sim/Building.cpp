// Buildings (src/sim/building.rs): placements drain_orders validated, and the
// production queues.

#include "World.hpp"

#include <optional>

namespace husk {

void applyPlacements(World& w) {
    std::vector<PendingPlacement> pending;
    pending.swap(w.placements);
    for (const PendingPlacement& p : pending) {
        const uint32_t site = spawnBuilding(w, p.kind, 0, p.center, false);
        const Vec2 sitePos = w.get(site)->pos.cur;
        const uint32_t goal = w.grid.nearestOpen(w.grid.cellAt(sitePos));
        Entity* builder = w.indexed(p.builder);
        if (!builder || !builder->isUnit()) continue;
        builder->orders.clear();
        builder->orders.push_back(Order::build(site, goal));
    }
}

void tickProduction(World& w) {
    const Catalogs& c = w.cat();
    struct Job {
        uint16_t buildingKind;
        uint16_t unitKind;
        Vec2 center;
        std::optional<Vec2> rally;
        float unitRadius;
    };
    std::vector<Job> jobs;
    for (auto& [id, e] : w.entities) {
        if (!e.isBuilding()) continue;
        Building& b = e.building;
        if (e.team != 0 || !b.complete()) continue;
        if (b.queue.empty()) continue;
        const uint16_t front = b.queue.front();
        b.queueProgress += kSimDt;
        if (b.queueProgress < c.units.def(front).buildTime) continue;
        b.queueProgress = 0.0f;
        b.queue.erase(b.queue.begin());
        jobs.push_back({b.kind, front, e.pos.cur, b.rally, c.units.def(front).radius});
    }

    for (const Job& job : jobs) {
        const float footprintH = static_cast<float>(c.buildings.def(job.buildingKind).footprint[1]);
        // exit at the south edge, snapped to an open cell
        Vec2 exit = job.center + Vec2(0.0f, footprintH * 0.5f + job.unitRadius + 0.4f);
        exit = w.grid.cellCenter(w.grid.nearestOpen(w.grid.cellAt(exit)));
        const uint32_t unit = spawnUnit(w, job.unitKind, 0, exit);
        if (job.rally) {
            const uint32_t goal = w.grid.nearestOpen(w.grid.cellAt(*job.rally));
            w.get(unit)->orders.push_back(Order::point(false, goal, w.grid.cellCenter(goal)));
        }
    }
}

} // namespace husk
