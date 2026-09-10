#include "Snapshot.hpp"

#include <algorithm>
#include <stdexcept>

namespace husk {

SaveGame capture(const World& w, CampaignState campaign) {
    SaveGame save;
    save.version = kSaveVersion;
    save.campaign = std::move(campaign);
    SimSnapshot& s = save.sim;
    s.tick = w.tick;
    s.rng = w.rng.saveState();
    s.idAlloc = w.idAlloc;
    s.map = w.map;
    s.economy = w.economy;
    s.affinity = w.affinity;
    s.research = w.research;
    s.heroState = w.heroState;
    s.mission = w.mission;
    s.pendingOrders = w.orderQueue;

    // std::map iteration is SimId order, the order the game sorts into
    for (const auto& [id, e] : w.entities) {
        switch (e.kind) {
        case EntityKind::Unit: {
            UnitSave u;
            u.id = id;
            u.kind = e.unitKind;
            u.team = e.team;
            u.prev = e.pos.prev;
            u.cur = e.pos.cur;
            u.hpCur = e.health.cur;
            u.hpMax = e.health.max;
            u.cooldown = e.combat.cooldown;
            u.target = e.combat.target;
            u.home = e.combat.home;
            u.orders = e.orders;
            u.arrived = e.arrived;
            u.hero = e.hero;
            u.overcharge = e.overcharge;
            u.slow = e.slow;
            u.avatar = e.avatar;
            u.timed = e.timedLife;
            u.raider = e.raider;
            s.units.push_back(std::move(u));
            break;
        }
        case EntityKind::Building:
            s.buildings.push_back({id, e.team, e.pos.cur, e.health.cur, e.health.max, e.building});
            break;
        case EntityKind::Source: s.sources.push_back({id, e.pos.cur, e.source}); break;
        case EntityKind::Item: s.items.push_back({id, e.itemKind, e.pos.cur}); break;
        }
    }
    return save;
}

void clearSimWorld(World& w) {
    w.entities.clear();
    w.statics.clear();
    w.flowFields.clear();
    w.orderQueue.clear();
    w.placements.clear();
    w.pendingDrops.clear();
    w.events = SimEvents{};
}

void restore(World& w, const SaveGame& save) {
    if (save.version != kSaveVersion) throw std::runtime_error("unsupported save version");
    const SimSnapshot& snap = save.sim;
    clearSimWorld(w);

    // map and fresh nav; entity restore re-applies the blocking below
    NavGrid grid = NavGrid::fromDef(snap.map);
    w.map = snap.map;

    w.tick = snap.tick;
    w.rng = Pcg32::fromState(snap.rng.first, snap.rng.second);
    w.idAlloc = snap.idAlloc;
    w.economy = snap.economy;
    w.affinity = snap.affinity;
    w.research = snap.research;
    w.heroState = snap.heroState;
    w.mission = snap.mission;
    w.difficulty = save.campaign.difficulty;
    w.orderQueue = snap.pendingOrders;

    const Catalogs& c = w.cat();

    for (const UnitSave& u : snap.units) {
        const UnitDef& def = c.units.def(u.kind);
        Entity e;
        e.id = u.id;
        e.kind = EntityKind::Unit;
        e.pos = {u.prev, u.cur};
        e.mover = {def.speed, def.radius};
        e.team = u.team;
        e.unitKind = u.kind;
        e.armor = def.armor;
        e.health = {u.hpCur, u.hpMax};
        e.combat = {u.cooldown, u.target, u.home};
        e.orders = u.orders;
        e.arrived = u.arrived;
        e.hero = u.hero;
        e.overcharge = u.overcharge;
        e.slow = u.slow;
        e.avatar = u.avatar;
        e.timedLife = u.timed;
        e.raider = u.raider;
        w.entities[u.id] = std::move(e);
    }

    for (const BuildingSave& b : snap.buildings) {
        // radius and armor mirror spawn_building, from the def
        const BuildingDef& def = c.buildings.def(b.building.kind);
        const float radius = static_cast<float>(std::max(def.footprint[0], def.footprint[1])) * 0.5f;
        Entity e;
        e.id = b.id;
        e.kind = EntityKind::Building;
        e.pos = SimPos::at(b.pos);
        e.mover = {0.0f, radius};
        e.team = b.team;
        e.armor = def.armor;
        e.health = {b.hpCur, b.hpMax};
        e.building = b.building;
        w.entities[b.id] = std::move(e);
        w.statics[b.id] = {b.pos, radius};
        grid.setBlocked(b.building.cells, true);
    }

    for (const SourceSave& s : snap.sources) {
        const SourceDef& def = c.sources.def(s.source.kind);
        const uint32_t fw = def.footprint[0];
        const uint32_t fh = def.footprint[1];
        const float radius = static_cast<float>(std::max(fw, fh)) * 0.5f;
        Entity e;
        e.id = s.id;
        e.kind = EntityKind::Source;
        e.pos = SimPos::at(s.pos);
        e.mover = {0.0f, radius};
        e.source = s.source;
        w.entities[s.id] = std::move(e);
        w.statics[s.id] = {s.pos, radius};
        // positions were grid-snapped at spawn; re-snapping gives the same cells
        const auto [snapped, min] = grid.snapFootprint(s.pos, fw, fh);
        (void)snapped;
        grid.setBlocked(grid.footprintCells(min, fw, fh), true);
    }

    for (const ItemSave& i : snap.items) {
        Entity e;
        e.id = i.id;
        e.kind = EntityKind::Item;
        e.pos = SimPos::at(i.pos);
        e.mover = {0.0f, 0.35f};
        e.itemKind = i.kind;
        w.entities[i.id] = std::move(e);
        w.statics[i.id] = {i.pos, 0.35f};
    }

    w.grid = std::move(grid);
}

} // namespace husk
