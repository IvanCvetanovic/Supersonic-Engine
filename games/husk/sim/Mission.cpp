#include "Mission.hpp"

#include "Ron.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace husk {

namespace {

using ron::Value;

// ---- reading the RON shapes -------------------------------------------------------

const Value* opt(const Value& s, std::string_view name, std::string_view path) {
    return ron::asStruct(s, path).field(name);
}

float f32Or(const Value& s, std::string_view name, std::string_view path, float fallback) {
    const Value* v = opt(s, name, path);
    return v ? ron::asF32(*v, ron::join(path, name)) : fallback;
}

bool boolOr(const Value& s, std::string_view name, std::string_view path, bool fallback) {
    const Value* v = opt(s, name, path);
    return v ? ron::asBool(*v, ron::join(path, name)) : fallback;
}

std::string strOr(const Value& s, std::string_view name, std::string_view path) {
    const Value* v = opt(s, name, path);
    return v ? ron::asString(*v, ron::join(path, name)) : std::string();
}

std::array<float, 4> quad(const Value& v, std::string_view path) {
    const auto& t = ron::asTuple(v, 4, path);
    return {ron::asF32(t[0], path), ron::asF32(t[1], path), ron::asF32(t[2], path), ron::asF32(t[3], path)};
}

std::vector<std::array<float, 4>> quads(const Value& s, std::string_view name, std::string_view path, bool required) {
    std::vector<std::array<float, 4>> out;
    const Value* v = required ? &ron::require(s, name, path) : opt(s, name, path);
    if (!v) return out;
    const std::string p = ron::join(path, name);
    for (const Value& item : ron::asList(*v, p)) out.push_back(quad(item, p));
    return out;
}

MapSpec mapSpec(const Value& s, std::string_view path) {
    MapSpec m;
    m.half = ron::asF32(ron::require(s, "half", path), ron::join(path, "half"));
    m.obstacles = quads(s, "obstacles", path, true);
    m.roads = quads(s, "roads", path, false);
    if (const Value* v = opt(s, "plateaus", path)) {
        const std::string p = ron::join(path, "plateaus");
        for (const Value& item : ron::asList(*v, p)) {
            const auto& t = ron::asTuple(item, 5, p);
            m.plateaus.push_back({{ron::asF32(t[0], p), ron::asF32(t[1], p), ron::asF32(t[2], p), ron::asF32(t[3], p)},
                                  ron::asU8(t[4], p)});
        }
    }
    if (const Value* v = opt(s, "plateau_polys", path)) {
        const std::string p = ron::join(path, "plateau_polys");
        for (const Value& item : ron::asList(*v, p)) {
            const auto& t = ron::asTuple(item, 2, p);
            std::vector<std::pair<float, float>> verts;
            for (const Value& vert : ron::asList(t[0], p)) {
                const auto& xy = ron::asTuple(vert, 2, p);
                verts.emplace_back(ron::asF32(xy[0], p), ron::asF32(xy[1], p));
            }
            m.plateauPolys.emplace_back(std::move(verts), ron::asU8(t[1], p));
        }
    }
    m.ramps = quads(s, "ramps", path, false);
    m.bumps = quads(s, "bumps", path, false);
    return m;
}

std::vector<NamedSpot> spots(const Value& s, std::string_view name, std::string_view path) {
    std::vector<NamedSpot> out;
    const Value* v = opt(s, name, path);
    if (!v) return out;
    const std::string p = ron::join(path, name);
    for (const Value& item : ron::asList(*v, p)) {
        const auto& t = ron::asTuple(item, 3, p);
        out.push_back({ron::asString(t[0], p), ron::asF32(t[1], p), ron::asF32(t[2], p)});
    }
    return out;
}

std::vector<NamedSquad> squads(const Value& s, std::string_view name, std::string_view path) {
    std::vector<NamedSquad> out;
    const Value* v = opt(s, name, path);
    if (!v) return out;
    const std::string p = ron::join(path, name);
    for (const Value& item : ron::asList(*v, p)) {
        const auto& t = ron::asTuple(item, 4, p);
        out.push_back({ron::asString(t[0], p), ron::asU32(t[1], p), ron::asF32(t[2], p), ron::asF32(t[3], p)});
    }
    return out;
}

SpawnSpec spawnSpec(const Value& s, std::string_view path) {
    ron::asStruct(s, path);
    SpawnSpec out;
    if (const Value* v = opt(s, "hero", path)) {
        const std::string p = ron::join(path, "hero");
        if (const Value* xy = ron::asOption(*v, p)) {
            const auto& t = ron::asTuple(*xy, 2, p);
            out.hero = std::pair<float, float>{ron::asF32(t[0], p), ron::asF32(t[1], p)};
        }
    }
    out.buildings = spots(s, "buildings", path);
    out.units = squads(s, "units", path);
    out.enemies = squads(s, "enemies", path);
    out.sources = spots(s, "sources", path);
    out.items = spots(s, "items", path);
    out.startEssence = f32Or(s, "start_essence", path, 0.0f);
    return out;
}

std::vector<SceneStep> scenes(const Value& s, std::string_view name, std::string_view path) {
    std::vector<SceneStep> out;
    const Value* v = opt(s, name, path);
    if (!v) return out;
    const std::string p = ron::join(path, name);
    for (const Value& item : ron::asList(*v, p)) {
        SceneStep step;
        step.at = ron::asF32(ron::require(item, "at", p), ron::join(p, "at"));
        step.subtitle = strOr(item, "subtitle", p);
        step.speaker = strOr(item, "speaker", p);
        if (const Value* cam = opt(item, "camera", p)) {
            if (const Value* c3 = ron::asOption(*cam, ron::join(p, "camera"))) {
                const auto& t = ron::asTuple(*c3, 3, p);
                step.camera = std::array<float, 3>{ron::asF32(t[0], p), ron::asF32(t[1], p), ron::asF32(t[2], p)};
            }
        }
        if (const Value* actors = opt(item, "actors", p)) {
            for (const Value& a : ron::asList(*actors, p)) {
                const auto& t = ron::asTuple(a, 2, p);
                step.actors.emplace_back(ron::asF32(t[0], p), ron::asF32(t[1], p));
            }
        }
        step.storm = f32Or(item, "storm", p, 0.0f);
        step.flash = f32Or(item, "flash", p, 0.0f);
        out.push_back(std::move(step));
    }
    return out;
}

[[noreturn]] void unknownVariant(std::string_view path, std::string_view name) {
    throw ron::Error(std::string(path) + ": unknown variant `" + std::string(name) + "`");
}

// A newtype variant's one payload, or a tuple variant's element.
const Value& element(const ron::Variant& var, size_t i, size_t arity, std::string_view path) {
    if (!var.payload || var.payload->kind != Value::Kind::Tuple || var.payload->items.size() != arity) {
        throw ron::Error(std::string(path) + ": `" + std::string(var.name) + "` expects " + std::to_string(arity) +
                         " value(s)");
    }
    return var.payload->items[i];
}

const Value& structPayload(const ron::Variant& var, std::string_view path) {
    if (!var.payload || var.payload->kind != Value::Kind::Struct) {
        throw ron::Error(std::string(path) + ": `" + std::string(var.name) + "` expects named fields");
    }
    return *var.payload;
}

float fieldF32(const Value& s, std::string_view name, std::string_view path) {
    return ron::asF32(ron::require(s, name, path), ron::join(path, name));
}

TriggerCond cond(const Value& v, std::string_view path) {
    const ron::Variant var = ron::asVariant(v, path);
    TriggerCond c;
    using K = TriggerCond::Kind;
    const std::string_view n = var.name;
    if (n == "TimeAtLeast") {
        c.kind = K::TimeAtLeast;
        c.amount = ron::asF32(element(var, 0, 1, path), path);
    } else if (n == "TotalExtractedAtLeast") {
        c.kind = K::TotalExtractedAtLeast;
        c.amount = ron::asF32(element(var, 0, 1, path), path);
    } else if (n == "EssenceAtLeast") {
        c.kind = K::EssenceAtLeast;
        c.amount = ron::asF32(element(var, 0, 1, path), path);
    } else if (n == "AnimaAtLeast") {
        c.kind = K::AnimaAtLeast;
        c.amount = ron::asF32(element(var, 0, 1, path), path);
    } else if (n == "HeroLevelAtLeast") {
        c.kind = K::HeroLevelAtLeast;
        c.count = ron::asU8(element(var, 0, 1, path), path);
    } else if (n == "HeroRankAtLeast") {
        c.kind = K::HeroRankAtLeast;
        const Value& s = structPayload(var, path);
        c.slot = ron::asU8(ron::require(s, "slot", path), path);
        c.rank = ron::asU8(ron::require(s, "rank", path), path);
    } else if (n == "HeroInArea") {
        c.kind = K::HeroInArea;
        const Value& s = structPayload(var, path);
        c.x = fieldF32(s, "x", path);
        c.y = fieldF32(s, "y", path);
        c.radius = fieldF32(s, "radius", path);
    } else if (n == "PlayerUnitsOfAtLeast") {
        c.kind = K::PlayerUnitsOfAtLeast;
        const Value& s = structPayload(var, path);
        c.name = ron::asString(ron::require(s, "unit", path), path);
        c.count = ron::asU32(ron::require(s, "count", path), path);
    } else if (n == "PlayerBuildingsOfAtLeast") {
        c.kind = K::PlayerBuildingsOfAtLeast;
        const Value& s = structPayload(var, path);
        c.name = ron::asString(ron::require(s, "building", path), path);
        c.count = ron::asU32(ron::require(s, "count", path), path);
    } else if (n == "ArmyWillAtLeast") {
        c.kind = K::ArmyWillAtLeast;
        c.count = ron::asU32(element(var, 0, 1, path), path);
    } else if (n == "EnemyUnitsAtMost") {
        c.kind = K::EnemyUnitsAtMost;
        c.count = ron::asU32(element(var, 0, 1, path), path);
    } else if (n == "PlayerWiped" && !var.payload) {
        c.kind = K::PlayerWiped;
    } else if (n == "ItemsOnGroundAtMost") {
        c.kind = K::ItemsOnGroundAtMost;
        c.count = ron::asU32(element(var, 0, 1, path), path);
    } else if (n == "ObjectiveComplete") {
        c.kind = K::ObjectiveComplete;
        c.name = ron::asString(element(var, 0, 1, path), path);
    } else if (n == "ObjectiveActive") {
        c.kind = K::ObjectiveActive;
        c.name = ron::asString(element(var, 0, 1, path), path);
    } else if (n == "ObjectiveActiveForAtLeast") {
        c.kind = K::ObjectiveActiveForAtLeast;
        c.name = ron::asString(element(var, 0, 2, path), path);
        c.amount = ron::asF32(element(var, 1, 2, path), path);
    } else if (n == "AllRequiredObjectivesComplete" && !var.payload) {
        c.kind = K::AllRequiredObjectivesComplete;
    } else {
        unknownVariant(path, n);
    }
    return c;
}

TriggerAction action(const Value& v, std::string_view path) {
    const ron::Variant var = ron::asVariant(v, path);
    TriggerAction a;
    using K = TriggerAction::Kind;
    const std::string_view n = var.name;
    auto str = [&](const Value& s, std::string_view f) { return ron::asString(ron::require(s, f, path), path); };
    if (n == "Message") {
        a.kind = K::Message;
        a.text = ron::asString(element(var, 0, 1, path), path);
    } else if (n == "Say") {
        a.kind = K::Say;
        const Value& s = structPayload(var, path);
        a.speaker = str(s, "speaker");
        a.text = str(s, "text");
    } else if (n == "SpawnSquad") {
        a.kind = K::SpawnSquad;
        const Value& s = structPayload(var, path);
        a.name = str(s, "unit");
        a.count = ron::asU32(ron::require(s, "count", path), path);
        a.x = fieldF32(s, "x", path);
        a.y = fieldF32(s, "y", path);
        a.flag = ron::asBool(ron::require(s, "aggressive", path), path);
    } else if (n == "SpawnPlayerSquad") {
        a.kind = K::SpawnPlayerSquad;
        const Value& s = structPayload(var, path);
        a.name = str(s, "unit");
        a.count = ron::asU32(ron::require(s, "count", path), path);
        a.x = fieldF32(s, "x", path);
        a.y = fieldF32(s, "y", path);
    } else if (n == "SpawnPlayerBuilding") {
        a.kind = K::SpawnPlayerBuilding;
        const Value& s = structPayload(var, path);
        a.name = str(s, "building");
        a.x = fieldF32(s, "x", path);
        a.y = fieldF32(s, "y", path);
    } else if (n == "SpawnItem") {
        a.kind = K::SpawnItem;
        const Value& s = structPayload(var, path);
        a.name = str(s, "item");
        a.x = fieldF32(s, "x", path);
        a.y = fieldF32(s, "y", path);
    } else if (n == "AddObjective") {
        a.kind = K::AddObjective;
        const Value& s = structPayload(var, path);
        a.name = str(s, "name");
        a.text = str(s, "text");
        a.flag = ron::asBool(ron::require(s, "optional", path), path);
    } else if (n == "CompleteObjective") {
        a.kind = K::CompleteObjective;
        a.name = ron::asString(element(var, 0, 1, path), path);
    } else if (n == "FailObjective") {
        a.kind = K::FailObjective;
        a.name = ron::asString(element(var, 0, 1, path), path);
    } else if (n == "GrantEssence") {
        a.kind = K::GrantEssence;
        a.amount = ron::asF32(element(var, 0, 1, path), path);
    } else if (n == "GrantAnima") {
        a.kind = K::GrantAnima;
        a.amount = ron::asF32(element(var, 0, 1, path), path);
    } else if (n == "GrantHeroXp") {
        a.kind = K::GrantHeroXp;
        a.amount = ron::asF32(element(var, 0, 1, path), path);
    } else if (n == "Victory" && !var.payload) {
        a.kind = K::Victory;
    } else if (n == "Defeat" && !var.payload) {
        a.kind = K::Defeat;
    } else {
        unknownVariant(path, n);
    }
    return a;
}

std::vector<ObjectiveDef> objectives(const Value& s, std::string_view path) {
    std::vector<ObjectiveDef> out;
    const Value* v = opt(s, "objectives", path);
    if (!v) return out;
    const std::string p = ron::join(path, "objectives");
    for (const Value& item : ron::asList(*v, p)) {
        out.push_back({ron::asString(ron::require(item, "name", p), p), ron::asString(ron::require(item, "text", p), p),
                       boolOr(item, "optional", p, false)});
    }
    return out;
}

std::vector<TriggerDef> triggers(const Value& s, std::string_view path) {
    std::vector<TriggerDef> out;
    const Value* v = opt(s, "triggers", path);
    if (!v) return out;
    const std::string p = ron::join(path, "triggers");
    for (const Value& item : ron::asList(*v, p)) {
        TriggerDef t;
        t.name = ron::asString(ron::require(item, "name", p), p);
        t.once = boolOr(item, "once", p, true);
        const std::string tp = p + "[" + t.name + "]";
        for (const Value& c : ron::asList(ron::require(item, "when", tp), ron::join(tp, "when"))) {
            t.when.push_back(cond(c, ron::join(tp, "when")));
        }
        for (const Value& a : ron::asList(ron::require(item, "actions", tp), ron::join(tp, "actions"))) {
            t.actions.push_back(action(a, ron::join(tp, "actions")));
        }
        out.push_back(std::move(t));
    }
    return out;
}

MissionDef missionDef(const Value& s, std::string_view path) {
    MissionDef d;
    d.name = ron::asString(ron::require(s, "name", path), ron::join(path, "name"));
    d.briefing = strOr(s, "briefing", path);
    d.night = boolOr(s, "night", path, false);
    if (const Value* m = opt(s, "map", path)) {
        if (const Value* inner = ron::asOption(*m, ron::join(path, "map"))) d.map = mapSpec(*inner, ron::join(path, "map"));
    }
    d.rotate180 = boolOr(s, "rotate180", path, false);
    d.dayCycle = boolOr(s, "day_cycle", path, false);
    d.rainy = boolOr(s, "rainy", path, false);
    if (const Value* sp = opt(s, "spawns", path)) {
        if (const Value* inner = ron::asOption(*sp, ron::join(path, "spawns"))) {
            d.spawns = spawnSpec(*inner, ron::join(path, "spawns"));
        }
    }
    d.biome = strOr(s, "biome", path);
    d.objectives = objectives(s, path);
    d.triggers = triggers(s, path);
    d.intro = scenes(s, "intro", path);
    d.outro = scenes(s, "outro", path);
    return d;
}

// ---- trigger evaluation ------------------------------------------------------------

// World facts every condition reads, snapshotted once per tick before any
// action runs - except objective state and the outcome, which are live.
struct Facts {
    float missionSecs = 0.0f;
    float totalExtracted = 0.0f;
    float essence = 0.0f;
    float anima = 0.0f;
    std::optional<uint8_t> heroLevel;
    std::array<uint8_t, 4> heroRanks{};
    std::optional<Vec2> heroPos;
    uint32_t enemyUnits = 0;
    uint32_t playerUnits = 0;
    uint32_t playerBuildings = 0;
    std::map<uint16_t, uint32_t> playerUnitsByKind;
    std::map<uint16_t, uint32_t> playerBuildingsByKind;
    uint32_t armyWill = 0;
    uint32_t itemsOnGround = 0;
    bool reviving = false;
};

float secondsSince(uint64_t tick, uint64_t since) {
    return static_cast<float>(tick > since ? tick - since : 0) / kSimHzF; // saturating_sub
}

Facts gatherFacts(const World& w, uint64_t startTick) {
    const Catalogs& c = w.cat();
    Facts f;
    f.missionSecs = secondsSince(w.tick, startTick);
    // `iter().sum()`, left to right
    f.totalExtracted = -0.0f;
    for (float v : w.affinity.cumulative) f.totalExtracted += v;
    f.essence = w.economy.essence;
    f.anima = w.economy.anima;

    f.reviving = w.heroState.kind == HeroState::Kind::Reviving;
    if (w.heroState.kind == HeroState::Kind::Alive) {
        if (const Entity* e = w.indexed(w.heroState.id)) {
            if (e->hero) {
                f.heroLevel = e->hero->level;
                f.heroRanks = e->hero->ranks;
            }
            f.heroPos = e->pos.cur;
        }
    }

    for (const auto& [id, e] : w.entities) {
        if (e.isUnit()) {
            if (e.health.cur <= 0.0f) continue;
            if (e.team == 0) {
                ++f.playerUnits;
                ++f.playerUnitsByKind[e.unitKind];
                const UnitDef& def = c.units.def(e.unitKind);
                if (!def.worker) f.armyWill += def.will;
            } else {
                ++f.enemyUnits;
            }
        } else if (e.isBuilding()) {
            if (e.team != 0) continue;
            ++f.playerBuildings;
            if (e.building.complete()) ++f.playerBuildingsByKind[e.building.kind];
        } else if (e.isItem()) {
            ++f.itemsOnGround;
        }
    }
    return f;
}

bool condHolds(const TriggerCond& cond, const Facts& facts, const std::vector<ObjectiveState>& objs, uint64_t tick,
               const Catalogs& c) {
    using K = TriggerCond::Kind;
    auto count = [](const std::map<uint16_t, uint32_t>& m, uint16_t k) {
        auto it = m.find(k);
        return it == m.end() ? 0u : it->second;
    };
    switch (cond.kind) {
    case K::TimeAtLeast: return facts.missionSecs >= cond.amount;
    case K::TotalExtractedAtLeast: return facts.totalExtracted >= cond.amount;
    case K::EssenceAtLeast: return facts.essence >= cond.amount;
    case K::AnimaAtLeast: return facts.anima >= cond.amount;
    case K::HeroLevelAtLeast: return facts.heroLevel && *facts.heroLevel >= cond.count;
    case K::HeroRankAtLeast: return cond.slot < 4 && facts.heroRanks[cond.slot] >= cond.rank;
    case K::HeroInArea: return facts.heroPos && facts.heroPos->distance(Vec2(cond.x, cond.y)) <= cond.radius;
    case K::PlayerUnitsOfAtLeast: return count(facts.playerUnitsByKind, c.units.id(cond.name)) >= cond.count;
    case K::PlayerBuildingsOfAtLeast:
        return count(facts.playerBuildingsByKind, c.buildings.id(cond.name)) >= cond.count;
    case K::ArmyWillAtLeast: return facts.armyWill >= cond.count;
    case K::EnemyUnitsAtMost: return facts.enemyUnits <= cond.count;
    case K::PlayerWiped: return facts.playerUnits == 0 && facts.playerBuildings == 0 && !facts.reviving;
    case K::ItemsOnGroundAtMost: return facts.itemsOnGround <= cond.count;
    case K::ObjectiveComplete:
        return std::any_of(objs.begin(), objs.end(), [&](const ObjectiveState& o) {
            return o.name == cond.name && o.status == ObjectiveStatus::Complete;
        });
    case K::ObjectiveActive:
        return std::any_of(objs.begin(), objs.end(), [&](const ObjectiveState& o) {
            return o.name == cond.name && o.status == ObjectiveStatus::Active;
        });
    case K::ObjectiveActiveForAtLeast:
        return std::any_of(objs.begin(), objs.end(), [&](const ObjectiveState& o) {
            return o.name == cond.name && o.status == ObjectiveStatus::Active &&
                   secondsSince(tick, o.sinceTick) >= cond.amount;
        });
    case K::AllRequiredObjectivesComplete: {
        bool anyRequired = false;
        for (const ObjectiveState& o : objs) {
            if (o.optional) continue;
            anyRequired = true;
            if (o.status != ObjectiveStatus::Complete) return false;
        }
        return anyRequired;
    }
    }
    return false;
}

// Where aggressive scripted squads converge: the player's oldest standing
// building, else the oldest player unit; nothing if the player has nothing.
std::pair<std::optional<uint32_t>, Vec2> raidTarget(const World& w) {
    for (const auto& [id, e] : w.entities) {
        if (e.isBuilding() && e.team == 0) return {id, e.pos.cur};
    }
    for (const auto& [id, e] : w.entities) {
        if (e.isUnit() && e.team == 0) return {id, e.pos.cur};
    }
    return {std::nullopt, kZero};
}

uint32_t scaledEnemyCount(const World& w, uint32_t base) {
    const float mult = w.cat().difficulty.mults(w.difficulty).enemyCount;
    return std::max(asU32(std::round(static_cast<float>(base) * mult)), 1u);
}

void pushMissionFx(World& w, MissionFx::Kind kind, std::string text, std::string speaker = {}, bool victory = false) {
    MissionFx fx;
    fx.kind = kind;
    fx.text = std::move(text);
    fx.speaker = std::move(speaker);
    fx.victory = victory;
    w.events.mission.push_back(std::move(fx));
}

void setObjectiveStatus(World& w, const std::string& name, ObjectiveStatus status) {
    if (!w.mission) return;
    auto it = std::find_if(w.mission->objectives.begin(), w.mission->objectives.end(), [&](const ObjectiveState& o) {
        return o.name == name && o.status == ObjectiveStatus::Active;
    });
    if (it == w.mission->objectives.end()) return; // unknown or already resolved
    it->status = status;
    it->sinceTick = w.tick;
    if (status == ObjectiveStatus::Complete) pushMissionFx(w, MissionFx::Kind::ObjectiveComplete, it->text);
    if (status == ObjectiveStatus::Failed) pushMissionFx(w, MissionFx::Kind::ObjectiveFailed, it->text);
}

void setOutcome(World& w, MissionOutcome outcome) {
    if (!w.mission) return;
    if (w.mission->outcome != MissionOutcome::Playing) return; // first outcome wins
    w.mission->outcome = outcome;
    pushMissionFx(w, MissionFx::Kind::Outcome, {}, {}, outcome == MissionOutcome::Victory);
}

void applyAction(World& w, const TriggerAction& a) {
    const Catalogs& c = w.cat();
    using K = TriggerAction::Kind;
    switch (a.kind) {
    case K::Message: pushMissionFx(w, MissionFx::Kind::Message, a.text); break;
    case K::Say: pushMissionFx(w, MissionFx::Kind::Say, a.text, a.speaker); break;
    case K::SpawnSquad: {
        const uint16_t kind = c.units.id(a.name);
        const uint32_t n = scaledEnemyCount(w, a.count);
        const std::vector<uint32_t> ids = spawnSquad(w, 1, Vec2(a.x, a.y), {{kind, n}});
        if (a.flag) {
            const auto [raidId, raidPos] = raidTarget(w);
            const uint32_t goal = w.grid.nearestOpen(w.grid.cellAt(raidPos));
            const Vec2 target = w.grid.cellCenter(goal);
            for (uint32_t id : ids) {
                Entity* e = w.indexed(id);
                if (!e) continue;
                // remember WHICH objective, so the raider re-targets when it is
                // destroyed rather than idling on the rubble
                e->raider = raidId.value_or(UINT32_MAX);
                e->orders.push_back(Order::point(true, goal, target));
            }
        }
        break;
    }
    case K::SpawnPlayerSquad:
        spawnSquad(w, 0, Vec2(a.x, a.y), {{c.units.id(a.name), a.count}});
        break;
    case K::SpawnPlayerBuilding: spawnBuilding(w, c.buildings.id(a.name), 0, Vec2(a.x, a.y), true); break;
    case K::SpawnItem: spawnItem(w, c.items.id(a.name), Vec2(a.x, a.y)); break;
    case K::AddObjective: {
        if (!w.mission) return;
        auto& objs = w.mission->objectives;
        if (std::any_of(objs.begin(), objs.end(), [&](const ObjectiveState& o) { return o.name == a.name; })) return;
        objs.push_back({a.name, a.text, a.flag, ObjectiveStatus::Active, w.tick});
        pushMissionFx(w, MissionFx::Kind::ObjectiveAdded, a.text);
        break;
    }
    case K::CompleteObjective: setObjectiveStatus(w, a.name, ObjectiveStatus::Complete); break;
    case K::FailObjective: setObjectiveStatus(w, a.name, ObjectiveStatus::Failed); break;
    case K::GrantEssence: w.economy.essence += a.amount; break;
    case K::GrantAnima: w.economy.anima += a.amount; break;
    case K::GrantHeroXp: grantHeroXp(w, a.amount); break;
    case K::Victory: setOutcome(w, MissionOutcome::Victory); break;
    case K::Defeat: setOutcome(w, MissionOutcome::Defeat); break;
    }
}

void rotateDef180(MissionDef& def) {
    if (def.map) rotateMapSpec180(*def.map);
    if (def.spawns) rotateSpawnSpec180(*def.spawns);
    rotateTriggers180(def.triggers);
    rotateScenes180(def.intro);
    rotateScenes180(def.outro);
}

} // namespace

