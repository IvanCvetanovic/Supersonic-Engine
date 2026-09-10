#include "Data.hpp"

#include "Fnv.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <sstream>

namespace husk {

namespace {

using ron::Value;

float f32(const Value& s, std::string_view name, std::string_view path) {
    return ron::asF32(ron::require(s, name, path), ron::join(path, name));
}

// A `#[serde(default)]` float.
float f32Or(const Value& s, std::string_view name, std::string_view path, float fallback) {
    const Value* v = ron::asStruct(s, path).field(name);
    return v ? ron::asF32(*v, ron::join(path, name)) : fallback;
}

uint32_t u32(const Value& s, std::string_view name, std::string_view path) {
    return ron::asU32(ron::require(s, name, path), ron::join(path, name));
}

const std::string& str(const Value& s, std::string_view name, std::string_view path) {
    return ron::asString(ron::require(s, name, path), ron::join(path, name));
}

std::string strOr(const Value& s, std::string_view name, std::string_view path) {
    const Value* v = ron::asStruct(s, path).field(name);
    return v ? ron::asString(*v, ron::join(path, name)) : std::string();
}

bool boolOr(const Value& s, std::string_view name, std::string_view path) {
    const Value* v = ron::asStruct(s, path).field(name);
    return v ? ron::asBool(*v, ron::join(path, name)) : false;
}

// serde gives a missing Option field None whether or not it says
// #[serde(default)], so every Option below is optional on disk.
const Value* option(const Value& s, std::string_view name, std::string_view path) {
    const Value* v = ron::asStruct(s, path).field(name);
    return v ? ron::asOption(*v, ron::join(path, name)) : nullptr;
}

std::vector<std::string> strings(const Value& s, std::string_view name, std::string_view path) {
    std::vector<std::string> out;
    const Value* v = ron::asStruct(s, path).field(name);
    if (!v) return out;
    const std::string p = ron::join(path, name);
    for (const Value& item : ron::asList(*v, p)) out.push_back(ron::asString(item, p));
    return out;
}

template <size_t N>
std::array<float, N> floatTuple(const Value& s, std::string_view name, std::string_view path) {
    const std::string p = ron::join(path, name);
    const auto& items = ron::asTuple(ron::require(s, name, path), N, p);
    std::array<float, N> out{};
    for (size_t i = 0; i < N; ++i) out[i] = ron::asF32(items[i], p);
    return out;
}

std::array<uint32_t, 2> footprint(const Value& s, std::string_view path) {
    const std::string p = ron::join(path, "footprint");
    const auto& items = ron::asTuple(ron::require(s, "footprint", path), 2, p);
    return {ron::asU32(items[0], p), ron::asU32(items[1], p)};
}

[[noreturn]] void unknownVariant(std::string_view path, std::string_view name) {
    throw ron::Error(std::string(path) + ": unknown variant `" + std::string(name) + "`");
}

AttackType attackType(const Value& v, std::string_view path) {
    const auto var = ron::asVariant(v, path);
    if (var.payload) unknownVariant(path, var.name);
    if (var.name == "Normal") return AttackType::Normal;
    if (var.name == "Pierce") return AttackType::Pierce;
    if (var.name == "Siege") return AttackType::Siege;
    if (var.name == "Arcane") return AttackType::Arcane;
    unknownVariant(path, var.name);
}

ArmorType armorType(const Value& v, std::string_view path) {
    const auto var = ron::asVariant(v, path);
    if (var.payload) unknownVariant(path, var.name);
    if (var.name == "Unarmored") return ArmorType::Unarmored;
    if (var.name == "Light") return ArmorType::Light;
    if (var.name == "Heavy") return ArmorType::Heavy;
    if (var.name == "Fortified") return ArmorType::Fortified;
    unknownVariant(path, var.name);
}

Affinity affinity(const Value& v, std::string_view path) {
    const auto var = ron::asVariant(v, path);
    if (var.payload) unknownVariant(path, var.name);
    if (var.name == "Steel") return Affinity::Steel;
    if (var.name == "Flora") return Affinity::Flora;
    if (var.name == "Volt") return Affinity::Volt;
    if (var.name == "Stone") return Affinity::Stone;
    if (var.name == "Pyre") return Affinity::Pyre;
    if (var.name == "Aqua") return Affinity::Aqua;
    unknownVariant(path, var.name);
}

std::optional<AttackDef> attack(const Value& s, std::string_view path) {
    const Value* v = option(s, "attack", path);
    if (!v) return std::nullopt;
    const std::string p = ron::join(path, "attack");
    AttackDef a;
    a.kind = attackType(ron::require(*v, "kind", p), ron::join(p, "kind"));
    a.damage = f32(*v, "damage", p);
    a.cooldown = f32(*v, "cooldown", p);
    a.range = f32(*v, "range", p);
    a.acquire = f32(*v, "acquire", p);
    a.ranged = ron::asBool(ron::require(*v, "ranged", p), ron::join(p, "ranged"));
    return a;
}

UnitDef unitDef(const Value& s, std::string_view path) {
    UnitDef d;
    d.name = str(s, "name", path);
    d.worker = boolOr(s, "worker", path);
    d.will = u32(s, "will", path);
    d.hp = f32(s, "hp", path);
    d.speed = f32(s, "speed", path);
    d.radius = f32(s, "radius", path);
    d.armor = armorType(ron::require(s, "armor", path), ron::join(path, "armor"));
    d.attack = attack(s, path);
    d.color = floatTuple<3>(s, "color", path);
    d.size = floatTuple<2>(s, "size", path);
    d.costEssence = f32(s, "cost_essence", path);
    d.costAnima = f32Or(s, "cost_anima", path, 0.0f);
    d.buildTime = f32(s, "build_time", path);
    if (const Value* req = option(s, "requires", path)) {
        const std::string p = ron::join(path, "requires");
        const auto& t = ron::asTuple(*req, 2, p);
        d.requirement = AffinityRequirement{affinity(t[0], p), ron::asU8(t[1], p)};
    }
    d.xpValue = f32Or(s, "xp_value", path, 0.0f);
    if (const Value* pv = option(s, "passive", path)) {
        const std::string p = ron::join(path, "passive");
        const auto var = ron::asVariant(*pv, p);
        if (!var.payload) unknownVariant(p, var.name);
        UnitPassive passive;
        if (var.name == "FloraRegen") {
            passive.kind = UnitPassive::Kind::FloraRegen;
            passive.rate = f32(*var.payload, "rate", p);
            passive.radius = f32(*var.payload, "radius", p);
        } else if (var.name == "Taunt") {
            passive.kind = UnitPassive::Kind::Taunt;
            passive.radius = f32(*var.payload, "radius", p);
        } else if (var.name == "ChainArc") {
            passive.kind = UnitPassive::Kind::ChainArc;
            passive.jumps = ron::asU8(ron::require(*var.payload, "jumps", p), ron::join(p, "jumps"));
            passive.radius = f32(*var.payload, "radius", p);
            passive.falloff = f32(*var.payload, "falloff", p);
        } else {
            unknownVariant(p, var.name);
        }
        d.passive = passive;
    }
    return d;
}

BuildingDef buildingDef(const Value& s, std::string_view path) {
    BuildingDef d;
    d.name = str(s, "name", path);
    d.hp = f32(s, "hp", path);
    d.armor = armorType(ron::require(s, "armor", path), ron::join(path, "armor"));
    d.costEssence = f32(s, "cost_essence", path);
    d.buildTime = f32(s, "build_time", path);
    d.willProvided = u32(s, "will_provided", path);
    d.footprint = footprint(s, path);
    d.size = floatTuple<2>(s, "size", path);
    d.color = floatTuple<3>(s, "color", path);
    d.produces = strings(s, "produces", path);
    d.researches = strings(s, "researches", path);
    d.attack = attack(s, path);
    return d;
}

SourceDef sourceDef(const Value& s, std::string_view path) {
    SourceDef d;
    d.name = str(s, "name", path);
    d.affinity = affinity(ron::require(s, "affinity", path), ron::join(path, "affinity"));
    d.essence = f32(s, "essence", path);
    d.anima = f32Or(s, "anima", path, 0.0f);
    d.footprint = footprint(s, path);
    d.size = floatTuple<2>(s, "size", path);
    return d;
}

HeroConfig heroConfig(const Value& s, std::string_view path) {
    HeroConfig h;
    h.unit = str(s, "unit", path);
    h.hpPerLevel = f32(s, "hp_per_level", path);
    h.damagePerLevel = f32(s, "damage_per_level", path);
    h.regenBase = f32(s, "regen_base", path);
    h.regenPerLevel = f32(s, "regen_per_level", path);
    {
        const std::string p = ron::join(path, "xp_curve");
        for (const Value& v : ron::asList(ron::require(s, "xp_curve", path), p)) {
            h.xpCurve.push_back(ron::asF32(v, p));
        }
    }
    h.xpRadius = f32(s, "xp_radius", path);
    ron::require(s, "abilities", path);
    h.abilities = strings(s, "abilities", path);
    h.reviveCostBase = f32(s, "revive_cost_base", path);
    h.reviveCostPerLevel = f32(s, "revive_cost_per_level", path);
    h.reviveTime = f32(s, "revive_time", path);
    return h;
}

AbilityKind abilityKind(const Value& v, std::string_view path) {
    const auto var = ron::asVariant(v, path);
    if (var.payload) unknownVariant(path, var.name);
    if (var.name == "TargetEnemyOrSource") return AbilityKind::TargetEnemyOrSource;
    if (var.name == "TargetFriendlyUnit") return AbilityKind::TargetFriendlyUnit;
    if (var.name == "NoTarget") return AbilityKind::NoTarget;
    unknownVariant(path, var.name);
}

AbilityDef abilityDef(const Value& s, std::string_view path) {
    AbilityDef d;
    d.name = str(s, "name", path);
    d.description = strOr(s, "description", path);
    d.kind = abilityKind(ron::require(s, "kind", path), ron::join(path, "kind"));
    d.castRange = f32(s, "cast_range", path);
    {
        const std::string p = ron::join(path, "max_level_gate");
        for (const Value& v : ron::asList(ron::require(s, "max_level_gate", path), p)) {
            d.maxLevelGate.push_back(ron::asU8(v, p));
        }
    }
    const std::string p = ron::join(path, "ranks");
    for (const Value& r : ron::asList(ron::require(s, "ranks", path), p)) {
        AbilityRank k;
        k.cooldown = f32(r, "cooldown", p);
        k.damage = f32Or(r, "damage", p, 0.0f);
        k.refund = f32Or(r, "refund", p, 0.0f);
        k.drain = f32Or(r, "drain", p, 0.0f);
        k.healFrac = f32Or(r, "heal_frac", p, 0.0f);
        k.atkSpeed = f32Or(r, "atk_speed", p, 0.0f);
        k.moveSpeed = f32Or(r, "move_speed", p, 0.0f);
        k.duration = f32Or(r, "duration", p, 0.0f);
        k.burn = f32Or(r, "burn", p, 0.0f);
        k.radius = f32Or(r, "radius", p, 0.0f);
        k.slow = f32Or(r, "slow", p, 0.0f);
        k.hpBonus = f32Or(r, "hp_bonus", p, 0.0f);
        k.dmgBonus = f32Or(r, "dmg_bonus", p, 0.0f);
        if (const Value* t = ron::asStruct(r, p).field("thralls")) k.thralls = ron::asU32(*t, ron::join(p, "thralls"));
        d.ranks.push_back(k);
    }
    return d;
}

ItemDef itemDef(const Value& s, std::string_view path) {
    ItemDef d;
    d.name = str(s, "name", path);
    d.hpAdd = f32Or(s, "hp_add", path, 0.0f);
    d.damageAdd = f32Or(s, "damage_add", path, 0.0f);
    d.regenAdd = f32Or(s, "regen_add", path, 0.0f);
    d.moveMultAdd = f32Or(s, "move_mult_add", path, 0.0f);
    if (const Value* a = option(s, "active", path)) {
        const std::string p = ron::join(path, "active");
        const auto var = ron::asVariant(*a, p);
        if (!var.payload) unknownVariant(p, var.name);
        ActiveEffect e;
        if (var.name == "Heal") {
            e.kind = ActiveEffect::Kind::Heal;
            e.amount = f32(*var.payload, "amount", p);
        } else if (var.name == "Haste") {
            e.kind = ActiveEffect::Kind::Haste;
            e.atkSpeed = f32(*var.payload, "atk_speed", p);
            e.moveSpeed = f32(*var.payload, "move_speed", p);
            e.secs = f32(*var.payload, "secs", p);
        } else {
            unknownVariant(p, var.name);
        }
        d.active = e;
    }
    return d;
}

UpgradeDef upgradeDef(const Value& s, std::string_view path) {
    UpgradeDef d;
    d.name = str(s, "name", path);
    d.maxLevel = ron::asU8(ron::require(s, "max_level", path), ron::join(path, "max_level"));
    d.costEssence = f32(s, "cost_essence", path);
    d.costAnima = f32Or(s, "cost_anima", path, 0.0f);
    d.time = f32(s, "time", path);
    const std::string p = ron::join(path, "effect");
    const auto var = ron::asVariant(ron::require(s, "effect", path), p);
    if (!var.payload) unknownVariant(p, var.name);
    if (var.name == "AttackSpeed") {
        d.effect.kind = UpgradeEffect::Kind::AttackSpeed;
    } else if (var.name == "GatherRate") {
        d.effect.kind = UpgradeEffect::Kind::GatherRate;
    } else {
        unknownVariant(p, var.name);
    }
    d.effect.pctPerLevel = f32(*var.payload, "pct_per_level", p);
    return d;
}

DifficultyMults difficultyMults(const Value& s, std::string_view name, std::string_view path) {
    const std::string p = ron::join(path, name);
    const Value& v = ron::require(s, name, path);
    return {f32(v, "enemy_hp", p), f32(v, "enemy_damage", p), f32(v, "enemy_count", p)};
}

// `ron_files`: every .ron in a directory, sorted by file name. OS directory
// order is arbitrary, and catalog ids must not be.
std::vector<std::filesystem::path> ronFiles(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() == ".ron") files.push_back(entry.path());
    }
    if (ec) throw ron::Error("failed to read " + dir.generic_string() + ": " + ec.message());
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.filename().generic_string() < b.filename().generic_string();
    });
    return files;
}

