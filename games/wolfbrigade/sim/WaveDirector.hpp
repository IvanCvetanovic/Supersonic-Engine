#pragma once

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "sim/EventBus.hpp"
#include "sim/GameData.hpp"
#include "sim/GameState.hpp"
#include "sim/UnitStats.hpp"

namespace WolfBrigade {

// Runs the enemy schedule, from `scripts/systems/wave_director.gd`.
//
// At each wave's authored time it queues that wave's units and lets them out of
// the spawn edge one every `spawn_interval`, so a wave arrives as a column
// rather than as a wall. It counts what is still alive to declare victory when
// the last wave is cleared, and loses the run when the Town Hall falls.
//
// Spawning goes through a CALLBACK rather than a reference to whatever creates
// units. That is the original's decoupling and it is also what makes this the
// best-tested slice in the port: the game's own harness hands it a callback
// that only records, so the director can be driven through the entire schedule
// with nothing else in the world at all.
//
// It only advances while the run is playing. A director that kept spawning
// through a defeat would bury a player who had already lost.
//
// Endless mode is gone, with the game's 73999ce. The schedule is the whole of
// what this runs: the waves the active level's waves.json names.
class WaveDirector {
public:
    // What a spawn asks for: a stat block already scaled by difficulty and by
    // the queued entry's own multipliers, and where to put it.
    using SpawnFn = std::function<void(const UnitStats&, const glm::vec2&)>;

    WaveDirector(const GameData& data, GameState& state, EventBus& bus)
        : m_data(&data), m_state(&state), m_bus(&bus) {}

    // Reads the schedule and remembers where enemies come in. Called once
    // before the first step, and again after a load - the schedule is
    // re-derived from data rather than saved, so only the live counters below
    // are restored.
    //
    // Deliberately touches NO counter, so a restore can call it twice without
    // rewinding the run it just put back.
    void Setup(SpawnFn spawn, float enemyX, float groundY);

    // Back to the start of a run: no time elapsed, no wave reached, nothing
    // queued and nothing alive.
    //
    // The counters Setup deliberately leaves alone, and exactly the set
    // FromSave writes. Godot never needs this because a fresh match is a fresh
    // scene and therefore a brand-new director; a Match here is an object that
    // gets re-booted, and without this a second run would begin with the first
    // one's clock already past its opening waves and its dead still counted as
    // alive - so the new run would spawn a backlog immediately and could never
    // declare victory.
    void Reset();

    // One step. The original is a Godot _process, and the harness drives it at
    // a fixed dt rather than a frame time - which is what makes the counts it
    // prints reproducible at all.
    //
    // A double, like every other scalar in the simulation. GDScript's float is
    // 64-bit and an elapsed time accumulated over hundreds of steps is exactly
    // where the width shows - see the note at the top of Unit.hpp.
    void Step(double delta);

    // --- What the HUD asks ------------------------------------------------

    int TotalWaves() const { return static_cast<int>(m_waves.size()); }

    // Seconds until the next wave, or -1 once every wave has started.
    double SecondsToNextWave() const;

    // --- What the rest of the simulation tells it -------------------------

    // An enemy entered or left the world. The original hears these on the
    // EventBus; here they are calls, because the thing that would emit them -
    // the unit - is not ported yet, and a director that cannot be told about a
    // death cannot ever declare victory.
    void OnEnemySpawned();
    void OnEnemyDied();

    // The Town Hall fell.
    void OnTownHallDestroyed();

    int AliveEnemies() const { return m_aliveEnemies; }

    Supersonic::Json::Value ToSave() const;

    // Restores the live counters. Setup must have run FIRST: the schedule is
    // re-derived from the data rather than saved, so only progress comes back
    // through here. A save written while Endless existed still loads - its
    // `endless_index` is simply not read.
    //
    // The alive count is a PARAMETER rather than a saved number. The original
    // recounts the enemy group after every unit is rebuilt, and says why: the
    // restore path bypasses the spawn function that would have emitted
    // unit_spawned, so a stored count would be the only source of a truth
    // nothing else could correct.
    void FromSave(const Supersonic::Json::Value& saved, int aliveEnemies);
    double Elapsed() const { return m_elapsed; }
    int QueuedSpawns() const { return static_cast<int>(m_queue.size()); }

private:
    // One enemy waiting its turn at the spawn edge.
    //
    // The multipliers ride ALONG with the queued entry, as the original's queue
    // still carries them: difficulty scales the stat block when the spawn
    // actually happens, and these multiply on top of that. The shipped
    // schedule queues everything at 1.0.
    struct Queued {
        std::string unit;
        float hpMultiplier{1.0f};
        float damageMultiplier{1.0f};
    };

    void StartWave(size_t index);
    void SpawnNext();
    void CheckVictory();

    const GameData* m_data{nullptr};
    GameState* m_state{nullptr};
    EventBus* m_bus{nullptr};

    SpawnFn m_spawn;
    float m_enemyX{5960.0f};
    float m_groundY{800.0f};

    std::vector<Supersonic::Json::Value> m_waves;
    double m_spawnInterval{0.8};

    double m_elapsed{0.0};
    size_t m_nextWave{0};
    std::vector<Queued> m_queue;
    double m_spawnAccumulator{0.0};
    int m_aliveEnemies{0};
    bool m_allSpawned{false};
};

} // namespace WolfBrigade