// ---- loading -------------------------------------------------------------------------

MissionDef loadMissionDef(const std::filesystem::path& root, std::string_view name) {
    const auto dir = root / "missions";
    const std::string base(name);
    const auto main = dir / (base + ".ron");
    if (!std::filesystem::exists(main)) throw ron::Error("failed to read " + main.generic_string());
    MissionDef def = missionDef(ron::parseFile(main), main.generic_string());

    if (const auto p = dir / (base + ".map.ron"); std::filesystem::exists(p)) {
        def.map = mapSpec(ron::parseFile(p), p.generic_string());
    }
    if (const auto p = dir / (base + ".spawns.ron"); std::filesystem::exists(p)) {
        def.spawns = spawnSpec(ron::parseFile(p), p.generic_string());
    }
    if (const auto p = dir / (base + ".triggers.ron"); std::filesystem::exists(p)) {
        const Value doc = ron::parseFile(p);
        const std::string path = p.generic_string();
        ron::asStruct(doc, path);
        def.objectives = objectives(doc, path);
        def.triggers = triggers(doc, path);
    }
    if (const auto p = dir / (base + ".scenes.ron"); std::filesystem::exists(p)) {
        const Value doc = ron::parseFile(p);
        const std::string path = p.generic_string();
        ron::asStruct(doc, path);
        def.intro = scenes(doc, "intro", path);
        def.outro = scenes(doc, "outro", path);
    }
    return def;
}

