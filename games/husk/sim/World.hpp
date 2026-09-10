#pragma once

// HUSK's simulation world, ported from Bevy to plain data.
//
// The game keeps its sim in Bevy's ECS, but borrows nothing from it except the
// containers, so the port keeps none of it. Every sim entity is one `Entity`
// record, keyed by its SimId in a std::map, which makes SimId order the
// iteration order everywhere. The game iterates in SimId order wherever order
// can change a result, and by raw query order only where it provably cannot, so
// SimId order everywhere is a superset of its rule. If some Rust system does
// depend on query order after all, that shows up as a hash mismatch to go and
// read, not as a second source of nondeterminism.
//
// TWO BEVY SEMANTICS THE PORT KEEPS ON PURPOSE:
//
//  - SimIndex is separate from existence. A dying entity leaves the index at
//    once (`index.0.remove`) but stays queryable until its despawn applies. The
//    `indexed` flag is the index, and the map is existence.
//
//  - Commands are deferred. A system that despawns or inserts through
//    `Commands` does not see the change itself; Bevy applies it before the
//    next system in the chain. `defer()` queues exactly those operations, and
//    `step()` applies them after each system. Exclusive systems (`&mut World`)
//    act immediately, as they do in the game.

#include "Data.hpp"
#include "Flow.hpp"
#include "Map.hpp"
#include "MissionTypes.hpp"
#include "Rng.hpp"
#include "Vec2.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace husk {

inline constexpr double kSimHz = 20.0;
inline constexpr float kSimHzF = 20.0f;       // `SIM_HZ as f32`
inline constexpr float kSimDt = 1.0f / 20.0f; // SIM_DT
inline constexpr uint64_t kDefaultSeed = 0x4855534Bull; // "HUSK"

// ---- components ---------------------------------------------------------------

struct SimPos {
    Vec2 prev; // last tick's, for interpolation
    Vec2 cur;
    static SimPos at(Vec2 p) { return {p, p}; }
};

struct Mover {
    float speed = 0.0f; // world units per second; 0 for statics
    float radius = 0.0f;
};

struct Health {
    float cur = 0.0f;
    float max = 0.0f;
};

struct CombatState {
    float cooldown = 0.0f;
    std::optional<uint32_t> target;
    Vec2 home; // the leash anchor
};

struct StatMods {
    float damageAdd = 0.0f;
    float atkSpeedMult = 1.0f;
    float moveMult = 1.0f;
    float regen = 0.0f;
};

struct CastTarget {
    enum class Kind : uint8_t { Id, Point, None };
    Kind kind = Kind::None;
    uint32_t id = 0;
    Vec2 point;

    static CastTarget ofId(uint32_t id) { return {Kind::Id, id, {}}; }
    static CastTarget ofPoint(Vec2 p) { return {Kind::Point, 0, p}; }
    static CastTarget none() { return {}; }
};

enum class OrderKind : uint8_t { Point, Attack, Patrol, Hold, Extract, Repair, Build, Cast, Pickup };

// One queued order; the front of a unit's queue is the active one.
struct Order {
    OrderKind kind = OrderKind::Hold;
    bool attack = false;   // Point
    uint32_t goalCell = 0; // every walking order; Cast only when hasGoal
    bool hasGoal = true;   // Cast's goal_cell is an Option
    Vec2 target;           // Point, Patrol
    Vec2 home;             // Patrol
    uint32_t id = 0;       // Attack target, Extract source, Repair target, Build site, Pickup item
    uint8_t ability = 0;   // Cast
    CastTarget castTarget; // Cast

    static Order point(bool attack, uint32_t goal, Vec2 target);
    static Order attackUnit(uint32_t target);
    static Order patrol(uint32_t goal, Vec2 target, Vec2 home);
    static Order hold();
    static Order extract(uint32_t source, uint32_t goal);
    static Order repair(uint32_t target, uint32_t goal);
    static Order build(uint32_t site, uint32_t goal);
    static Order cast(uint8_t ability, CastTarget target, std::optional<uint32_t> goal);
    static Order pickup(uint32_t item, uint32_t goal);
};

using Orders = std::deque<Order>;

inline constexpr size_t kInventorySlots = 6;

struct Hero {
    uint8_t level = 1;
    float xp = 0.0f;
    uint8_t points = 1; // one point at level 1
    std::array<uint8_t, 4> ranks{};
    std::array<float, 4> cooldowns{};
    std::array<std::optional<uint16_t>, kInventorySlots> inventory{};
};

struct OverchargeBuff {
    uint64_t untilTick = 0;
    float atkSpeed = 0.0f;
    float moveSpeed = 0.0f;
    float burn = 0.0f;
};

