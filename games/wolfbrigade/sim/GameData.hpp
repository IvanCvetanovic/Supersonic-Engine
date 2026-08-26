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

inline constexpr const char* kWorker = "worker";
inline constexpr const char* kSoldier = "soldier";
inline constexpr const char* kArcher = "archer";
inline constexpr const char* kRaider = "raider";
inline constexpr const char* kBrute = "brute";
} // namespace Ids

// Every tunable number in the game, from `data/*.json`.
//
// A port of `scripts/core/data_loader.gd`, which is an autoload that parses all
// ten files once at startup and hands out typed accessors. The rule it enforces
// is the first one in the game's own CLAUDE.md: NOTHING here hardcodes a stat.
// A file is data or it is a bug.
//
// The files are copied into this repository rather than read from the game's,
// because a build machine will not have the game checked out - and because the
// game repository is the ORACLE this port is verified against, which makes it
// read-only. When a number changes there, it is copied here deliberately.
//
// One difference from Godot worth knowing before it surprises somebody. Godot's
// Dictionary preserves insertion order; this engine's Json::Object is a
// std::map and is sorted by key. Nothing in the data depends on object order -
// the wave schedule and the difficulty menu order are both JSON ARRAYS, which
// do keep their order - but anything ported later that iterates an object and
// cares about the sequence has to sort explicitly rather than inherit it.
class GameData {
public:
    // Parses all ten files from `directory`. Returns false if any was missing
    // or malformed, having loaded the rest: one broken file is a fixable
    // authoring mistake, and refusing to load the other nine turns it into a
    // game that will not start with nothing to point at.
    //
    // A file that fails to parse becomes an EMPTY OBJECT rather than absent, so
    // every accessor below keeps its type and the validator reports missing
    // cross-references instead of the whole thing collapsing.
    bool LoadAll(const std::string& directory);

    // What went wrong while loading, as human strings. Separate from the
    // validator's issues: this is "the file would not parse", that is "the file
    // parsed and says something impossible".
    const std::vector<std::string>& LoadErrors() const { return m_loadErrors; }

    // How many files LoadAll knows about. Ten, and the original prints it.
    static int FileCount();

    // The logical keys, in the order data_loader.gd lists them, so a log line
    // from the port and one from the original can be read side by side.
    static const std::vector<std::string>& FileKeys();

    // The raw document behind a logical key, or a null value.
    const Supersonic::Json::Value& Raw(const std::string& key) const;

    // --- Typed accessors, one per data_loader.gd function ------------------

    const Supersonic::Json::Value& World() const { return Raw("world"); }
    const Supersonic::Json::Value& Units() const { return Raw("units"); }
    const Supersonic::Json::Value& Unit(const std::string& id) const { return Units()[id]; }
    const Supersonic::Json::Value& Buildings() const { return Raw("buildings"); }
    const Supersonic::Json::Value& Building(const std::string& id) const { return Buildings()[id]; }
    const Supersonic::Json::Value& Economy() const { return Raw("economy"); }
    const Supersonic::Json::Value& Upgrades() const { return Raw("upgrades"); }
    const Supersonic::Json::Value& Upgrade(const std::string& id) const { return Upgrades()[id]; }

    // The schedule itself, which is an array inside waves.json rather than the
    // document - `waves()` and `wave_config()` in the original are two views of
    // one file.
    const Supersonic::Json::Array& Waves() const { return Raw("waves")["waves"].AsArray(); }
    const Supersonic::Json::Value& WaveConfig() const { return Raw("waves"); }

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

private:
    Supersonic::Json::Object m_documents;
    std::vector<std::string> m_loadErrors;
};

} // namespace WolfBrigade
