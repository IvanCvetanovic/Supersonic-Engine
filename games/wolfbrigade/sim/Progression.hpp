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

    // CLAMPED AT ZERO, because `save.gd:105` is `maxi(0, renown() + delta)` and
    // this took the delta unguarded. Nothing shipped could reach the
    // difference - Meta::Buy checks CanBuy first, and AwardRunEnd only ever
    // adds - so it was a divergence from the oracle that no caller exercised
    // and no test named. It is the accessor that is public, though, and the
    // next caller is the one that would find it.
    void AddRenown(int amount);

    // Absent means zero, which is a fresh profile - not an error. Every Armory
    // upgrade starts unowned.
    int MetaLevel(const std::string& id) const;
    void SetMetaLevel(const std::string& id, int level) {
        m_metaLevels[id] = level;
        m_dirty = true;
    }

    const MetaLevels& AllMetaLevels() const { return m_metaLevels; }

    // The deepest wave ever reached, from `save.gd`.
    //
    // MAX ONLY, and the asymmetry is the whole of it: a player who restarts and
    // replays the easy early waves must not lower their own record, so a
    // smaller number is a no-op rather than a write. It lives on the Profile
    // because it outlives the run, like the renown beside it.
    //
    // It arrives with the match-boot slice rather than with the rest of meta
    // because the edge that feeds it is main.gd's wave_started -> record_wave,
    // and that edge is the boot's. The cost of arriving late is stated: the
    // profile document gains a "best_wave" key with no version bump, which is
    // safe in exactly one direction. An older profile reads 0, which is right;
    // a newer profile read by an older build loses the record silently.
    int BestWave() const { return m_bestWave; }
    void RecordWave(int wave);

    // --- Preferences -------------------------------------------------------
    //
    // `save.gd`'s other half: difficulty, control scheme, mute and volume. They live HERE
    // rather than in a store of their own because the original is one file with
    // one dictionary and seven keys, and a second file would mean a second
    // path, a second load, a second corruption policy and a second thing every
    // screen has to remember to read.
    //
    // What actually separates the two halves is not where they are written but
    // what erases them, and that is `ResetProgress` below rather than a file
    // boundary.
    //
    // The key names are the original's - "difficulty", "control_scheme",
    // "muted", "master_volume" - so the two games read each other's save. (The
    // game mode went with Endless; a profile still carrying "mode" is read
    // without it.)
    //
    // The getters take the caller's fallback exactly as `save.gd:49-67` do,
    // because "never chosen" is a state the menu has to be able to see: on a
    // first launch the difficulty falls back to the one the DATA declares, not
    // to one this class invented. An empty string is that state, which is the
    // convention `GameState::m_difficulty` already uses for the same value.
    std::string Difficulty(const std::string& fallback) const;
    void SetDifficulty(std::string id) {
        m_difficulty = std::move(id);
        m_dirty = true;
    }

    // The preferred control scheme: "auto", "desktop" or "touch". Only STORED
    // here, as `save.gd:56` stores it; what "auto" resolves to is the input
    // layer's business.
    std::string ControlScheme(const std::string& fallback) const;
    void SetControlScheme(std::string scheme) {
        m_controlScheme = std::move(scheme);
        m_dirty = true;
    }

    // No fallback, unlike the original's `muted(fallback)`. A bool has nowhere
    // to put "absent", every one of the four call sites passes false, and false
    // is the default here - so the tri-state would be a distinction this game
    // never draws. Say it rather than build it.
    bool Muted() const { return m_muted; }
    void SetMuted(bool muted) {
        m_muted = muted;
        m_dirty = true;
    }

    // A 0..1 linear scalar, clamped on the way in AND on the way out of JSON,
    // because `save.gd:76` clamps on read and `:79` on write. A hand-edited
    // save must not be able to over-drive the mixer.
    float MasterVolume() const { return m_masterVolume; }
    void SetMasterVolume(float volume);

    // `save.gd:111-116`. Wipes the META half - renown, owned levels, and the
    // high score - and KEEPS the preferences beside it. The asymmetry is the
    // reason this function exists: a player starting over does not want their
    // volume reset, and `verify_settings.gd:79` asserts exactly that.
    void ResetProgress();

    // Whether anything has changed since the last Save or Load.
    //
    // `save.gd` writes through on EVERY setter - `_put` calls `_flush` - which
    // is affordable there because the document is tiny and the writes are rare.
    // Reproducing that literally would mean a filesystem call inside a class
    // that deliberately has no path, so the flag is here and the write is the
    // caller's, once per tick.
    //
    // The point is that it is not something each new writer has to remember.
    // The Armory screen, the settings screen and the run-end award all change
    // this object, and a design where each of them calls Save is a design where
    // the fourth one does not.
    //
    // Load and FromJson CLEAR it: arriving from disk is not a change to write
    // back, and treating it as one would rewrite the file on every launch.
    bool IsDirty() const { return m_dirty; }

    // Round-trips through JSON, which is what the original writes to disk.
    std::string ToJson() const;
    bool FromJson(const std::string& text);

    bool Save(const std::string& path) const;
    bool Load(const std::string& path);

private:
    int m_renown{0};
    int m_bestWave{0};
    MetaLevels m_metaLevels;

    // Empty means never chosen; see Difficulty/Mode above.
    std::string m_difficulty;
    std::string m_controlScheme;
    bool m_muted{false};
    float m_masterVolume{1.0f};

    // Not serialised, and cleared by Save and Load. See IsDirty.
    //
    // `mutable` because Save is const: writing the file is not a change to
    // the profile, it is the point at which the profile stops having one.
    mutable bool m_dirty{false};
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

// Lands a research whose cost was paid when it entered a building's research
// queue - the Armory path, where research takes time. Marks it and raises the
// building effects exactly as Research does, without charging. False when it
// was already researched or nobody authored it, which is when nothing should
// be announced.
bool CompleteResearch(const GameData& data, GameState& state, const std::string& id,
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
