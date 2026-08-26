#pragma once

#include <map>
#include <string>
#include <vector>

#include "sim/Building.hpp"
#include "sim/GameData.hpp"
#include "sim/GameState.hpp"
#include "sim/UnitStats.hpp"

namespace WolfBrigade {

// What the player keeps between runs, from `scripts/systems/save.gd`.
//
// Renown and the Armory levels it buys. Deliberately NOT part of GameState:
// that is the state of one run and is wiped by Reset, and a player who lost a
// battle has not lost the upgrades they bought before it.
//
// A plain object with explicit load and save rather than the original's static
// cache with a lazy read. The lazy read is what makes a Godot static singleton
// convenient and is also what makes it untestable without clearing global state
// between cases - and this port has no `user://` to write to anyway, so the
// path is the caller's problem.
class Profile {
public:
    int Renown() const { return m_renown; }
    void AddRenown(int amount) { m_renown += amount; }

    // Absent means zero, which is a fresh profile - not an error. Every Armory
    // upgrade starts unowned.
    int MetaLevel(const std::string& id) const;
    void SetMetaLevel(const std::string& id, int level) { m_metaLevels[id] = level; }

    const MetaLevels& AllMetaLevels() const { return m_metaLevels; }

    // Round-trips through JSON, which is what the original writes to disk.
    std::string ToJson() const;
    bool FromJson(const std::string& text);

    bool Save(const std::string& path) const;
    bool Load(const std::string& path);

private:
    int m_renown{0};
    MetaLevels m_metaLevels;
};

// In-run research, from `scripts/systems/upgrades.gd`.
//
// An upgrade is bought once with resources and then adds flat deltas to every
// FUTURE spawn of the entity it names. Units are never retroactively changed -
// you train new ones - while BUILDINGS are, because they are permanent
// structures and a player who researches Reinforced Walls means the Town Hall
// they already have.
//
// Free functions rather than a class: there is no state here that is not
// already in GameState, and the original is static for the same reason.
namespace Upgrades {

// Not yet researched, every prerequisite researched, and affordable.
bool CanResearch(const GameData& data, const GameState& state, const std::string& id);

// Prerequisites met but not yet bought. What the UI shows as available, which
// is a different question from whether it can be paid for right now.
bool IsAvailable(const GameData& data, const GameState& state, const std::string& id);

// Buys it: deducts the cost, marks it, and raises the building effects onto
// everything already standing. `existing` is the player's buildings; pass an
// empty span in a test that has none.
bool Research(const GameData& data, GameState& state, const std::string& id,
              const std::vector<Building*>& existing);

// A unit's stats with every researched effect and every owned meta level
// applied. THE spawn path for player units.
//
// Enemies never come through here, which is the whole of how meta is kept off
// them: there is no flag to forget, only a function they do not call.
UnitStats ForUnit(const GameData& data, const GameState& state, const Profile& profile,
                  const std::string& unitId);

BuildingStats ForBuilding(const GameData& data, const GameState& state, const Profile& profile,
                          const std::string& buildingId);

} // namespace Upgrades

// Persistent progression, from `scripts/systems/meta.gd`.
//
// The Armory: renown earned per run, spent on levels that apply to every run
// after. Effects reuse the exact shape in-run upgrades use - `effects{ entity{
// field: delta } }` - applied as delta times the owned level, so a level-three
// upgrade is three times a level-one one and both are flat.
namespace Meta {

// Adds every owned level's deltas for this entity. Called from Upgrades::ForUnit
// and ForBuilding, so owned bonuses ride the ordinary player-spawn path rather
// than needing their own wiring at every call site.
//
// Returns the number of effect fields that did NOT exist on the target, which
// is a data typo and worth surfacing.
int Apply(const GameData& data, const Profile& profile, UnitStats& stats,
          const std::string& entityId);
int Apply(const GameData& data, const Profile& profile, BuildingStats& stats,
          const std::string& entityId);

// The flat bonus owned upgrades add to a STARTING resource. Read only by a
// fresh run: a Continue restores balances that already banked it.
int StartingResourceBonus(const GameData& data, const Profile& profile,
                          const std::string& resource);

int MaxLevel(const GameData& data, const std::string& id);
bool IsMaxed(const GameData& data, const Profile& profile, const std::string& id);

// What the next level costs: base + growth times the level already owned. -1
// for an unknown id or a maxed one, which is the same answer to the UI - there
// is nothing to buy.
int NextCost(const GameData& data, const Profile& profile, const std::string& id);

bool CanBuy(const GameData& data, const Profile& profile, const std::string& id);
bool Buy(const GameData& data, Profile& profile, const std::string& id);

// What a run reaching `wave` is WORTH. Pure - it banks nothing.
//
// The sum over waves survived of (per_wave + growth * (k-1)), which is a
// rising reward for going deeper rather than a flat one per wave.
int RunEndRenown(const GameData& data, int wave, bool won);

// Banks it. Safe to call once per run and only once: the phase guard on
// Win/Lose fires each of them a single time, and a finished run clears its
// Continue save - so this cannot be farmed by quitting and resuming.
int AwardRunEnd(const GameData& data, Profile& profile, int wave, bool won);

} // namespace Meta

} // namespace WolfBrigade
