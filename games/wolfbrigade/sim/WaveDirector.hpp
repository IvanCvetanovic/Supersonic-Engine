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
class WaveDirector {
public:
    // What a spawn asks for: a stat block already scaled by difficulty and by
    // endless growth, and where to put it.
    using SpawnFn = std::function<void(const UnitStats&, const glm::vec2&)>;

    WaveDirector(const GameData& data, GameState& state, EventBus& bus)
        : m_data(&data), m_state(&state), m_bus(&bus) {}

    // Reads the schedule and the endless configuration, and remembers where
    // enemies come in. Called once before the first step, and again after a
    // load - the schedule is re-derived from data rather than saved, so only
    // the live counters below are restored.
    void Setup(SpawnFn spawn, float enemyX, float groundY);

    // One step. The original is a Godot _process, and the harness drives it at
    // a fixed dt rather than a frame time - which is what makes the counts it
    // prints reproducible at all.
    void Step(float delta);

    // --- What the HUD asks ------------------------------------------------

    int TotalWaves() const { return static_cast<int>(m_waves.size()); }
    bool IsEndless() const { return m_endless; }

    // Seconds until the next wave, or -1 when none is scheduled. Endless
    // always has a next one, so it never returns -1.
    float SecondsToNextWave() const;

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
    float Elapsed() const { return m_elapsed; }
    int QueuedSpawns() const { return static_cast<int>(m_queue.size()); }

private:
    // One enemy waiting its turn at the spawn edge.
    //
    // The multipliers ride ALONG with the queued entry rather than being
    // applied when it is queued, because an endless wave's growth is a property
    // of the wave and the stat block does not exist until the spawn actually
    // happens.
    struct Queued {
        std::string unit;
        float hpMultiplier{1.0f};
        float damageMultiplier{1.0f};
    };

    void StartWave(size_t index);
    void StartEndlessWave();
    void SpawnNext();
    void CheckVictory();
    float NextEndlessTime() const;

    const GameData* m_data{nullptr};
    GameState* m_state{nullptr};
    EventBus* m_bus{nullptr};

    SpawnFn m_spawn;
    float m_enemyX{5960.0f};
    float m_groundY{800.0f};

    std::vector<Supersonic::Json::Value> m_waves;
    float m_spawnInterval{0.8f};

    float m_elapsed{0.0f};
    size_t m_nextWave{0};
    std::vector<Queued> m_queue;
    float m_spawnAccumulator{0.0f};
    int m_aliveEnemies{0};
    bool m_allSpawned{false};

    bool m_endless{false};
    Supersonic::Json::Value m_endlessConfig;
    int m_endlessIndex{0};
    float m_endlessBaseTime{0.0f};
    float m_endlessInterval{75.0f};

    // Never generate more than this many endless waves in one step.
    //
    // A step with a large delta - a load hitch, or a harness stepping two
    // seconds at a time - would otherwise generate every wave the elapsed time
    // has passed, all at once, and the queue would explode. The cap turns that
    // into a backlog that drains over the next few steps.
    static constexpr int kEndlessPerStepCap = 8;
};

} // namespace WolfBrigade