struct SlowDebuff {
    uint64_t untilTick = 0;
    float slow = 0.0f;
};

struct AvatarBuff {
    uint64_t untilTick = 0;
    float hpBonus = 0.0f;
    float dmgBonus = 0.0f;
};

struct TimedLife {
    uint64_t untilTick = 0;
};

inline constexpr size_t kQueueCap = 7;

struct Building {
    uint16_t kind = 0;
    float progress = 0.0f; // construction, 0..=1
    std::vector<uint16_t> queue;
    float queueProgress = 0.0f;
    std::optional<Vec2> rally;
    std::vector<uint32_t> cells; // nav cells blocked while standing
    float attackCooldown = 0.0f;

    bool complete() const { return progress >= 1.0f; }
};

struct EssenceSource {
    uint16_t kind = 0;
    Affinity affinity = Affinity::Steel;
    float essence = 0.0f;
    float essenceMax = 0.0f;
    float anima = 0.0f;
    float animaPerEssence = 0.0f;
    bool animated = false; // raised as a thrall by Avatar of Essence

    bool husked() const { return essence <= 0.0f; }
};

enum class EntityKind : uint8_t { Unit, Building, Source, Item };

// Every sim entity. What a kind carries is fixed by how the game spawns it:
//   Unit      pos mover team armor health unitKind combat mods orders arrived
//             (+ hero, buffs, timedLife, raider)
//   Building  pos mover team armor health building          ("Structure")
//   Source    pos mover source
//   Item      pos mover itemKind
struct Entity {
    uint32_t id = 0;
    EntityKind kind = EntityKind::Unit;
    bool indexed = true; // in SimIndex

    SimPos pos;
    Mover mover;

    // units and buildings
    uint8_t team = 0;
    ArmorType armor = ArmorType::Unarmored;
    Health health;

    // units
    uint16_t unitKind = 0;
    CombatState combat;
    StatMods mods;
    Orders orders;
    std::optional<uint32_t> arrived;
    std::optional<Hero> hero;
    std::optional<OverchargeBuff> overcharge;
    std::optional<SlowDebuff> slow;
    std::optional<AvatarBuff> avatar;
    std::optional<TimedLife> timedLife;
    std::optional<uint32_t> raider; // the raid objective's SimId; UINT32_MAX = none yet

    Building building;    // buildings
    EssenceSource source; // sources
    uint16_t itemKind = 0; // items

    bool isUnit() const { return kind == EntityKind::Unit; }
    bool isBuilding() const { return kind == EntityKind::Building; }
    bool isSource() const { return kind == EntityKind::Source; }
    bool isItem() const { return kind == EntityKind::Item; }
    // Team, Armor and Health: units and buildings.
    bool hasTeam() const { return isUnit() || isBuilding(); }
};

// ---- resources ------------------------------------------------------------------

struct HeroState {
    enum class Kind : uint8_t { Absent, Alive, Dead, Reviving };
    Kind kind = Kind::Absent;
    uint32_t id = 0;   // Alive
    Hero saved;        // Dead, Reviving
    uint64_t tick = 0; // Dead: died_tick; Reviving: ready_tick

    static HeroState alive(uint32_t id) { return {Kind::Alive, id, {}, 0}; }
    static HeroState dead(Hero saved, uint64_t diedTick) { return {Kind::Dead, 0, std::move(saved), diedTick}; }
    static HeroState reviving(Hero saved, uint64_t readyTick) {
        return {Kind::Reviving, 0, std::move(saved), readyTick};
    }
};

struct PlayerEconomy {
    float essence = 0.0f;
    float anima = 0.0f;
};

inline constexpr float kTier2At = 300.0f;

struct AffinityState {
    std::array<float, kAffinityCount> cumulative{};

    uint8_t tier(Affinity a) const;
    bool satisfies(const std::optional<AffinityRequirement>& requirement) const;
};

struct Research {
    uint16_t upgrade = 0;
    uint64_t readyTick = 0;
};

struct ResearchState {
    std::vector<uint8_t> levels; // grown lazily; missing means 0
    std::optional<Research> inProgress;

    uint8_t level(uint16_t upgrade) const;
    void bump(uint16_t upgrade);
    float atkSpeedMult(const Catalog<UpgradeDef>& catalog) const;
    float gatherMult(const Catalog<UpgradeDef>& catalog) const;
};

inline constexpr uint32_t kWillCap = 100;

struct WillState {
    uint32_t used = 0;
    uint32_t cap = 0;
};

struct PendingPlacement {
    uint32_t builder = 0;
    uint16_t kind = 0;
    Vec2 center;
};

// ---- transient events (never hashed) ----------------------------------------