MissionDef parseMissionDefFile(const std::filesystem::path& path) {
    return missionDef(ron::parseFile(path), path.generic_string());
}

MapDef mapDefFromSpec(const MapSpec& spec) {
    auto rects = [](const std::vector<std::array<float, 4>>& list) {
        std::vector<Rect2> out;
        for (const auto& r : list) out.push_back(Rect2::make(r[0], r[1], r[2], r[3]));
        return out;
    };
    MapDef m;
    m.half = spec.half;
    m.obstacles = rects(spec.obstacles);
    m.roads = rects(spec.roads);
    for (const auto& [r, level] : spec.plateaus) m.plateaus.emplace_back(Rect2::make(r[0], r[1], r[2], r[3]), level);
    for (const auto& [verts, level] : spec.plateauPolys) {
        std::vector<Vec2> v;
        for (const auto& [x, y] : verts) v.emplace_back(x, y);
        m.plateauPolys.emplace_back(std::move(v), level);
    }
    m.ramps = rects(spec.ramps);
    m.bumps = spec.bumps;
    return m;
}

void rotateMapSpec180(MapSpec& m) {
    auto rect = [](std::array<float, 4>& r) { r = {-r[2], -r[3], -r[0], -r[1]}; };
    for (auto& r : m.obstacles) rect(r);
    for (auto& r : m.roads) rect(r);
    for (auto& [r, level] : m.plateaus) rect(r);
    for (auto& [verts, level] : m.plateauPolys) {
        for (auto& v : verts) v = {-v.first, -v.second};
    }
    for (auto& r : m.ramps) rect(r);
    for (auto& b : m.bumps) {
        b[0] = -b[0];
        b[1] = -b[1];
    }
}

