// Spawning: unit.rs, building.rs (spawn_building, placement_clear),
// economy.rs (spawn_source), hero.rs (spawn_hero, spawn_item) and scenario.rs.

#include "World.hpp"

#include <algorithm>
#include <cmath>

namespace husk {

uint32_t spawnUnit(World& w, uint16_t kind, uint8_t team, Vec2 pos) {
    const UnitDef& def = w.cat().units.def(kind);
    // difficulty scales enemy hp at spawn
    const float hp = team != 0 ? def.hp * w.cat().difficulty.mults(w.difficulty).enemyHp : def.hp;
    const uint32_t id = w.allocId();
    Entity e;
    e.id = id;
    e.kind = EntityKind::Unit;
    e.pos = SimPos::at(pos);
    e.mover = {def.speed, def.radius};
    e.team = team;
    e.unitKind = kind;
    e.armor = def.armor;
    e.health = {hp, hp};
    e.combat.home = pos;
    w.entities[id] = std::move(e);
    return id;
}

std::vector<uint32_t> spawnSquad(World& w, uint8_t team, Vec2 center,
                                 const std::vector<std::pair<uint16_t, uint32_t>>& comp) {
    uint32_t total = 0;
    for (const auto& [kind, n] : comp) total += n;
    const uint32_t cols = asU32(fmaxr(std::ceil(std::sqrt(static_cast<float>(total))), 1.0f));
    const float spacing = 1.7f;
    std::vector<uint32_t> ids;
    ids.reserve(total);
    uint32_t i = 0;
    for (const auto& [kind, count] : comp) {
        for (uint32_t k = 0; k < count; ++k) {
            const uint32_t col = i % cols;
            const uint32_t row = i / cols;
            const uint32_t rows = (total + cols - 1) / cols; // div_ceil
            const Vec2 base =
                center + Vec2((static_cast<float>(col) - static_cast<float>(cols - 1) * 0.5f) * spacing,
                              (static_cast<float>(row) - static_cast<float>(rows - 1) * 0.5f) * spacing);
            // Two draws, x first: Rust evaluates `Vec2::new(a, b)` left to
            // right, and C++ does not promise that for constructor arguments.
            const float jx = w.rng.rangeF32(-0.25f, 0.25f);
            const float jy = w.rng.rangeF32(-0.25f, 0.25f);
            ids.push_back(spawnUnit(w, kind, team, base + Vec2(jx, jy)));
            ++i;
        }
    }
    return ids;
}

void spawnM0Scenario(World& w) {
    const uint16_t stalker = w.cat().units.id("stalker");
    spawnSquad(w, 0, Vec2(-32.0f, -32.0f), {{stalker, kM0UnitCount}});
}

uint32_t spawnBuilding(World& w, uint16_t kind, uint8_t team, Vec2 center, bool complete) {
    const BuildingDef& def = w.cat().buildings.def(kind);
    const uint32_t fw = def.footprint[0];
    const uint32_t fh = def.footprint[1];
    const auto [snapped, min] = w.grid.snapFootprint(center, fw, fh);
    std::vector<uint32_t> cells = w.grid.footprintCells(min, fw, fh);
    w.grid.setBlocked(cells, true);
    // pathing changed - drop every cached flow field
    w.flowFields.clear();

    const uint32_t id = w.allocId();
    const float radius = static_cast<float>(std::max(fw, fh)) * 0.5f;
    Entity e;
    e.id = id;
    e.kind = EntityKind::Building;
    e.pos = SimPos::at(snapped);
    e.mover = {0.0f, radius};
    e.team = team;
    e.armor = def.armor;
    e.health = {complete ? def.hp : def.hp * 0.1f, def.hp};
    e.building.kind = kind;
    e.building.progress = complete ? 1.0f : 0.0f;
    e.building.cells = std::move(cells);
    w.entities[id] = std::move(e);
    w.statics[id] = {snapped, radius};
    return id;
}

uint32_t spawnSource(World& w, uint16_t kind, Vec2 center) {
    const SourceDef& def = w.cat().sources.def(kind);
    const uint32_t fw = def.footprint[0];
    const uint32_t fh = def.footprint[1];
    const auto [snapped, min] = w.grid.snapFootprint(center, fw, fh);
    w.grid.setBlocked(w.grid.footprintCells(min, fw, fh), true);

    const uint32_t id = w.allocId();
    const float radius = static_cast<float>(std::max(fw, fh)) * 0.5f;
    Entity e;
    e.id = id;
    e.kind = EntityKind::Source;
    e.pos = SimPos::at(snapped);
    e.mover = {0.0f, radius};
    e.source.kind = kind;
    e.source.affinity = def.affinity;
    e.source.essence = def.essence;
    e.source.essenceMax = def.essence;
    e.source.anima = def.anima;
    e.source.animaPerEssence = def.essence > 0.0f ? def.anima / def.essence : 0.0f;
    w.entities[id] = std::move(e);
    w.statics[id] = {snapped, radius};
    return id;
}

uint32_t spawnHero(World& w, Vec2 pos) {
    const uint16_t kind = w.cat().units.id(w.cat().hero.unit);
    const uint32_t id = spawnUnit(w, kind, 0, pos);
    w.entities[id].hero = Hero{};
    w.heroState = HeroState::alive(id);
    return id;
}

uint32_t spawnItem(World& w, uint16_t kind, Vec2 pos) {
    const uint32_t id = w.allocId();
    Entity e;
    e.id = id;
    e.kind = EntityKind::Item;
    e.pos = SimPos::at(pos);
    e.mover = {0.0f, 0.35f};
    e.itemKind = kind;
    w.entities[id] = std::move(e);
    w.statics[id] = {pos, 0.35f};
    return id;
}

bool placementClear(const NavGrid& grid, CellCoords min, uint32_t w, uint32_t h,
                    const std::vector<std::pair<Vec2, float>>& units) {
    const std::vector<uint32_t> cells = grid.footprintCells(min, w, h);
    if (cells.size() != static_cast<size_t>(w) * h || !grid.cellsFree(cells)) return false;
    const uint8_t baseLevel = grid.level[cells[0]];
    for (uint32_t c : cells) {
        if (grid.level[c] != baseLevel || grid.ramp[c]) return false;
    }
    const Vec2 lo = grid.origin + Vec2(static_cast<float>(min.first), static_cast<float>(min.second)) * grid.cell;
    const Vec2 hi = lo + Vec2(static_cast<float>(w), static_cast<float>(h)) * grid.cell;
    for (const auto& [pos, radius] : units) {
        if (pos.x + radius > lo.x && pos.x - radius < hi.x && pos.y + radius > lo.y && pos.y - radius < hi.y) {
            return false;
        }
    }
    return true;
}

namespace {

struct SourceSpot {
    const char* name;
    float x;
    float y;
};

// The M2 macro map's sources (scenario.rs SOURCES), in the game's order.
constexpr SourceSpot kMacroSources[] = {
    {"tree", -38.0f, -23.0f},
    {"tree", -36.0f, -20.0f},
    {"tree", -40.0f, -19.0f},
    {"tree", -37.0f, -26.0f},
    {"tree", -25.0f, -38.0f},
    {"tree", -22.0f, -36.0f},
    {"ancient tree", -42.0f, -15.0f},
    {"ancient tree", 8.0f, 40.0f},
    {"car", -12.0f, -12.0f},
    {"car", -8.0f, -10.0f},
    {"car", 0.0f, -14.0f},
    {"car", 6.0f, -11.0f},
    {"transformer", 16.0f, -6.0f},
    {"transformer", 24.0f, 4.0f},
    {"stone section", -16.0f, 4.0f},
    {"stone section", -9.0f, 6.0f},
    {"stone section", 11.0f, 5.0f},
    {"stone section", 18.0f, -8.0f},
    {"fuel tank", 30.0f, -18.0f},
    {"fuel tank", 34.0f, -12.0f},
    {"pond", -30.0f, 16.0f},
    {"pond", -20.0f, 8.0f},
};

} // namespace

void spawnM2MacroScenario(World& w) {
    const Catalogs& c = w.cat();
    spawnBuilding(w, c.buildings.id("sanctum"), 0, Vec2(-32.0f, -32.0f), true);

    const uint16_t siphon = c.units.id("siphon");
    const uint16_t stalker = c.units.id("stalker");
    const uint16_t golem = c.units.id("golem");
    const uint16_t wisp = c.units.id("wisp");
    spawnSquad(w, 0, Vec2(-28.0f, -28.5f), {{siphon, 4}});
    spawnSquad(w, 0, Vec2(-27.0f, -34.5f), {{stalker, 2}});

    spawnHero(w, Vec2(-29.0f, -33.0f));

    w.economy.essence = 100.0f;

    spawnItem(w, c.items.id("rebar knuckles"), Vec2(-25.5f, -27.0f));
    spawnItem(w, c.items.id("asphalt plating"), Vec2(4.0f, -22.0f));
    spawnItem(w, c.items.id("live wire charm"), Vec2(30.0f, 22.0f));

    for (const SourceSpot& s : kMacroSources) {
        spawnSource(w, c.sources.id(s.name), Vec2(s.x, s.y));
    }

    spawnSquad(w, 1, Vec2(28.0f, 28.0f), {{stalker, 10}, {golem, 4}, {wisp, 2}});
}

} // namespace husk