template <class Def, class Parse>
Catalog<Def> loadCatalog(const std::filesystem::path& dir, const char* what, Parse parse) {
    Catalog<Def> cat;
    for (const auto& file : ronFiles(dir)) {
        const std::string path = file.generic_string();
        Def def = parse(ron::parseFile(file), path);
        cat.byName[lowerAscii(def.name)] = static_cast<uint16_t>(cat.defs.size());
        cat.defs.push_back(std::move(def));
    }
    if (cat.defs.empty()) throw ron::Error(std::string("no ") + what + " definitions found");
    return cat;
}

void check(bool ok, const std::string& what) {
    if (!ok) throw ron::Error(what);
}

} // namespace

std::string lowerAscii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (static_cast<unsigned char>(c) >= 0x80) {
            throw ron::Error("catalog name \"" + std::string(s) +
                             "\" is not ASCII; its lower-case form would need Unicode case mapping");
        }
        out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

float HeroConfig::xpForLevel(uint8_t level) const {
    if (level <= 1) return 0.0f;
    const size_t i = static_cast<size_t>(level - 2);
    return i < xpCurve.size() ? xpCurve[i] : std::numeric_limits<float>::infinity();
}

float HeroConfig::reviveCost(uint8_t level) const {
    const uint8_t levelUps = level > 0 ? static_cast<uint8_t>(level - 1) : 0; // saturating_sub
    return reviveCostBase + reviveCostPerLevel * static_cast<float>(levelUps);
}

