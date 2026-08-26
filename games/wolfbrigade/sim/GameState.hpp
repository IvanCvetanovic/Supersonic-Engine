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

    // String-valued, as in the original, because these persist into the JSON
    // save and an enum's integer would silently change meaning if the order
    // ever did.
    static constexpr const char* kCampaign = "campaign";
    static constexpr const char* kEndless = "endless";

    GameState(const GameData& data, EventBus& bus) : m_data(&data), m_bus(&bus) {}

    // Back to the initial data-driven state: on boot, and on Restart.
    //
    // Starting resources are the file's numbers scaled by difficulty and then
    // given a flat persistent bonus. The bonus lives ONLY here, on a fresh run:
    // a Continue restores balances that already banked it, and adding it again
    // on resume would double it every time the player reloaded.
    //
    // Deliberately does NOT clear the chosen difficulty or mode. A Restart
    // keeps both - the player picked Hard once, and rebuilding the run is not
    // them changing their mind.
    void Reset();

    // --- Resources ---------------------------------------------------------

    int Amount(const std::string& resource) const;
    void Add(const std::string& resource, int n);

    bool CanAfford(const Cost& cost) const;

    // Spends the whole cost or none of it. Returns false having changed
    // nothing when any single resource is short.
    bool TrySpend(const Cost& cost);

    const std::map<std::string, int>& Resources() const { return m_resources; }

    // --- Difficulty and mode -----------------------------------------------

    void SetDifficulty(const std::string& id) { m_difficulty = id; }
    void SetMode(const std::string& mode) { m_mode = mode; }
    bool IsEndless() const { return m_mode == kEndless; }
    const std::string& Mode() const { return m_mode; }

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

    // --- Save --------------------------------------------------------------

    void SetCurrentWave(int wave) { m_currentWave = wave; }
    int CurrentWave() const { return m_currentWave; }

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
    std::string m_mode{kCampaign};
};

} // namespace WolfBrigade
