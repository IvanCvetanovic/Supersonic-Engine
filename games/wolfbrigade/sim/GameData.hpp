#pragma once

#include <string>
#include <vector>

#include "core/Json.hpp"

namespace WolfBrigade {

// Canonical ids, from `scripts/core/ids.gd`.
//
// These DOCUMENT keys that already exist in the JSON; they are not behaviour.
// The original's rule is that anything crossing a module boundary references an
// id through here rather than as a bare literal, so a rename is a compile error
// instead of a lookup that silently returns nothing.
namespace Ids {
inline constexpr const char* kWood = "wood";
inline constexpr const char* kFood = "food";

inline constexpr const char* kTownHall = "town_hall";
inline constexpr const char* kBarracks = "barracks";
inline constexpr const char* kTower = "tower";
inline constexpr const char* kArmory = "armory";
inline constexpr const char* kTemple = "temple";
inline constexpr const char* kStorehouse = "storehouse";
inline constexpr const char* kWaystone = "waystone";
inline constexpr const char* kFarm = "farm";

inline constexpr const char* kWorker = "worker";
inline constexpr const char* kHero = "hero";
inline constexpr const char* kPriest = "priest";
inline constexpr const char* kSoldier = "soldier";
inline constexpr const char* kArcher = "archer";
inline constexpr const char* kRaider = "raider";
inline constexpr const char* kBrute = "brute";
} // namespace Ids

// Every tunable number in the game, from `data/*.json`.
//
// A port of `scripts/core/data_loader.gd`, which is an autoload that parses all
// twelve files once at startup and hands out typed accessors. The rule it
// enforces is the first one in the game's own CLAUDE.md: NOTHING here hardcodes
// a stat. A file is data or it is a bug.
//
// The files are copied into this repository rather than read from the game's,
// because a build machine will not have the game checked out - and because the
// game repository is the ORACLE this port is verified against, which makes it
// read-only. When a number changes there, it is copied here deliberately.
//
// CAMPAIGN LEVELS. Since the game's campaign spine (1016954), a level may
// override top-level keys of world, economy and waves, and World(), Economy()
// and WaveConfig() hand out the MERGED copies - so every consumer is
// level-aware without knowing levels exist, which is the original's own design.
// LoadAll applies the default level; a match applies the one the player chose.
//
// One difference from Godot worth knowing before it surprises somebody. Godot's
// Dictionary preserves insertion order; this engine's Json::Object is a
// std::map and is sorted by key. Nothing in the data depends on object order -
// the wave schedule, the level order and the difficulty menu order are all JSON
// ARRAYS, which keep their order - but anything ported later that iterates an
// object and cares about the sequence has to sort explicitly rather than
// inherit it.
class GameData {
public:
    // Parses all twelve files from `directory`, then applies the default level.
    // Returns false if any was missing or malformed, having loaded the rest:
    // one broken file is a fixable authoring mistake, and refusing to load the
    // other eleven turns it into a game that will not start with nothing to
    // point at.
    //
    // A file that fails to parse becomes an EMPTY OBJECT rather than absent, so
    // every accessor below keeps its type and the validator reports missing
    // cross-references instead of the whole thing collapsing.
    bool LoadAll(const std::string& directory);

    // What went wrong while loading, as human strings. Separate from the
    // validator's issues: this is "the file would not parse", that is "the file
    // parsed and says something impossible".
    const std::vector<std::string>& LoadErrors() const { return m_loadErrors; }

    // How many files LoadAll knows about. Twelve, and the original prints it.
    static int FileCount();

    // The logical keys, in the order data_loader.gd lists them, so a log line
    // from the port and one from the original can be read side by side.
    static const std::vector<std::string>& FileKeys();

    // The raw document behind a logical key, or a null value. Raw, meaning
    // BEFORE any level's overrides - which is what the loader prints key counts
    // of, and what every level merges over.
    const Supersonic::Json::Value& Raw(const std::string& key) const;