const std::vector<std::pair<uint16_t, float>>& LootTable::dropsFor(uint16_t kind) const {
    static const std::vector<std::pair<uint16_t, float>> kNone;
    return kind < drops.size() ? drops[kind] : kNone;
}

std::filesystem::path defaultDataRoot() {
    return std::filesystem::path(HUSK_DATA_DIR);
}

Catalogs loadCatalogs(const std::filesystem::path& root) {
    Catalogs c;
    c.root = root;

    {
        const auto file = root / "balance" / "matrix.ron";
        const std::string path = file.generic_string();
        const Value doc = ron::parseFile(file);
        const char* rows[4] = {"normal", "pierce", "siege", "arcane"};
        for (size_t a = 0; a < 4; ++a) c.matrix.table[a] = floatTuple<4>(doc, rows[a], path);
    }
    {
        const auto file = root / "balance" / "affinity.ron";
        const Value doc = ron::parseFile(file);
        c.payoff.hpFracPerTier = f32(doc, "hp_frac_per_tier", file.generic_string());
        c.payoff.damageFracPerTier = f32(doc, "damage_frac_per_tier", file.generic_string());
    }

    c.units = loadCatalog<UnitDef>(root / "units", "unit", unitDef);
    c.buildings = loadCatalog<BuildingDef>(root / "buildings", "building", buildingDef);
    c.upgrades = loadCatalog<UpgradeDef>(root / "upgrades", "upgrade", [](const Value& s, std::string_view p) {
        UpgradeDef d = upgradeDef(s, p);
        check(d.maxLevel > 0, "upgrade \"" + d.name + "\" needs max_level >= 1");
        return d;
    });
    c.sources = loadCatalog<SourceDef>(root / "sources", "source", sourceDef);

    {
        const auto file = root / "balance" / "hero.ron";
        c.hero = heroConfig(ron::parseFile(file), file.generic_string());
        check(c.hero.xpCurve.size() == HeroConfig::kMaxLevel - 1u, "xp_curve must cover levels 2..=10");
        check(c.hero.abilities.size() == 4, "hero kit is 3 basics + 1 ult");
    }

    c.abilities = loadCatalog<AbilityDef>(root / "abilities", "ability", [](const Value& s, std::string_view p) {
        AbilityDef d = abilityDef(s, p);
        check(d.maxLevelGate.size() == d.ranks.size(), d.name + ": one level gate per rank");
        return d;
    });
    c.items = loadCatalog<ItemDef>(root / "items", "item", itemDef);

    {
        // BTreeMap<String, Vec<(item, chance)>>: iterated in sorted key order,
        // which is what a std::map of the keys gives.
        const auto file = root / "balance" / "loot.ron";
        const std::string path = file.generic_string();
        const Value doc = ron::parseFile(file);
        if (doc.kind != Value::Kind::Map) throw ron::Error(path + ": expected a map");
        std::map<std::string, const Value*> rows;
        for (size_t i = 0; i < doc.keys.size(); ++i) {
            rows[ron::asString(doc.keys[i], path)] = &doc.items[i];
        }
        c.loot.drops.assign(c.units.defs.size(), {});
        for (const auto& [unitName, entries] : rows) {
            const uint16_t kind = c.units.id(lowerAscii(unitName));
            const std::string p = ron::join(path, unitName);
            auto& row = c.loot.drops[kind];
            row.clear();
            for (const Value& e : ron::asList(*entries, p)) {
                const float chance = f32(e, "chance", p);
                check(chance >= 0.0f && chance <= 1.0f, "loot chance for \"" + unitName + "\" out of range");
                row.emplace_back(c.items.id(lowerAscii(str(e, "item", p))), chance);
            }
        }
    }

    {
        const auto file = root / "balance" / "difficulty.ron";
        const std::string path = file.generic_string();
        const Value doc = ron::parseFile(file);
        c.difficulty.story = difficultyMults(doc, "story", path);
        c.difficulty.normal = difficultyMults(doc, "normal", path);
        c.difficulty.hard = difficultyMults(doc, "hard", path);
        for (const DifficultyMults* m : {&c.difficulty.story, &c.difficulty.normal, &c.difficulty.hard}) {
            check(m->enemyHp > 0.0f && m->enemyDamage > 0.0f && m->enemyCount > 0.0f,
                  "difficulty multipliers must be positive");
        }
    }
    return c;
}