void rotateSpawnSpec180(SpawnSpec& s) {
    if (s.hero) *s.hero = {-s.hero->first, -s.hero->second};
    for (auto& b : s.buildings) { b.x = -b.x; b.y = -b.y; }
    for (auto& u : s.units) { u.x = -u.x; u.y = -u.y; }
    for (auto& e : s.enemies) { e.x = -e.x; e.y = -e.y; }
    for (auto& src : s.sources) { src.x = -src.x; src.y = -src.y; }
    for (auto& it : s.items) { it.x = -it.x; it.y = -it.y; }
}

void rotateTriggers180(std::vector<TriggerDef>& triggers) {
    for (TriggerDef& t : triggers) {
        for (TriggerCond& c : t.when) {
            if (c.kind == TriggerCond::Kind::HeroInArea) {
                c.x = -c.x;
                c.y = -c.y;
            }
        }
        for (TriggerAction& a : t.actions) {
            switch (a.kind) {
            case TriggerAction::Kind::SpawnSquad:
            case TriggerAction::Kind::SpawnPlayerSquad:
            case TriggerAction::Kind::SpawnPlayerBuilding:
            case TriggerAction::Kind::SpawnItem:
                a.x = -a.x;
                a.y = -a.y;
                break;
            default: break;
            }
        }
    }
}

