#pragma once

#include <map>
#include <string>

#include "sim/EventBus.hpp"
#include "sim/GameData.hpp"
#include "sim/UnitStats.hpp"

namespace WolfBrigade {

// A cost, which is always a map because nothing in this game costs one thing.
//
// A barracks costs wood AND food, and spending has to be all-or-nothing: a
// player who cannot afford the food must not lose the wood. That is why
// TrySpend exists rather than two calls to a Spend that could half-succeed.
using Cost = std::map<std::string, int>;

// How many of each meta-upgrade the player owns, across runs.
//
// A plain map rather than a system, because that is all the parts ported so far
// need: the persistent SaveData that fills it belongs to a later slice, and an
// empty one is a fresh profile - which is what every headless harness runs
// against.
using MetaLevels = std::map<std::string, int>;

// The authoritative mutable state of a run, from `scripts/core/game_state.gd`.
//
// Resources, wave progress, and whether the run is still going. Everything that
// changes announces itself through the EventBus, so no UI or system ever polls
// this - which is what lets the HUD be ported long after the economy is.
//
// Resources are a generic id -> amount map rather than named fields. The game
// has two today and the data file decides how many there are; a `wood` member
// would make adding a third a code change, which is exactly what the original's
// first architecture rule forbids.
class GameState {
public:
    enum class Phase { Playing, Won, Lost };

    GameState(const GameData& data, EventBus& bus) : m_data(&data), m_bus(&bus) {}

    // The data this run reads, level overrides applied. A unit reads the band
    // and the economy's flags through it, where the original reads DataLoader.
    const GameData& Data() const { return *m_data; }

    // Back to the initial data-driven state: on boot, and on Restart.
    //
    // Starting resources are the file's numbers scaled by difficulty and then
    // given a flat persistent bonus. The bonus lives ONLY here, on a fresh run:
    // a Continue restores balances that already banked it, and adding it again
    // on resume would double it every time the player reloaded.
    //
    // Deliberately does NOT clear the chosen difficulty or level. A Restart
    // keeps both - the player picked Hard once, and rebuilding the run is not
    // them changing their mind. It DOES ring the shelter bell off and forget a
    // pending respawn, which belong to the run.
    void Reset();

    // --- Resources ---------------------------------------------------------

    int Amount(const std::string& resource) const;
    void Add(const std::string& resource, int n);

    bool CanAfford(const Cost& cost) const;

    // Spends the whole cost or none of it. Returns false having changed
    // nothing when any single resource is short.
    bool TrySpend(const Cost& cost);

    const std::map<std::string, int>& Resources() const { return m_resources; }

    // --- Difficulty and level ----------------------------------------------
    //
    // The game mode is gone with Endless (73999ce). What a run is now is a
    // campaign LEVEL, and it replaces the mode everywhere the mode was: chosen
    // before the run, kept by Restart, and carried in the save so a Continue
    // resumes the right map.

    void SetDifficulty(const std::string& id) { m_difficulty = id; }

    void SetLevel(const std::string& id) { m_level = id; }

    // The choice as made, "" when none was.
    const std::string& Level() const { return m_level; }

    // The active level, resolving an unset or unknown choice to the data's
    // default - so a harness that boots a match directly plays level 1, and a
    // save naming a level that no longer ships still opens.
    std::string CurrentLevel() const;

    // The active preset, resolving an unset or unknown choice to the data
    // default - so every consumer always has a valid preset and a harness that
    // skips the menu behaves as Normal.
    std::string CurrentDifficulty() const;

    // A named multiplier from the active preset, or 1.0 when the key is absent.
    // That fallback is deliberate: a new difficulty axis added to the JSON is a
    // no-op until code reads it, rather than a zero that silently disables
    // something.
    float DifficultyMultiplier(const std::string& key) const;

    // Scales a freshly-built ENEMY stat block by the active difficulty.
    //
    // Mutates and returns the same block, which is safe only because every
    // spawn is handed its own: scaling a shared one would make the tenth
    // raider inherit the ninth's difficulty on top of its own. Player units
    // never come through here.
    UnitStats& ScaleEnemyStats(UnitStats& stats) const;