// ---- dump ---------------------------------------------------------------------

std::string bitsHex(float f) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08x", floatBits(f));
    return buf;
}

std::string textHash(std::string_view s) {
    Fnv1a h;
    h.writeBytes(s);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%zu:%016llx", s.size(), static_cast<unsigned long long>(h.finish()));
    return buf;
}

namespace {

std::string attackText(const std::optional<AttackDef>& a) {
    if (!a) return "none";
    std::ostringstream o;
    o << static_cast<int>(a->kind) << ',' << bitsHex(a->damage) << ',' << bitsHex(a->cooldown) << ','
      << bitsHex(a->range) << ',' << bitsHex(a->acquire) << ',' << (a->ranged ? 1 : 0);
    return o.str();
}

std::string joinStrings(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

} // namespace

std::string dumpCatalogs(const Catalogs& c) {
    std::ostringstream o;
    for (size_t i = 0; i < c.units.defs.size(); ++i) {
        const UnitDef& d = c.units.defs[i];
        o << "unit " << i << " \"" << lowerAscii(d.name) << "\" worker=" << (d.worker ? 1 : 0) << " will=" << d.will
          << " hp=" << bitsHex(d.hp) << " speed=" << bitsHex(d.speed) << " radius=" << bitsHex(d.radius)
          << " armor=" << static_cast<int>(d.armor) << " attack=" << attackText(d.attack) << " color="
          << bitsHex(d.color[0]) << ',' << bitsHex(d.color[1]) << ',' << bitsHex(d.color[2]) << " size="
          << bitsHex(d.size[0]) << ',' << bitsHex(d.size[1]) << " cost=" << bitsHex(d.costEssence) << ','
          << bitsHex(d.costAnima) << " build=" << bitsHex(d.buildTime) << " requires=";
        if (d.requirement) {
            o << static_cast<int>(d.requirement->affinity) << ',' << static_cast<int>(d.requirement->tier);
        } else {
            o << "none";
        }
        o << " xp=" << bitsHex(d.xpValue) << " passive=";
        if (!d.passive) {
            o << "none";
        } else if (d.passive->kind == UnitPassive::Kind::FloraRegen) {
            o << "flora," << bitsHex(d.passive->rate) << ',' << bitsHex(d.passive->radius);
        } else if (d.passive->kind == UnitPassive::Kind::Taunt) {
            o << "taunt," << bitsHex(d.passive->radius);
        } else {
            o << "chain," << static_cast<int>(d.passive->jumps) << ',' << bitsHex(d.passive->radius) << ','
              << bitsHex(d.passive->falloff);
        }
        o << '\n';
    }
    for (size_t i = 0; i < c.buildings.defs.size(); ++i) {
        const BuildingDef& d = c.buildings.defs[i];
        o << "building " << i << " \"" << lowerAscii(d.name) << "\" hp=" << bitsHex(d.hp)
          << " armor=" << static_cast<int>(d.armor) << " cost=" << bitsHex(d.costEssence)
          << " build=" << bitsHex(d.buildTime) << " will=" << d.willProvided << " footprint=" << d.footprint[0]
          << ',' << d.footprint[1] << " size=" << bitsHex(d.size[0]) << ',' << bitsHex(d.size[1])
          << " color=" << bitsHex(d.color[0]) << ',' << bitsHex(d.color[1]) << ',' << bitsHex(d.color[2])
          << " produces=" << joinStrings(d.produces, "|") << " researches=" << joinStrings(d.researches, "|")
          << " attack=" << attackText(d.attack) << '\n';
    }
    for (size_t i = 0; i < c.sources.defs.size(); ++i) {
        const SourceDef& d = c.sources.defs[i];
        o << "source " << i << " \"" << lowerAscii(d.name) << "\" affinity=" << static_cast<int>(d.affinity)
          << " essence=" << bitsHex(d.essence) << " anima=" << bitsHex(d.anima) << " footprint="
          << d.footprint[0] << ',' << d.footprint[1] << " size=" << bitsHex(d.size[0]) << ','
          << bitsHex(d.size[1]) << '\n';
    }
    {
        const HeroConfig& h = c.hero;
        o << "hero unit=\"" << h.unit << "\" hp_per_level=" << bitsHex(h.hpPerLevel)
          << " damage_per_level=" << bitsHex(h.damagePerLevel) << " regen_base=" << bitsHex(h.regenBase)
          << " regen_per_level=" << bitsHex(h.regenPerLevel) << " xp_curve=";
        for (size_t i = 0; i < h.xpCurve.size(); ++i) o << (i ? "," : "") << bitsHex(h.xpCurve[i]);
        o << " xp_radius=" << bitsHex(h.xpRadius) << " abilities=" << joinStrings(h.abilities, "|")
          << " revive=" << bitsHex(h.reviveCostBase) << ',' << bitsHex(h.reviveCostPerLevel) << ','
          << bitsHex(h.reviveTime) << '\n';
    }
    for (size_t i = 0; i < c.abilities.defs.size(); ++i) {
        const AbilityDef& d = c.abilities.defs[i];
        o << "ability " << i << " \"" << lowerAscii(d.name) << "\" description=" << textHash(d.description)
          << " kind=" << static_cast<int>(d.kind) << " cast_range=" << bitsHex(d.castRange) << " gates=";
        for (size_t g = 0; g < d.maxLevelGate.size(); ++g) o << (g ? "," : "") << static_cast<int>(d.maxLevelGate[g]);
        o << '\n';
        for (size_t r = 0; r < d.ranks.size(); ++r) {
            const AbilityRank& k = d.ranks[r];
            o << "  rank " << r << " cooldown=" << bitsHex(k.cooldown) << " damage=" << bitsHex(k.damage)
              << " refund=" << bitsHex(k.refund) << " drain=" << bitsHex(k.drain)
              << " heal_frac=" << bitsHex(k.healFrac) << " atk_speed=" << bitsHex(k.atkSpeed)
              << " move_speed=" << bitsHex(k.moveSpeed) << " duration=" << bitsHex(k.duration)
              << " burn=" << bitsHex(k.burn) << " radius=" << bitsHex(k.radius) << " slow=" << bitsHex(k.slow)
              << " hp_bonus=" << bitsHex(k.hpBonus) << " dmg_bonus=" << bitsHex(k.dmgBonus)
              << " thralls=" << k.thralls << '\n';
        }
    }
    for (size_t i = 0; i < c.items.defs.size(); ++i) {
        const ItemDef& d = c.items.defs[i];
        o << "item " << i << " \"" << lowerAscii(d.name) << "\" hp_add=" << bitsHex(d.hpAdd)
          << " damage_add=" << bitsHex(d.damageAdd) << " regen_add=" << bitsHex(d.regenAdd)
          << " move_mult_add=" << bitsHex(d.moveMultAdd) << " active=";
        if (!d.active) {
            o << "none";
        } else if (d.active->kind == ActiveEffect::Kind::Heal) {
            o << "heal," << bitsHex(d.active->amount);
        } else {
            o << "haste," << bitsHex(d.active->atkSpeed) << ',' << bitsHex(d.active->moveSpeed) << ','
              << bitsHex(d.active->secs);
        }
        o << '\n';
    }
    for (size_t i = 0; i < c.upgrades.defs.size(); ++i) {
        const UpgradeDef& d = c.upgrades.defs[i];
        o << "upgrade " << i << " \"" << lowerAscii(d.name) << "\" max_level=" << static_cast<int>(d.maxLevel)
          << " cost=" << bitsHex(d.costEssence) << ',' << bitsHex(d.costAnima) << " time=" << bitsHex(d.time)
          << " effect="
          << (d.effect.kind == UpgradeEffect::Kind::AttackSpeed ? "attack_speed," : "gather_rate,")
          << bitsHex(d.effect.pctPerLevel) << '\n';
    }
    for (size_t a = 0; a < 4; ++a) {
        o << "matrix " << a << ' ';
        for (size_t r = 0; r < 4; ++r) o << (r ? "," : "") << bitsHex(c.matrix.table[a][r]);
        o << '\n';
    }
    for (size_t kind = 0; kind < c.units.defs.size(); ++kind) {
        o << "loot " << kind << ' ';
        const auto& row = c.loot.dropsFor(static_cast<uint16_t>(kind));
        for (size_t i = 0; i < row.size(); ++i) o << (i ? "," : "") << row[i].first << ':' << bitsHex(row[i].second);
        o << '\n';
    }
    o << "affinity hp_frac_per_tier=" << bitsHex(c.payoff.hpFracPerTier)
      << " damage_frac_per_tier=" << bitsHex(c.payoff.damageFracPerTier) << '\n';
    const std::pair<const char*, const DifficultyMults*> modes[3] = {
        {"story", &c.difficulty.story}, {"normal", &c.difficulty.normal}, {"hard", &c.difficulty.hard}};
    for (const auto& [name, m] : modes) {
        o << "difficulty " << name << " enemy_hp=" << bitsHex(m->enemyHp) << " enemy_damage=" << bitsHex(m->enemyDamage)
          << " enemy_count=" << bitsHex(m->enemyCount) << '\n';
    }
    return o.str();
}

} // namespace husk