void rotateScenes180(std::vector<SceneStep>& steps) {
    for (SceneStep& s : steps) {
        if (s.camera) {
            (*s.camera)[0] = -(*s.camera)[0];
            (*s.camera)[1] = -(*s.camera)[1];
        }
        for (auto& a : s.actors) a = {-a.first, -a.second};
    }
}

std::optional<std::string> installMission(World& w, MissionDef def) {
    if (def.rotate180) rotateDef180(def);
    const Catalogs& c = w.cat();

    // Every name resolved BEFORE the world is touched: an unknown name is a
    // load-time error and the world is left alone.
    auto unknown = [&](const char* what, const std::string& name) {
        return std::optional<std::string>("mission " + def.name + ": unknown " + what + " \"" + name + "\"");
    };
    for (const TriggerDef& t : def.triggers) {
        for (const TriggerAction& a : t.actions) {
            switch (a.kind) {
            case TriggerAction::Kind::SpawnSquad:
            case TriggerAction::Kind::SpawnPlayerSquad:
                if (!c.units.tryId(a.name)) return unknown("unit", a.name);
                break;
            case TriggerAction::Kind::SpawnPlayerBuilding:
                if (!c.buildings.tryId(a.name)) return unknown("building", a.name);
                break;
            case TriggerAction::Kind::SpawnItem:
                if (!c.items.tryId(a.name)) return unknown("item", a.name);
                break;
            default: break;
            }
        }
        for (const TriggerCond& cond : t.when) {
            if (cond.kind == TriggerCond::Kind::PlayerUnitsOfAtLeast && !c.units.tryId(cond.name)) {
                return unknown("unit", cond.name);
            }
            if (cond.kind == TriggerCond::Kind::PlayerBuildingsOfAtLeast && !c.buildings.tryId(cond.name)) {
                return unknown("building", cond.name);
            }
        }
    }
    if (def.spawns) {
        for (const auto& s : def.spawns->sources) {
            if (!c.sources.tryId(s.name)) return unknown("source", s.name);
        }
        for (const auto& b : def.spawns->buildings) {
            if (!c.buildings.tryId(b.name)) return unknown("building", b.name);
        }
        for (const auto& u : def.spawns->units) {
            if (!c.units.tryId(u.name)) return unknown("unit", u.name);
        }
        for (const auto& e : def.spawns->enemies) {
            if (!c.units.tryId(e.name)) return unknown("unit", e.name);
        }
        for (const auto& i : def.spawns->items) {
            if (!c.items.tryId(i.name)) return unknown("item", i.name);
        }
    }

    // ---- validation done; from here the world is mutated ----

    if (def.map) {
        if (w.idAlloc != 0) throw std::logic_error(def.name + ": a mission map needs a fresh sim world");
        w.map = mapDefFromSpec(*def.map);
        w.grid = NavGrid::fromDef(w.map);
        w.flowFields.clear();
    }

    // initial spawns in a fixed order, so SimIds are deterministic
    if (def.spawns) {
        const SpawnSpec spawns = *def.spawns;
        for (const auto& s : spawns.sources) spawnSource(w, c.sources.id(s.name), Vec2(s.x, s.y));
        for (const auto& b : spawns.buildings) spawnBuilding(w, c.buildings.id(b.name), 0, Vec2(b.x, b.y), true);
        if (spawns.hero) spawnHero(w, Vec2(spawns.hero->first, spawns.hero->second));
        for (const auto& u : spawns.units) spawnSquad(w, 0, Vec2(u.x, u.y), {{c.units.id(u.name), u.count}});
        for (const auto& e : spawns.enemies) {
            const uint32_t n = scaledEnemyCount(w, e.count);
            spawnSquad(w, 1, Vec2(e.x, e.y), {{c.units.id(e.name), n}});
        }
        for (const auto& i : spawns.items) spawnItem(w, c.items.id(i.name), Vec2(i.x, i.y));
        w.economy.essence += spawns.startEssence;
    }

    MissionRuntime rt;
    rt.startTick = w.tick;
    for (const ObjectiveDef& o : def.objectives) {
        rt.objectives.push_back({o.name, o.text, o.optional, ObjectiveStatus::Active, rt.startTick});
    }
    rt.fired.assign(def.triggers.size(), false);
    rt.def = std::move(def);
    w.mission = std::move(rt);
    return std::nullopt;
}

