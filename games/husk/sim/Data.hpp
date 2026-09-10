#pragma once

// HUSK's data-driven balance (src/sim/data.rs and difficulty.rs): the damage
// matrix, the unit/building/source/upgrade/ability/item catalogs, the hero
// config, loot tables, affinity payoff and difficulty table, all read from
// RON at startup.
//
// Ids are catalog indices in SORTED FILENAME order, not the order of the names
// inside, so two machines agree on them. Name lookups are lower-case, as the
// game's are.

#include "Ron.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace husk {

enum class AttackType : uint8_t { Normal, Pierce, Siege, Arcane };
enum class ArmorType : uint8_t { Unarmored, Light, Heavy, Fortified };

// Declaration order is the hash order and the index into AffinityState.
enum class Affinity : uint8_t { Steel, Flora, Volt, Stone, Pyre, Aqua };
inline constexpr size_t kAffinityCount = 6;

enum class Difficulty : uint8_t { Story, Normal, Hard };

struct AttackDef {
    AttackType kind = AttackType::Normal;
    float damage = 0.0f;
    float cooldown = 0.0f;
    float range = 0.0f;
    float acquire = 0.0f;
    bool ranged = false;
};

struct UnitPassive {
    enum class Kind : uint8_t { FloraRegen, Taunt, ChainArc };
    Kind kind = Kind::FloraRegen;
    float rate = 0.0f;    // FloraRegen
    float radius = 0.0f;  // all three
    uint8_t jumps = 0;    // ChainArc
    float falloff = 0.0f; // ChainArc
};

struct AffinityRequirement {
    Affinity affinity = Affinity::Steel;
    uint8_t tier = 0;
};

struct UnitDef {
    std::string name;
    bool worker = false;
    uint32_t will = 0;
    float hp = 0.0f;
    float speed = 0.0f;
    float radius = 0.0f;
    ArmorType armor = ArmorType::Unarmored;
    std::optional<AttackDef> attack;
    std::array<float, 3> color{};
    std::array<float, 2> size{};
    float costEssence = 0.0f;
    float costAnima = 0.0f;
    float buildTime = 0.0f;
    // `requires` in the RON; renamed because it is a keyword in C++20.
    std::optional<AffinityRequirement> requirement;
    float xpValue = 0.0f;
    std::optional<UnitPassive> passive;
};

struct BuildingDef {
    std::string name;
    float hp = 0.0f;
    ArmorType armor = ArmorType::Unarmored;
    float costEssence = 0.0f;
    float buildTime = 0.0f;
    uint32_t willProvided = 0;
    std::array<uint32_t, 2> footprint{};
    std::array<float, 2> size{};
    std::array<float, 3> color{};
    std::vector<std::string> produces;
    std::vector<std::string> researches;
    std::optional<AttackDef> attack;
};

struct SourceDef {
    std::string name;
    Affinity affinity = Affinity::Steel;
    float essence = 0.0f;
    float anima = 0.0f;
    std::array<uint32_t, 2> footprint{};
    std::array<float, 2> size{};
};

struct HeroConfig {
    static constexpr uint8_t kMaxLevel = 10;

    std::string unit;
    float hpPerLevel = 0.0f;
    float damagePerLevel = 0.0f;
    float regenBase = 0.0f;
    float regenPerLevel = 0.0f;
    std::vector<float> xpCurve; // cumulative XP to reach levels 2..=10
    float xpRadius = 0.0f;
    std::vector<std::string> abilities;
    float reviveCostBase = 0.0f;
    float reviveCostPerLevel = 0.0f;
    float reviveTime = 0.0f;

    float xpForLevel(uint8_t level) const;
    float reviveCost(uint8_t level) const;
};

enum class AbilityKind : uint8_t { TargetEnemyOrSource, TargetFriendlyUnit, NoTarget };

struct AbilityRank {
    float cooldown = 0.0f;
    float damage = 0.0f;
    float refund = 0.0f;
    float drain = 0.0f;
    float healFrac = 0.0f;
    float atkSpeed = 0.0f;
    float moveSpeed = 0.0f;
    float duration = 0.0f;
    float burn = 0.0f;
    float radius = 0.0f;
    float slow = 0.0f;
    float hpBonus = 0.0f;
    float dmgBonus = 0.0f;
    uint32_t thralls = 0;
};

struct AbilityDef {
    std::string name;
    std::string description;
    AbilityKind kind = AbilityKind::NoTarget;
    float castRange = 0.0f;
    std::vector<uint8_t> maxLevelGate;
    std::vector<AbilityRank> ranks;