    // --- Typed accessors, one per data_loader.gd function ------------------

    // The three a level can override, merged for the active level.
    const Supersonic::Json::Value& World() const { return Section("world"); }
    const Supersonic::Json::Value& Economy() const { return Section("economy"); }

    // The schedule itself, which is an array inside waves.json rather than the
    // document - `waves()` and `wave_config()` in the original are two views of
    // one file.
    const Supersonic::Json::Array& Waves() const { return WaveConfig()["waves"].AsArray(); }
    const Supersonic::Json::Value& WaveConfig() const { return Section("waves"); }

    const Supersonic::Json::Value& Units() const { return Raw("units"); }
    const Supersonic::Json::Value& Unit(const std::string& id) const { return Units()[id]; }
    const Supersonic::Json::Value& Buildings() const { return Raw("buildings"); }
    const Supersonic::Json::Value& Building(const std::string& id) const { return Buildings()[id]; }
    const Supersonic::Json::Value& Upgrades() const { return Raw("upgrades"); }
    const Supersonic::Json::Value& Upgrade(const std::string& id) const { return Upgrades()[id]; }

    const Supersonic::Json::Value& Difficulty() const { return Raw("difficulty"); }
    const Supersonic::Json::Value& DifficultyPreset(const std::string& id) const {
        return Difficulty()["presets"][id];
    }
    std::string DifficultyDefault() const;
    const Supersonic::Json::Array& DifficultyOrder() const {
        return Difficulty()["order"].AsArray();
    }

    const Supersonic::Json::Value& Audio() const { return Raw("audio"); }

    const Supersonic::Json::Value& Meta() const { return Raw("meta"); }
    const Supersonic::Json::Value& MetaCurrency() const { return Meta()["currency"]; }
    const Supersonic::Json::Value& MetaUpgrades() const { return Meta()["upgrades"]; }
    const Supersonic::Json::Value& MetaUpgrade(const std::string& id) const {
        return MetaUpgrades()[id];
    }

    const Supersonic::Json::Value& Fx() const { return Raw("fx"); }

    // --- Campaign levels ---------------------------------------------------

    // id -> level definition ({display_name, description, world/economy/waves
    // overrides, capture_points}).
    const Supersonic::Json::Value& Levels() const { return Raw("levels")["levels"]; }
    const Supersonic::Json::Value& Level(const std::string& id) const { return Levels()[id]; }

    // Menu display order of level ids.
    const Supersonic::Json::Array& LevelsOrder() const { return Raw("levels")["order"].AsArray(); }

    // The level used when nothing was chosen: the first in the order, or
    // "level_1" when the order is empty - the original's own fallback.
    std::string DefaultLevel() const;

    // Makes `id` the active level: world, economy and waves are rebuilt as the
    // base file with the level's overrides laid over it. Top-level keys
    // replace, arrays whole. It NEVER stacks - every call merges from the
    // pristine base, so switching levels is clean. An unknown id means the
    // default level, not an error.
    void ApplyLevel(const std::string& id);

    const std::string& CurrentLevelId() const { return m_levelId; }

    // The active level's capture points; empty when it has none.
    const Supersonic::Json::Array& LevelCapturePoints() const {
        return Level(m_levelId)["capture_points"].AsArray();
    }

    // --- Hero abilities ----------------------------------------------------

    const Supersonic::Json::Value& Abilities() const { return Raw("abilities"); }

    // One ability's definition ({kind, cooldown, ...}), or null for an unknown
    // id - and for `_comment`, which the original filters so a comment in the
    // file is never mistaken for an ability.
    const Supersonic::Json::Value& Ability(const std::string& id) const;

private:
    // The merged copy of a section when a level has been applied, else the
    // raw document.
    const Supersonic::Json::Value& Section(const std::string& key) const;

    Supersonic::Json::Object m_documents;
    Supersonic::Json::Object m_merged;
    std::string m_levelId;
    std::vector<std::string> m_loadErrors;
};

} // namespace WolfBrigade