std::optional<std::string> loadMission(World& w, std::string_view name) {
    MissionDef def;
    try {
        def = loadMissionDef(w.cat().root, name);
    } catch (const ron::Error& e) {
        return std::string(e.what());
    }
    return installMission(w, std::move(def));
}

MissionOutcome missionOutcome(const World& w) {
    return w.mission ? w.mission->outcome : MissionOutcome::Playing;
}

// ---- the two exclusive systems ------------------------------------------------------------

void retargetRaiders(World& w) {
    // Idle raiders whose objective is gone - destroyed, or never assigned. A
    // live objective that has merely moved is left to the leash.
    std::vector<uint32_t> stale;
    for (const auto& [id, e] : w.entities) {
        if (!e.isUnit() || !e.raider || !e.orders.empty()) continue;
        const uint32_t tid = *e.raider;
        if (tid == UINT32_MAX || !w.indexed(tid)) stale.push_back(id);
    }
    if (stale.empty()) return;

    const auto [raidId, raidPos] = raidTarget(w);
    if (!raidId) {
        // Nothing left to raid. Mark them objective-less and issue nothing; a
        // march to the origin would only churn against crowd arrival.
        for (uint32_t id : stale) w.get(id)->raider = UINT32_MAX;
        return;
    }
    const uint32_t goal = w.grid.nearestOpen(w.grid.cellAt(raidPos));
    const Vec2 target = w.grid.cellCenter(goal);
    for (uint32_t id : stale) {
        Entity* e = w.get(id);
        e->raider = *raidId;
        e->orders.push_back(Order::point(true, goal, target));
    }
}

