#include "World.hpp"

#include "Snapshot.hpp"

#include <mutex>

namespace husk {

// ---- orders ---------------------------------------------------------------------

Order Order::point(bool attack, uint32_t goal, Vec2 target) {
    Order o;
    o.kind = OrderKind::Point;
    o.attack = attack;
    o.goalCell = goal;
    o.target = target;
    return o;
}

Order Order::attackUnit(uint32_t target) {
    Order o;
    o.kind = OrderKind::Attack;
    o.id = target;
    return o;
}

Order Order::patrol(uint32_t goal, Vec2 target, Vec2 home) {
    Order o;
    o.kind = OrderKind::Patrol;
    o.goalCell = goal;
    o.target = target;
    o.home = home;
    return o;
}

Order Order::hold() {
    return Order{};
}

Order Order::extract(uint32_t source, uint32_t goal) {
    Order o;
    o.kind = OrderKind::Extract;
    o.id = source;
    o.goalCell = goal;
    return o;
}

Order Order::repair(uint32_t target, uint32_t goal) {
    Order o;
    o.kind = OrderKind::Repair;
    o.id = target;
    o.goalCell = goal;
    return o;
}

Order Order::build(uint32_t site, uint32_t goal) {
    Order o;
    o.kind = OrderKind::Build;
    o.id = site;
    o.goalCell = goal;
    return o;
}

Order Order::cast(uint8_t ability, CastTarget target, std::optional<uint32_t> goal) {
    Order o;
    o.kind = OrderKind::Cast;
    o.ability = ability;
    o.castTarget = target;
    o.hasGoal = goal.has_value();
    o.goalCell = goal.value_or(0);
    return o;
}

Order Order::pickup(uint32_t item, uint32_t goal) {
    Order o;
    o.kind = OrderKind::Pickup;
    o.id = item;
    o.goalCell = goal;
    return o;
}

OrderMsg OrderMsg::point(std::vector<uint32_t> units, bool attack, Vec2 target, bool queued) {
    OrderMsg m;
    m.kind = Kind::Point;
    m.units = std::move(units);
    m.attack = attack;
    m.target = target;
    m.queued = queued;
    return m;
}

OrderMsg OrderMsg::attackUnit(std::vector<uint32_t> units, uint32_t target, bool queued) {
    OrderMsg m;
    m.kind = Kind::Attack;
    m.units = std::move(units);
    m.id = target;
    m.queued = queued;
    return m;
}

OrderMsg OrderMsg::patrol(std::vector<uint32_t> units, Vec2 target) {
    OrderMsg m;
    m.kind = Kind::Patrol;
    m.units = std::move(units);
    m.target = target;
    return m;
}

OrderMsg OrderMsg::hold(std::vector<uint32_t> units) {
    OrderMsg m;
    m.kind = Kind::Hold;
    m.units = std::move(units);
    return m;
}

OrderMsg OrderMsg::stop(std::vector<uint32_t> units) {
    OrderMsg m;
    m.kind = Kind::Stop;
    m.units = std::move(units);
    return m;
}

OrderMsg OrderMsg::extract(std::vector<uint32_t> units, uint32_t source, bool queued) {
    OrderMsg m;
    m.kind = Kind::Extract;
    m.units = std::move(units);
    m.id = source;
    m.queued = queued;
    return m;
}

OrderMsg OrderMsg::repair(std::vector<uint32_t> units, uint32_t target, bool queued) {
    OrderMsg m;
    m.kind = Kind::Repair;
    m.units = std::move(units);
    m.id = target;
    m.queued = queued;
    return m;
}

OrderMsg OrderMsg::build(std::vector<uint32_t> units, uint32_t site, bool queued) {
    OrderMsg m;
    m.kind = Kind::Build;
    m.units = std::move(units);
    m.id = site;
    m.queued = queued;
    return m;
}

OrderMsg OrderMsg::placeBuilding(uint32_t builder, uint16_t kind, Vec2 center) {
    OrderMsg m;
    m.kind = Kind::PlaceBuilding;
    m.id = builder;
    m.kind16 = kind;
    m.target = center;
    return m;
}

OrderMsg OrderMsg::produce(uint32_t building, uint16_t kind) {
    OrderMsg m;
    m.kind = Kind::Produce;
    m.id = building;
    m.kind16 = kind;
    return m;
}

OrderMsg OrderMsg::setRally(uint32_t building, Vec2 target) {
    OrderMsg m;
    m.kind = Kind::SetRally;
    m.id = building;
    m.target = target;
    return m;
}

OrderMsg OrderMsg::castAbility(uint32_t hero, uint8_t ability, CastTarget target, bool queued) {
    OrderMsg m;
    m.kind = Kind::Cast;
    m.id = hero;
    m.ability = ability;
    m.cast = target;
    m.queued = queued;
    return m;
}

OrderMsg OrderMsg::learn(uint32_t hero, uint8_t ability) {
    OrderMsg m;
    m.kind = Kind::Learn;
    m.id = hero;
    m.ability = ability;
    return m;
}

OrderMsg OrderMsg::revive() {
    OrderMsg m;
    m.kind = Kind::Revive;
    return m;
}

OrderMsg OrderMsg::pickup(uint32_t hero, uint32_t item, bool queued) {
    OrderMsg m;
    m.kind = Kind::Pickup;
    m.id = hero;
    m.item = item;
    m.queued = queued;
    return m;
}

OrderMsg OrderMsg::dropItem(uint32_t hero, uint8_t slot) {
    OrderMsg m;
    m.kind = Kind::DropItem;
    m.id = hero;
    m.slot = slot;
    return m;
}

OrderMsg OrderMsg::useItem(uint32_t hero, uint8_t slot) {
    OrderMsg m;
    m.kind = Kind::UseItem;
    m.id = hero;
    m.slot = slot;
    return m;
}

OrderMsg OrderMsg::research(uint32_t building, uint16_t upgrade) {
    OrderMsg m;
    m.kind = Kind::Research;
    m.id = building;
    m.kind16 = upgrade;
    return m;
}

OrderMsg OrderMsg::cancelProduce(uint32_t building) {
    OrderMsg m;
    m.kind = Kind::CancelProduce;
    m.id = building;
    return m;
}

// ---- economy state ----------------------------------------------------------------

uint8_t AffinityState::tier(Affinity a) const {
    const float c = cumulative[static_cast<size_t>(a)];
    if (c >= kTier2At) return 2;
    if (c > 0.0f) return 1;
    return 0;
}

bool AffinityState::satisfies(const std::optional<AffinityRequirement>& requirement) const {
    return !requirement || tier(requirement->affinity) >= requirement->tier;
}

uint8_t ResearchState::level(uint16_t upgrade) const {
    return upgrade < levels.size() ? levels[upgrade] : 0;
}

void ResearchState::bump(uint16_t upgrade) {
    if (levels.size() <= upgrade) levels.resize(static_cast<size_t>(upgrade) + 1, 0);
    levels[upgrade] += 1;
}

float ResearchState::atkSpeedMult(const Catalog<UpgradeDef>& catalog) const {
    float m = 1.0f;
    for (size_t i = 0; i < catalog.defs.size(); ++i) {
        const UpgradeEffect& e = catalog.defs[i].effect;
        if (e.kind == UpgradeEffect::Kind::AttackSpeed) {
            m *= 1.0f + e.pctPerLevel * static_cast<float>(level(static_cast<uint16_t>(i)));
        }
    }
    return m;
}

float ResearchState::gatherMult(const Catalog<UpgradeDef>& catalog) const {
    float m = 1.0f;
    for (size_t i = 0; i < catalog.defs.size(); ++i) {
        const UpgradeEffect& e = catalog.defs[i].effect;
        if (e.kind == UpgradeEffect::Kind::GatherRate) {
            m *= 1.0f + e.pctPerLevel * static_cast<float>(level(static_cast<uint16_t>(i)));
        }
    }
    return m;
}

// ---- the world ------------------------------------------------------------------

World::World(std::shared_ptr<const Catalogs> cats, uint64_t seed)
    : catalogs(std::move(cats)), rng(makeSimRng(seed)), map(MapDef::m0TestMap()) {
    grid = NavGrid::fromDef(map);
}

Entity* World::get(uint32_t id) {
    auto it = entities.find(id);
    return it == entities.end() ? nullptr : &it->second;
}

const Entity* World::get(uint32_t id) const {
    auto it = entities.find(id);
    return it == entities.end() ? nullptr : &it->second;
}

Entity* World::indexed(uint32_t id) {
    Entity* e = get(id);
    return e && e->indexed ? e : nullptr;
}

const Entity* World::indexed(uint32_t id) const {
    const Entity* e = get(id);
    return e && e->indexed ? e : nullptr;
}

void World::unindex(uint32_t id) {
    if (Entity* e = get(id)) e->indexed = false;
}

void World::despawnDeferred(uint32_t id) {
    m_commands.push_back([id](World& w) { w.despawnNow(id); });
}

void World::applyCommands() {
    // Swapped out first: a command must not see its own queue.
    std::vector<std::function<void(World&)>> commands;
    commands.swap(m_commands);
    for (auto& c : commands) c(*this);
}

std::shared_ptr<const Catalogs> sharedCatalogs() {
    static std::once_flag once;
    static std::shared_ptr<const Catalogs> catalogs;
    std::call_once(once, [] { catalogs = std::make_shared<const Catalogs>(loadCatalogs(defaultDataRoot())); });
    return catalogs;
}

void resetSim(World& w, uint64_t seed) {
    clearSimWorld(w);
    w.map = MapDef::m0TestMap();
    w.grid = NavGrid::fromDef(w.map);
    w.rng = makeSimRng(seed);
    w.tick = 0;
    w.hash = 0;
    w.idAlloc = 0;
    w.economy = {};
    w.affinity = {};
    w.research = {};
    w.will = {};
    w.mission.reset();
    w.missionHooks.clear();
    w.heroState = {};
}

} // namespace husk