    uint8_t maxRank() const { return static_cast<uint8_t>(ranks.size()); }
    // `ranks[(rank.max(1) - 1)]`
    const AbilityRank& rank(uint8_t r) const { return ranks[(r < 1 ? 1 : r) - 1]; }
};

struct ActiveEffect {
    enum class Kind : uint8_t { Heal, Haste };
    Kind kind = Kind::Heal;
    float amount = 0.0f;    // Heal
    float atkSpeed = 0.0f;  // Haste
    float moveSpeed = 0.0f; // Haste
    float secs = 0.0f;      // Haste
};

struct ItemDef {
    std::string name;
    float hpAdd = 0.0f;
    float damageAdd = 0.0f;
    float regenAdd = 0.0f;
    float moveMultAdd = 0.0f;
    std::optional<ActiveEffect> active;
};

struct UpgradeEffect {
    enum class Kind : uint8_t { AttackSpeed, GatherRate };
    Kind kind = Kind::AttackSpeed;
    float pctPerLevel = 0.0f;
};

struct UpgradeDef {
    std::string name;
    uint8_t maxLevel = 0;
    float costEssence = 0.0f;
    float costAnima = 0.0f;
    float time = 0.0f;
    UpgradeEffect effect;
};

struct AffinityPayoff {
    float hpFracPerTier = 0.0f;
    float damageFracPerTier = 0.0f;
};

struct DamageMatrix {
    // [attack][armor]; columns are UNARMORED, LIGHT, HEAVY, FORTIFIED.
    std::array<std::array<float, 4>, 4> table{};

    float multiplier(AttackType attack, ArmorType armor) const {
        return table[static_cast<size_t>(attack)][static_cast<size_t>(armor)];
    }
};

struct DifficultyMults {
    float enemyHp = 1.0f;
    float enemyDamage = 1.0f;
    float enemyCount = 1.0f;
};

struct DifficultyTable {
    DifficultyMults story;
    DifficultyMults normal;
    DifficultyMults hard;

    const DifficultyMults& mults(Difficulty d) const {
        switch (d) {
        case Difficulty::Story: return story;
        case Difficulty::Hard: return hard;
        case Difficulty::Normal: break;
        }
        return normal;
    }
};

// Definitions by id, plus the lower-case name index.
template <class Def>
struct Catalog {
    std::vector<Def> defs;
    std::map<std::string, uint16_t, std::less<>> byName;

    // `id()`: an unknown name is a programming error in the game (it panics);
    // here it throws.
    uint16_t id(std::string_view name) const {
        auto it = byName.find(name);
        if (it == byName.end()) throw std::out_of_range("unknown catalog name \"" + std::string(name) + "\"");
        return it->second;
    }

    // `try_id()`: for hand-authored mission data, where a bad name is a
    // load-time error rather than a crash.
    std::optional<uint16_t> tryId(std::string_view name) const {
        auto it = byName.find(name);
        if (it == byName.end()) return std::nullopt;
        return it->second;
    }

    const Def& def(uint16_t id) const { return defs.at(id); }
};

struct LootTable {
    // (item id, chance) per slain unit kind; an unlisted kind drops nothing.
    std::vector<std::vector<std::pair<uint16_t, float>>> drops;

    const std::vector<std::pair<uint16_t, float>>& dropsFor(uint16_t kind) const;
};

struct Catalogs {
    std::filesystem::path root;
    DamageMatrix matrix;
    AffinityPayoff payoff;
    Catalog<UnitDef> units;
    Catalog<BuildingDef> buildings;
    Catalog<UpgradeDef> upgrades;
    Catalog<SourceDef> sources;
    HeroConfig hero;
    Catalog<AbilityDef> abilities;
    Catalog<ItemDef> items;
    LootTable loot;
    DifficultyTable difficulty;
};

// Everything the sim reads at startup, from `root` (the game's assets/ layout:
// units/, buildings/, balance/, ...). Throws ron::Error on a bad file or on a
// failed load-time assertion, where the game would panic.
Catalogs loadCatalogs(const std::filesystem::path& root);

// The directory the port's copy of the data lives in (games/husk/data).
std::filesystem::path defaultDataRoot();

// `str::to_lowercase` for the ASCII names the data uses. The loader checks they
// are ASCII, so per-byte lowering is the same operation.
std::string lowerAscii(std::string_view s);

// The catalogs in the oracle's `catalogs` text format, floats as IEEE bits, so
// a test can compare the two parses line by line. Missions are dumped by the
// mission module.
std::string dumpCatalogs(const Catalogs& c);

// Shared by every dump: a float as its 8 hex digits, and a string as its
// length and FNV-1a.
std::string bitsHex(float f);
std::string textHash(std::string_view s);

} // namespace husk
