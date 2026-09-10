// The determinism oracle's hash (src/sim/hash.rs): FNV-1a 64 over units,
// buildings, sources, items and the economy, in SimId order. Transient FX
// events are left out on purpose.

#include "Fnv.hpp"
#include "World.hpp"

#include <cstdio>
#include <sstream>

namespace husk {

namespace {

void writeOrder(Fnv1a& h, const Order& o) {
    switch (o.kind) {
    case OrderKind::Point:
        h.writeU32(1 + (o.attack ? 1u : 0u));
        h.writeU32(o.goalCell);
        break;
    case OrderKind::Attack:
        h.writeU32(3);
        h.writeU32(o.id);
        break;
    case OrderKind::Patrol:
        h.writeU32(4);
        h.writeU32(o.goalCell);
        break;
    case OrderKind::Hold: h.writeU32(5); break;
    case OrderKind::Extract:
        h.writeU32(6);
        h.writeU32(o.id);
        break;
    case OrderKind::Repair:
        h.writeU32(7);
        h.writeU32(o.id);
        break;
    case OrderKind::Build:
        h.writeU32(8);
        h.writeU32(o.id);
        break;
    case OrderKind::Cast:
        h.writeU32(9);
        h.writeU32(o.ability);
        switch (o.castTarget.kind) {
        case CastTarget::Kind::Id: h.writeU32(o.castTarget.id); break;
        case CastTarget::Kind::Point:
            h.writeF32(o.castTarget.point.x);
            h.writeF32(o.castTarget.point.y);
            break;
        case CastTarget::Kind::None: h.writeU32(UINT32_MAX - 1); break;
        }
        break;
    case OrderKind::Pickup:
        h.writeU32(10);
        h.writeU32(o.id);
        break;
    }
}

void writeState(Fnv1a& h, const World& w) {
    h.writeU64(w.tick);
    h.writeF32(w.economy.essence);
    h.writeF32(w.economy.anima);
    for (float c : w.affinity.cumulative) h.writeF32(c);
    h.writeU32(w.will.used);
    h.writeU32(w.will.cap);

    // research: per-mission mutable state that steers future ticks
    h.writeU32(static_cast<uint32_t>(w.research.levels.size()));
    for (uint8_t lvl : w.research.levels) h.writeU32(lvl);
    if (!w.research.inProgress) {
        h.writeU32(0);
    } else {
        h.writeU32(1);
        h.writeU32(w.research.inProgress->upgrade);
        h.writeU64(w.research.inProgress->readyTick);
    }

    // RNG stream position and the id allocator
    const auto [hi, lo] = w.rng.saveState();
    h.writeU64(hi);
    h.writeU64(lo);
    h.writeU32(w.idAlloc);

    h.writeU32(static_cast<uint32_t>(w.difficulty));

    if (!w.mission) {
        h.writeU32(0);
    } else {
        const MissionRuntime& rt = *w.mission;
        h.writeU32(1);
        h.writeU64(rt.startTick);
        h.writeU32(static_cast<uint32_t>(rt.outcome));
        h.writeU64(rt.fired.size());
        for (bool fired : rt.fired) h.writeU32(fired ? 1 : 0);
        h.writeU64(rt.objectives.size());
        for (const ObjectiveState& o : rt.objectives) {
            h.writeBytes(o.name);
            h.writeU32(static_cast<uint32_t>(o.status));
            h.writeU64(o.sinceTick);
        }
    }

    switch (w.heroState.kind) {
    case HeroState::Kind::Absent: h.writeU32(0); break;
    case HeroState::Kind::Alive:
        h.writeU32(1);
        h.writeU32(w.heroState.id);
        break;
    case HeroState::Kind::Dead:
        h.writeU32(2);
        h.writeU32(w.heroState.saved.level);
        h.writeF32(w.heroState.saved.xp);
        h.writeU64(w.heroState.tick);
        break;
    case HeroState::Kind::Reviving:
        h.writeU32(3);
        h.writeU32(w.heroState.saved.level);
        h.writeU64(w.heroState.tick);
        break;
    }
}

void writeUnits(Fnv1a& h, const World& w) {
    for (const auto& [id, e] : w.entities) {
        if (!e.isUnit()) continue;
        h.writeU32(id);
        h.writeF32(e.pos.cur.x);
        h.writeF32(e.pos.cur.y);
        h.writeU32(e.team);
        h.writeU32(e.unitKind);
        h.writeF32(e.health.cur);
        h.writeF32(e.combat.cooldown);
        h.writeU32(e.combat.target.value_or(UINT32_MAX));
        h.writeF32(e.combat.home.x);
        h.writeF32(e.combat.home.y);
        h.writeU64(e.orders.size());
        for (const Order& o : e.orders) writeOrder(h, o);
        h.writeU32(e.arrived.value_or(UINT32_MAX));
        if (e.hero) {
            h.writeU32(0xBEEF);
            h.writeU32(e.hero->level);
            h.writeF32(e.hero->xp);
            h.writeU32(e.hero->points);
            for (uint8_t rank : e.hero->ranks) h.writeU32(rank);
            for (float cd : e.hero->cooldowns) h.writeF32(cd);
            for (const auto& slot : e.hero->inventory) h.writeU32(slot ? *slot : UINT32_MAX);
        }
        if (e.overcharge) {
            h.writeU32(0xC1);
            h.writeU64(e.overcharge->untilTick);
            h.writeF32(e.overcharge->burn);
        }
        if (e.slow) {
            h.writeU32(0xC2);
            h.writeU64(e.slow->untilTick);
            h.writeF32(e.slow->slow);
        }
        if (e.avatar) {
            h.writeU32(0xC3);
            h.writeU64(e.avatar->untilTick);
        }
        if (e.timedLife) {
            h.writeU32(0xC4);
            h.writeU64(e.timedLife->untilTick);
        }
    }
}

void writeBuildings(Fnv1a& h, const World& w) {
    for (const auto& [id, e] : w.entities) {
        if (!e.isBuilding()) continue;
        const Building& b = e.building;
        h.writeU32(id);
        h.writeU32(e.team);
        h.writeU32(b.kind);
        h.writeF32(e.health.cur);
        h.writeF32(b.progress);
        h.writeF32(b.queueProgress);
        h.writeF32(b.attackCooldown);
        h.writeU64(b.queue.size());
        for (uint16_t k : b.queue) h.writeU32(k);
        if (b.rally) {
            h.writeF32(b.rally->x);
            h.writeF32(b.rally->y);
        } else {
            h.writeU32(UINT32_MAX);
        }
    }
}

void writeSources(Fnv1a& h, const World& w) {
    for (const auto& [id, e] : w.entities) {
        if (!e.isSource()) continue;
        h.writeU32(id);
        h.writeF32(e.source.essence);
        h.writeF32(e.source.anima);
        h.writeU32(e.source.animated ? 1 : 0);
    }
}

void writeItems(Fnv1a& h, const World& w) {
    for (const auto& [id, e] : w.entities) {
        if (!e.isItem()) continue;
        h.writeU32(id);
        h.writeU32(e.itemKind);
    }
}

std::string orderText(const Order& o) {
    std::ostringstream s;
    switch (o.kind) {
    case OrderKind::Point:
        s << "point:" << (o.attack ? 1 : 0) << ':' << o.goalCell << ':' << bitsHex(o.target.x) << ','
          << bitsHex(o.target.y);
        break;
    case OrderKind::Attack: s << "attack:" << o.id; break;
    case OrderKind::Patrol:
        s << "patrol:" << o.goalCell << ':' << bitsHex(o.target.x) << ',' << bitsHex(o.target.y) << ':'
          << bitsHex(o.home.x) << ',' << bitsHex(o.home.y);
        break;
    case OrderKind::Hold: s << "hold"; break;
    case OrderKind::Extract: s << "extract:" << o.id << ':' << o.goalCell; break;
    case OrderKind::Repair: s << "repair:" << o.id << ':' << o.goalCell; break;
    case OrderKind::Build: s << "build:" << o.id << ':' << o.goalCell; break;
    case OrderKind::Cast:
        s << "cast:" << static_cast<int>(o.ability) << ':';
        if (o.castTarget.kind == CastTarget::Kind::Id) {
            s << "id" << o.castTarget.id;
        } else if (o.castTarget.kind == CastTarget::Kind::Point) {
            s << "pt" << bitsHex(o.castTarget.point.x) << ',' << bitsHex(o.castTarget.point.y);
        } else {
            s << "none";
        }
        s << ':';
        if (o.hasGoal) {
            s << o.goalCell;
        } else {
            s << -1;
        }
        break;
    case OrderKind::Pickup: s << "pickup:" << o.id << ':' << o.goalCell; break;
    }
    return s.str();
}

} // namespace

uint64_t computeSimHash(const World& w) {
    Fnv1a h;
    writeState(h, w);
    writeUnits(h, w);
    writeBuildings(h, w);
    writeSources(h, w);
    writeItems(h, w);
    return h.finish();
}

void updateSimHash(World& w) {
    w.hash = computeSimHash(w);
}

std::array<uint64_t, 5> domainHashes(const World& w) {
    std::array<uint64_t, 5> out{};
    {
        Fnv1a h;
        writeState(h, w);
        out[0] = h.finish();
    }
    {
        Fnv1a h;
        writeUnits(h, w);
        out[1] = h.finish();
    }
    {
        Fnv1a h;
        writeBuildings(h, w);
        out[2] = h.finish();
    }
    {
        Fnv1a h;
        writeSources(h, w);
        out[3] = h.finish();
    }
    {
        Fnv1a h;
        writeItems(h, w);
        out[4] = h.finish();
    }
    return out;
}

std::string dumpEntities(const World& w) {
    std::ostringstream o;
    for (const auto& [id, e] : w.entities) {
        if (!e.isUnit()) continue;
        o << "  @" << w.tick << " unit " << id << " team=" << static_cast<int>(e.team) << " kind=" << e.unitKind
          << " pos=" << bitsHex(e.pos.cur.x) << ',' << bitsHex(e.pos.cur.y) << " prev=" << bitsHex(e.pos.prev.x)
          << ',' << bitsHex(e.pos.prev.y) << " hp=" << bitsHex(e.health.cur) << '/' << bitsHex(e.health.max)
          << " cd=" << bitsHex(e.combat.cooldown) << " target=";
        if (e.combat.target) {
            o << *e.combat.target;
        } else {
            o << -1;
        }
        o << " home=" << bitsHex(e.combat.home.x) << ',' << bitsHex(e.combat.home.y) << " arrived=";
        if (e.arrived) {
            o << *e.arrived;
        } else {
            o << -1;
        }
        o << " orders=";
        for (size_t i = 0; i < e.orders.size(); ++i) o << (i ? "|" : "") << orderText(e.orders[i]);
        o << '\n';
    }
    for (const auto& [id, e] : w.entities) {
        if (!e.isBuilding()) continue;
        o << "  @" << w.tick << " building " << id << " kind=" << e.building.kind << " hp=" << bitsHex(e.health.cur)
          << '/' << bitsHex(e.health.max) << " progress=" << bitsHex(e.building.progress) << " queue=[";
        for (size_t i = 0; i < e.building.queue.size(); ++i) o << (i ? ", " : "") << e.building.queue[i];
        o << "] qprog=" << bitsHex(e.building.queueProgress) << " cd=" << bitsHex(e.building.attackCooldown) << '\n';
    }
    for (const auto& [id, e] : w.entities) {
        if (!e.isSource()) continue;
        o << "  @" << w.tick << " source " << id << " essence=" << bitsHex(e.source.essence)
          << " anima=" << bitsHex(e.source.anima) << " animated=" << (e.source.animated ? 1 : 0) << '\n';
    }
    return o.str();
}

} // namespace husk