struct AttackFx {
    Vec2 from;
    Vec2 to;
    bool ranged = false;
    uint16_t kind = 0;     // attacker kind; UINT16_MAX for a tower
    uint32_t attacker = 0; // UINT32_MAX for a tower
    uint32_t victim = 0;
};

struct DeathFx {
    Vec2 pos;
    float radius = 0.0f;
    uint8_t team = 0;
};

struct KillFx {
    Vec2 pos;
    uint8_t victimTeam = 0;
    float xp = 0.0f;
    uint16_t victimKind = UINT16_MAX; // UINT16_MAX for structures: they never drop
};

struct CastFx {
    enum class Kind : uint8_t { SurgeBeam, Pulse, Overcharge, Avatar, LevelUp, Heal };
    Kind kind = Kind::LevelUp;
    Vec2 pos; // `from` for a SurgeBeam
    Vec2 to;  // SurgeBeam
    float radius = 0.0f; // Pulse
};

struct MissionFx {
    enum class Kind : uint8_t { Message, Say, ObjectiveAdded, ObjectiveComplete, ObjectiveFailed, Outcome };
    Kind kind = Kind::Message;
    std::string speaker;
    std::string text;
    bool victory = false;
};

struct SimEvents {
    std::vector<AttackFx> attacks;
    std::vector<DeathFx> deaths;
    std::vector<KillFx> kills;
    std::vector<CastFx> casts;
    std::vector<MissionFx> mission;
};

// ---- orders from outside ------------------------------------------------------

// A command addressed by SimId. The input layer or a script pushes these and
// the sim drains them at the start of the next tick.
struct OrderMsg {
    enum class Kind : uint8_t {
        Point, Attack, Patrol, Hold, Stop, Extract, Repair, Build,
        PlaceBuilding, Produce, SetRally, Cast, Learn, Revive, Pickup,
        DropItem, UseItem, Research, CancelProduce,
    };

    Kind kind = Kind::Stop;
    std::vector<uint32_t> units;
    bool attack = false;
    bool queued = false;
    Vec2 target;         // Point, Patrol, SetRally; PlaceBuilding's centre
    uint32_t id = 0;     // Attack target, Extract source, Repair target, Build site,
                         // the building of Produce/SetRally/Research/CancelProduce,
                         // PlaceBuilding's builder, the hero of Cast/Learn/Pickup/DropItem/UseItem
    uint32_t item = 0;   // Pickup
    uint16_t kind16 = 0; // Produce's unit kind, PlaceBuilding's building kind, Research's upgrade
    uint8_t ability = 0; // Cast, Learn
    uint8_t slot = 0;    // DropItem, UseItem
    CastTarget cast;     // Cast

    static OrderMsg point(std::vector<uint32_t> units, bool attack, Vec2 target, bool queued);
    static OrderMsg attackUnit(std::vector<uint32_t> units, uint32_t target, bool queued);
    static OrderMsg patrol(std::vector<uint32_t> units, Vec2 target);
    static OrderMsg hold(std::vector<uint32_t> units);
    static OrderMsg stop(std::vector<uint32_t> units);
    static OrderMsg extract(std::vector<uint32_t> units, uint32_t source, bool queued);
    static OrderMsg repair(std::vector<uint32_t> units, uint32_t target, bool queued);
    static OrderMsg build(std::vector<uint32_t> units, uint32_t site, bool queued);
    static OrderMsg placeBuilding(uint32_t builder, uint16_t kind, Vec2 center);
    static OrderMsg produce(uint32_t building, uint16_t kind);
    static OrderMsg setRally(uint32_t building, Vec2 target);
    static OrderMsg castAbility(uint32_t hero, uint8_t ability, CastTarget target, bool queued);
    static OrderMsg learn(uint32_t hero, uint8_t ability);
    static OrderMsg revive();
    static OrderMsg pickup(uint32_t hero, uint32_t item, bool queued);
    static OrderMsg dropItem(uint32_t hero, uint8_t slot);
    static OrderMsg useItem(uint32_t hero, uint8_t slot);
    static OrderMsg research(uint32_t building, uint16_t upgrade);
    static OrderMsg cancelProduce(uint32_t building);
};

// ---- the world ------------------------------------------------------------------

struct World {
    // SimPlugin::build: the M0 test map, a seeded RNG, every resource at its
    // default. The catalogs are shared and never mutated.
    explicit World(std::shared_ptr<const Catalogs> catalogs, uint64_t seed = kDefaultSeed);

    const Catalogs& cat() const { return *catalogs; }

    std::shared_ptr<const Catalogs> catalogs;