    // A wave's base spawn count, scaled - and never dropping a non-empty group
    // to zero. Easy must still send at least one of anything a wave lists, or
    // the schedule quietly loses a wave.
    int ScaleWaveCount(int baseCount) const;

    // --- In-run upgrades ---------------------------------------------------

    bool IsResearched(const std::string& id) const;
    void MarkResearched(const std::string& id);
    const std::map<std::string, bool>& Upgrades() const { return m_upgrades; }

    // --- Phase -------------------------------------------------------------

    bool IsPlaying() const { return m_phase == Phase::Playing; }
    Phase CurrentPhase() const { return m_phase; }

    // Both are one-way and both ignore a second call: a Town Hall destroyed by
    // the last raider on the field must not turn a win into a loss, and the
    // order those two land in is not something the simulation controls.
    void Win();
    void Lose();

    // --- Persistent meta ---------------------------------------------------

    void SetMetaLevels(MetaLevels levels) { m_metaLevels = std::move(levels); }
    const MetaLevels& GetMetaLevels() const { return m_metaLevels; }

    // The flat bonus owned meta-upgrades add to a starting resource, summed
    // across them and scaled by level. Public because Reset is not the only
    // thing that has to be able to explain the number a player starts with.
    int StartingResourceBonus(const std::string& resource) const;

    // --- The village's two run-wide switches (the restructure, b6c73fb) -----

    // The shelter bell: while it is rung, every worker abandons its task and
    // holes up at the nearest drop-off until it is rung again.
    bool WorkersSheltered() const { return m_workersSheltered; }
    void SetWorkersSheltered(bool sheltered) { m_workersSheltered = sheltered; }

    // Where the hero fell while a respawn is pending, or nothing when he is
    // alive. Carried in the save so a run suspended mid-respawn still revives
    // him at the respawn building nearest to where he actually died.
    bool HeroDown() const { return m_heroDown; }
    double HeroDownX() const { return m_heroDownX; }
    void SetHeroDown(double x) {
        m_heroDown = true;
        m_heroDownX = x;
    }
    void ClearHeroDown() {
        m_heroDown = false;
        m_heroDownX = 0.0;
    }

    // --- Save --------------------------------------------------------------

    void SetCurrentWave(int wave) { m_currentWave = wave; }
    int CurrentWave() const { return m_currentWave; }

    Supersonic::Json::Value ToSave() const;

    // Restores a run, BYPASSING Reset - which would zero the resources and
    // clear the upgrades this is trying to put back, and would re-bank the
    // persistent starting bonus on top of balances that already contain it.
    // Reloading twice would double a player's Deeper Coffers.
    //
    // Difficulty and level are set FIRST because everything downstream reads
    // them: the level decides which merged data the world is rebuilt from, and
    // enemy stat scaling reads the difficulty. A save written before levels
    // existed has no "level" and resolves to the default, as the original's
    // own from_save arranges; one written before Endless went still carries a
    // "mode", which is not read.
    //
    // Meta levels are NOT read from the file. They belong to the profile, which
    // outlives the run - and a snapshot that could resurrect them would let a
    // stale save undo a Reset Progress the player has since performed.
    void FromSave(const Supersonic::Json::Value& saved, const MetaLevels& fromProfile);

private:
    const GameData* m_data{nullptr};
    EventBus* m_bus{nullptr};

    std::map<std::string, int> m_resources;
    std::map<std::string, bool> m_upgrades;
    MetaLevels m_metaLevels;

    int m_currentWave{0};
    Phase m_phase{Phase::Playing};

    // Empty means nothing chosen, which resolves to the data default rather
    // than to a hardcoded one.
    std::string m_difficulty;
    std::string m_level;

    bool m_workersSheltered{false};
    bool m_heroDown{false};
    double m_heroDownX{0.0};
};

} // namespace WolfBrigade