void runTriggers(World& w) {
    // bespoke per-mission code first
    std::vector<std::function<void(World&)>> hooks;
    hooks.swap(w.missionHooks);
    for (auto& hook : hooks) hook(w);
    w.missionHooks = std::move(hooks);

    if (missionOutcome(w) != MissionOutcome::Playing) return;
    if (!w.mission) return;
    const std::vector<TriggerDef> triggers = w.mission->def.triggers;
    const Facts facts = gatherFacts(w, w.mission->startTick);
    const uint64_t tick = w.tick;

    for (size_t i = 0; i < triggers.size(); ++i) {
        // outcome decided mid-pass: stop, the first outcome wins
        if (missionOutcome(w) != MissionOutcome::Playing) return;
        const TriggerDef& t = triggers[i];
        if (t.once && w.mission->fired[i]) continue;
        // objective conditions read live state; everything else the snapshot
        const bool all = std::all_of(t.when.begin(), t.when.end(), [&](const TriggerCond& cond) {
            return condHolds(cond, facts, w.mission->objectives, tick, w.cat());
        });
        if (!all) continue;
        w.mission->fired[i] = true;
        for (const TriggerAction& a : t.actions) applyAction(w, a);
    }
}

// ---- the oracle's dump ----------------------------------------------------------------------

namespace {

std::string rectsText(const std::vector<std::array<float, 4>>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ';';
        out += bitsHex(v[i][0]) + "," + bitsHex(v[i][1]) + "," + bitsHex(v[i][2]) + "," + bitsHex(v[i][3]);
    }
    return out;
}

std::string condText(const TriggerCond& c) {
    using K = TriggerCond::Kind;
    switch (c.kind) {
    case K::TimeAtLeast: return "time_at_least " + bitsHex(c.amount);
    case K::TotalExtractedAtLeast: return "total_extracted_at_least " + bitsHex(c.amount);
    case K::EssenceAtLeast: return "essence_at_least " + bitsHex(c.amount);
    case K::AnimaAtLeast: return "anima_at_least " + bitsHex(c.amount);
    case K::HeroLevelAtLeast: return "hero_level_at_least " + std::to_string(c.count);
    case K::HeroRankAtLeast:
        return "hero_rank_at_least " + std::to_string(c.slot) + " " + std::to_string(c.rank);
    case K::HeroInArea: return "hero_in_area " + bitsHex(c.x) + "," + bitsHex(c.y) + "," + bitsHex(c.radius);
    case K::PlayerUnitsOfAtLeast: return "player_units_of_at_least \"" + c.name + "\" " + std::to_string(c.count);
    case K::PlayerBuildingsOfAtLeast:
        return "player_buildings_of_at_least \"" + c.name + "\" " + std::to_string(c.count);
    case K::ArmyWillAtLeast: return "army_will_at_least " + std::to_string(c.count);
    case K::EnemyUnitsAtMost: return "enemy_units_at_most " + std::to_string(c.count);
    case K::PlayerWiped: return "player_wiped";
    case K::ItemsOnGroundAtMost: return "items_on_ground_at_most " + std::to_string(c.count);
    case K::ObjectiveComplete: return "objective_complete \"" + c.name + "\"";
    case K::ObjectiveActive: return "objective_active \"" + c.name + "\"";
    case K::ObjectiveActiveForAtLeast:
        return "objective_active_for_at_least \"" + c.name + "\" " + bitsHex(c.amount);
    case K::AllRequiredObjectivesComplete: return "all_required_objectives_complete";
    }
    return "?";
}