    Pcg32 rng;
    MapDef map;
    NavGrid grid;
    uint64_t tick = 0; // SimTick: completed ticks
    uint64_t hash = 0; // SimHash
    std::vector<OrderMsg> orderQueue;
    FlowFields flowFields;
    SimEvents events;
    PlayerEconomy economy;
    AffinityState affinity;
    ResearchState research;
    WillState will;
    std::vector<PendingPlacement> placements;
    HeroState heroState;
    std::vector<std::pair<uint16_t, Vec2>> pendingDrops;
    std::optional<MissionRuntime> mission;
    // Per-mission code hooks: run every tick before triggers, and bound by the
    // same determinism rules as any system. Code, so never saved.
    std::vector<std::function<void(World&)>> missionHooks;
    uint32_t idAlloc = 0;
    Difficulty difficulty = Difficulty::Normal;
    bool paused = false; // SimPaused: gates the host's driver, not step()

    std::map<uint32_t, Entity> entities;
    std::map<uint32_t, std::pair<Vec2, float>> statics; // StaticIndex: position, radius

    // `world.get(entity)`: the entity, whether or not it is still indexed.
    Entity* get(uint32_t id);
    const Entity* get(uint32_t id) const;
    // `index.0.get(&id)`: only while indexed.
    Entity* indexed(uint32_t id);
    const Entity* indexed(uint32_t id) const;
    // `index.0.remove(&id)`
    void unindex(uint32_t id);

    uint32_t allocId() { return idAlloc++; }

    // `world.despawn(entity)`, immediately (exclusive systems).
    void despawnNow(uint32_t id) { entities.erase(id); }
    // `commands.entity(e).despawn()`: applies after the running system.
    void despawnDeferred(uint32_t id);
    // Any other deferred command.
    void defer(std::function<void(World&)> command) { m_commands.push_back(std::move(command)); }
    void applyCommands();

private:
    std::vector<std::function<void(World&)>> m_commands;
};

// The catalogs from games/husk/data, loaded once per process.
std::shared_ptr<const Catalogs> sharedCatalogs();

// ---- spawning (unit.rs, building.rs, economy.rs, hero.rs, scenario.rs) --------------

inline constexpr uint32_t kM0UnitCount = 30;

// Each returns the new entity's SimId.
uint32_t spawnUnit(World& w, uint16_t kind, uint8_t team, Vec2 pos);
std::vector<uint32_t> spawnSquad(World& w, uint8_t team, Vec2 center,
                                 const std::vector<std::pair<uint16_t, uint32_t>>& comp);
void spawnM0Scenario(World& w);
uint32_t spawnBuilding(World& w, uint16_t kind, uint8_t team, Vec2 center, bool complete);
uint32_t spawnSource(World& w, uint16_t kind, Vec2 center);
uint32_t spawnHero(World& w, Vec2 pos);
uint32_t spawnItem(World& w, uint16_t kind, Vec2 pos);
void spawnM2MacroScenario(World& w);

// A (w, h) footprint at `min` is placeable: cells free, one flat tier with no
// ramps, and no unit overlapping the rect expanded by its radius.
bool placementClear(const NavGrid& grid, CellCoords min, uint32_t w, uint32_t h,
                    const std::vector<std::pair<Vec2, float>>& units);

// ---- the tick ------------------------------------------------------------------------

// One SimStep: the 22 systems, chained, in the game's order.
void step(World& w);

// A blank slate: fresh seed, the M0 map, no entities. Difficulty is kept.
void resetSim(World& w, uint64_t seed);

void updateWill(World& w);
void drainOrders(World& w);
void advanceResearch(World& w);
void applyPlacements(World& w);
void retargetRaiders(World& w);
void ensureFlowFields(World& w);
void acquireTargets(World& w);
void heroStats(World& w);
void stepMovement(World& w);
void siphonChannels(World& w);
void tickProduction(World& w);
void heroActions(World& w);
void tickBuffs(World& w);
void tickAttacks(World& w);
void towerAttacks(World& w);
void rollLootDrops(World& w);
void passiveRegen(World& w);
void runTriggers(World& w);
void xpAwards(World& w);
void heroRevive(World& w);
void updateSimHash(World& w);

// Hero XP, shared by kill sharing and objective grants.
void applyXp(const HeroConfig& config, float xp, Vec2 pos, Hero& hero, Health& hp, SimEvents& events);
// An objective's XP grant: to the live hero, forfeited while dead or reviving.
void grantHeroXp(World& w, float xp);

// ---- the oracle's view -----------------------------------------------------------------

// `update_sim_hash`'s value for the current state.
uint64_t computeSimHash(const World& w);
// The same hash split at its domain boundaries - state, units, buildings,
// sources, items - each from a fresh FNV offset, as the oracle's --domains.
std::array<uint64_t, 5> domainHashes(const World& w);
// The oracle's --entities dump for the current tick.
std::string dumpEntities(const World& w);

} // namespace husk