std::string actionText(const TriggerAction& a) {
    using K = TriggerAction::Kind;
    const std::string at = bitsHex(a.x) + "," + bitsHex(a.y);
    switch (a.kind) {
    case K::Message: return "message " + textHash(a.text);
    case K::Say: return "say \"" + a.speaker + "\" " + textHash(a.text);
    case K::SpawnSquad:
        return "spawn_squad \"" + a.name + "\" " + std::to_string(a.count) + " " + at + " " + (a.flag ? "1" : "0");
    case K::SpawnPlayerSquad: return "spawn_player_squad \"" + a.name + "\" " + std::to_string(a.count) + " " + at;
    case K::SpawnPlayerBuilding: return "spawn_player_building \"" + a.name + "\" " + at;
    case K::SpawnItem: return "spawn_item \"" + a.name + "\" " + at;
    case K::AddObjective:
        return "add_objective \"" + a.name + "\" " + textHash(a.text) + " " + (a.flag ? "1" : "0");
    case K::CompleteObjective: return "complete_objective \"" + a.name + "\"";
    case K::FailObjective: return "fail_objective \"" + a.name + "\"";
    case K::GrantEssence: return "grant_essence " + bitsHex(a.amount);
    case K::GrantAnima: return "grant_anima " + bitsHex(a.amount);
    case K::GrantHeroXp: return "grant_hero_xp " + bitsHex(a.amount);
    case K::Victory: return "victory";
    case K::Defeat: return "defeat";
    }
    return "?";
}

void sceneText(std::ostringstream& o, const char* tag, const std::vector<SceneStep>& steps) {
    for (size_t i = 0; i < steps.size(); ++i) {
        const SceneStep& s = steps[i];
        o << "  " << tag << ' ' << i << " at=" << bitsHex(s.at) << " subtitle=" << textHash(s.subtitle)
          << " speaker=" << textHash(s.speaker) << " camera=";
        if (s.camera) {
            o << bitsHex((*s.camera)[0]) << ',' << bitsHex((*s.camera)[1]) << ',' << bitsHex((*s.camera)[2]);
        } else {
            o << "none";
        }
        o << " actors=";
        for (size_t a = 0; a < s.actors.size(); ++a) {
            o << (a ? ";" : "") << bitsHex(s.actors[a].first) << ',' << bitsHex(s.actors[a].second);
        }
        o << " storm=" << bitsHex(s.storm) << " flash=" << bitsHex(s.flash) << '\n';
    }
}

} // namespace

std::string dumpMission(const MissionDef& def, std::string_view name) {
    std::ostringstream o;
    o << "mission " << name << " name=" << textHash(def.name) << " briefing=" << textHash(def.briefing)
      << " night=" << (def.night ? 1 : 0) << " rotate180=" << (def.rotate180 ? 1 : 0)
      << " day_cycle=" << (def.dayCycle ? 1 : 0) << " rainy=" << (def.rainy ? 1 : 0)
      << " biome=" << textHash(def.biome) << '\n';
    if (!def.map) {
        o << "  map none\n";
    } else {
        const MapSpec& m = *def.map;
        o << "  map half=" << bitsHex(m.half) << '\n';
        o << "  obstacles " << rectsText(m.obstacles) << '\n';
        o << "  roads " << rectsText(m.roads) << '\n';
        o << "  plateaus ";
        for (size_t i = 0; i < m.plateaus.size(); ++i) {
            const auto& [r, level] = m.plateaus[i];
            o << (i ? ";" : "") << bitsHex(r[0]) << ',' << bitsHex(r[1]) << ',' << bitsHex(r[2]) << ','
              << bitsHex(r[3]) << ',' << static_cast<int>(level);
        }
        o << '\n';
        for (const auto& [verts, level] : m.plateauPolys) {
            o << "  poly " << static_cast<int>(level) << ' ';
            for (size_t i = 0; i < verts.size(); ++i) {
                o << (i ? ";" : "") << bitsHex(verts[i].first) << ',' << bitsHex(verts[i].second);
            }
            o << '\n';
        }
        o << "  ramps " << rectsText(m.ramps) << '\n';
        o << "  bumps " << rectsText(m.bumps) << '\n';
    }
    if (!def.spawns) {
        o << "  spawns none\n";
    } else {
        const SpawnSpec& s = *def.spawns;
        o << "  spawns hero=";
        if (s.hero) {
            o << bitsHex(s.hero->first) << ',' << bitsHex(s.hero->second);
        } else {
            o << "none";
        }
        o << " start_essence=" << bitsHex(s.startEssence) << '\n';
        for (const auto& b : s.buildings) o << "  building \"" << b.name << "\" " << bitsHex(b.x) << ',' << bitsHex(b.y) << '\n';
        for (const auto& u : s.units) {
            o << "  unit \"" << u.name << "\" " << u.count << ' ' << bitsHex(u.x) << ',' << bitsHex(u.y) << '\n';
        }
        for (const auto& e : s.enemies) {
            o << "  enemy \"" << e.name << "\" " << e.count << ' ' << bitsHex(e.x) << ',' << bitsHex(e.y) << '\n';
        }
        for (const auto& src : s.sources) o << "  source \"" << src.name << "\" " << bitsHex(src.x) << ',' << bitsHex(src.y) << '\n';
        for (const auto& it : s.items) o << "  item \"" << it.name << "\" " << bitsHex(it.x) << ',' << bitsHex(it.y) << '\n';
    }
    for (const ObjectiveDef& obj : def.objectives) {
        o << "  objective \"" << obj.name << "\" text=" << textHash(obj.text) << " optional=" << (obj.optional ? 1 : 0)
          << '\n';
    }
    for (const TriggerDef& t : def.triggers) {
        o << "  trigger \"" << t.name << "\" once=" << (t.once ? 1 : 0) << '\n';
        for (const TriggerCond& c : t.when) o << "    when " << condText(c) << '\n';
        for (const TriggerAction& a : t.actions) o << "    do " << actionText(a) << '\n';
    }
    sceneText(o, "intro", def.intro);
    sceneText(o, "outro", def.outro);
    return o.str();
}

} // namespace husk
