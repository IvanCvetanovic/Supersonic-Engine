#include "sim/WaveDirector.hpp"

#include <algorithm>
#include <cmath>

namespace WolfBrigade {

namespace {

int roundToInt(float value) { return static_cast<int>(std::lround(value)); }

} // namespace

void WaveDirector::Setup(SpawnFn spawn, float enemyX, float groundY) {
    m_spawn = std::move(spawn);
    m_enemyX = enemyX;
    m_groundY = groundY;

    m_waves = m_data->Waves();
    m_spawnInterval = m_data->WaveConfig()["spawn_interval"].AsNumber(0.8);

    m_endless = m_state->IsEndless();
    m_endlessConfig = m_data->WaveConfig()["endless"];
    m_endlessInterval = m_endlessConfig["interval"].AsNumber(75.0);

    // Endless begins a delay after the LAST scripted wave, not after the clock
    // starts. A player who has just fought wave five gets the same breathing
    // space whatever the schedule looked like.
    double lastTime = 0.0;
    if (!m_waves.empty()) lastTime = m_waves.back()["time"].AsNumber(0.0);
    m_endlessBaseTime = lastTime + m_endlessConfig["start_delay"].AsNumber(90.0);
}

void WaveDirector::Step(double delta) {
    // Nothing runs without somewhere to put a spawn, and nothing runs once the
    // run is decided: a director that kept spawning through a defeat would
    // bury a player who had already lost.
    if (!m_spawn || !m_state->IsPlaying()) return;

    m_elapsed += delta;

    // A while, not an if. A step long enough to pass two wave times has to
    // start both, or a hitch quietly skips a wave.
    while (m_nextWave < m_waves.size() &&
           m_elapsed >= m_waves[m_nextWave]["time"].AsNumber(0.0)) {
        StartWave(m_nextWave);
        ++m_nextWave;
    }

    if (m_endless && m_nextWave >= m_waves.size()) {
        int made = 0;
        while (m_elapsed >= NextEndlessTime() && made < kEndlessPerStepCap) {
            StartEndlessWave();
            ++made;
        }
    }

    if (!m_queue.empty()) {
        m_spawnAccumulator += delta;
        while (!m_queue.empty() && m_spawnAccumulator >= m_spawnInterval) {
            m_spawnAccumulator -= m_spawnInterval;
            SpawnNext();
        }
    }

    // Victory is campaign-only, and it is evaluated HERE rather than only when
    // something dies. A final wave that contributes no spawns would otherwise
    // never resolve: nothing dies, so nothing ever asks, and the run sits at
    // "playing" forever with an empty field. Endless never finishes spawning,
    // so it never wins - only the Town Hall falling ends it.
    if (!m_endless && m_nextWave >= m_waves.size() && m_queue.empty()) {
        m_allSpawned = true;
        CheckVictory();
    }
}

double WaveDirector::SecondsToNextWave() const {
    if (m_nextWave < m_waves.size()) {
        return std::max(0.0, m_waves[m_nextWave]["time"].AsNumber(0.0) - m_elapsed);
    }
    if (m_endless) return std::max(0.0, NextEndlessTime() - m_elapsed);
    return -1.0;
}

double WaveDirector::NextEndlessTime() const {
    return m_endlessBaseTime + static_cast<double>(m_endlessIndex) * m_endlessInterval;
}

void WaveDirector::StartWave(size_t index) {
    const Supersonic::Json::Value& wave = m_waves[index];

    for (const Supersonic::Json::Value& spawn : wave["spawns"].AsArray()) {
        const std::string unit = spawn["unit"].AsString(Ids::kRaider);
        const int count = static_cast<int>(spawn["count"].AsNumber(0.0));

        // Difficulty decides how MANY, not how tough - that is applied per
        // spawn, from the stat block.
        const int scaled = m_state->ScaleWaveCount(count);
        for (int i = 0; i < scaled; ++i) m_queue.push_back(Queued{unit, 1.0f, 1.0f});
    }

    // Primed, so the first of the wave comes out on the next drain rather than
    // after a full interval of nothing.
    m_spawnAccumulator = m_spawnInterval;

    const int number = static_cast<int>(wave["index"].AsNumber(static_cast<double>(index) + 1.0));
    m_state->SetCurrentWave(number);
    m_bus->waveStarted.Emit(number);
}

void WaveDirector::StartEndlessWave() {
    const int e = m_endlessIndex;
    const std::string unit = m_endlessConfig["unit"].AsString(Ids::kRaider);

    const int count = static_cast<int>(m_endlessConfig["base_count"].AsNumber(6.0)) +
                      e * static_cast<int>(m_endlessConfig["count_growth"].AsNumber(2.0));
    const float hp = 1.0f + static_cast<float>(e) * m_endlessConfig["hp_growth"].AsFloat(0.08f);
    const float damage =
        1.0f + static_cast<float>(e) * m_endlessConfig["damage_growth"].AsFloat(0.05f);

    const int scaled = m_state->ScaleWaveCount(count);
    for (int i = 0; i < scaled; ++i) m_queue.push_back(Queued{unit, hp, damage});

    // A heavy on a fixed cadence, counted from ONE: brute_every 2 means the
    // second endless wave, the fourth, the sixth - not the first.
    const int bruteEvery = static_cast<int>(m_endlessConfig["brute_every"].AsNumber(0.0));
    if (bruteEvery > 0 && (e + 1) % bruteEvery == 0) {
        const std::string heavy = m_endlessConfig["heavy_unit"].AsString(Ids::kBrute);
        const int brutes = m_state->ScaleWaveCount(
            static_cast<int>(m_endlessConfig["brute_count"].AsNumber(1.0)));
        for (int i = 0; i < brutes; ++i) m_queue.push_back(Queued{heavy, hp, damage});
    }

    m_spawnAccumulator = m_spawnInterval;
    ++m_endlessIndex;

    // Wave numbers carry on past the scripted ones: 6, 7, 8. This assumes the
    // scripted waves are numbered contiguously, which they are - the original
    // says so in the same place, and says to read the last wave's own index
    // instead if that ever stops being true.
    m_state->SetCurrentWave(static_cast<int>(m_waves.size()) + m_endlessIndex);
    m_bus->waveStarted.Emit(m_state->CurrentWave());
}

void WaveDirector::SpawnNext() {
    const Queued item = m_queue.front();
    m_queue.erase(m_queue.begin());

    // Its own block, every time. Difficulty scales it, then endless growth
    // multiplies on top - and a shared block would compound both across every
    // spawn in the wave.
    UnitStats stats = UnitStats::FromJson(item.unit, m_data->Unit(item.unit));
    m_state->ScaleEnemyStats(stats);

    if (item.hpMultiplier != 1.0f) {
        stats.maxHp = std::max(1, roundToInt(static_cast<float>(stats.maxHp) * item.hpMultiplier));
    }
    if (item.damageMultiplier != 1.0f) {
        stats.damage =
            std::max(0, roundToInt(static_cast<float>(stats.damage) * item.damageMultiplier));
    }

    m_spawn(stats, glm::vec2(m_enemyX, m_groundY));
}

Supersonic::Json::Value WaveDirector::ToSave() const {
    Supersonic::Json::Object out;
    out["elapsed"] = Supersonic::Json::Value(m_elapsed);
    out["next_wave"] = Supersonic::Json::Value(static_cast<double>(m_nextWave));
    out["endless_index"] = Supersonic::Json::Value(static_cast<double>(m_endlessIndex));
    out["spawn_accum"] = Supersonic::Json::Value(m_spawnAccumulator);

    Supersonic::Json::Array queue;
    for (const Queued& item : m_queue) {
        Supersonic::Json::Object entry;
        entry["unit"] = Supersonic::Json::Value(item.unit);
        entry["hp_mult"] = Supersonic::Json::Value(static_cast<double>(item.hpMultiplier));
        entry["dmg_mult"] = Supersonic::Json::Value(static_cast<double>(item.damageMultiplier));
        queue.push_back(Supersonic::Json::Value(std::move(entry)));
    }
    out["spawn_queue"] = Supersonic::Json::Value(std::move(queue));
    return Supersonic::Json::Value(std::move(out));
}

void WaveDirector::FromSave(const Supersonic::Json::Value& saved, int aliveEnemies) {
    m_elapsed = saved["elapsed"].AsNumber(0.0);
    m_endlessIndex = static_cast<int>(saved["endless_index"].AsNumber(0.0));
    m_spawnAccumulator = saved["spawn_accum"].AsNumber(0.0);

    // Clamped at BOTH ends before the cast. m_nextWave is unsigned, so a
    // hand-edited or truncated -1 would become eighteen quintillion and skip
    // the schedule entirely; and a data re-tune that removes waves would leave
    // a saved index past the end of the array.
    const double next = saved["next_wave"].AsNumber(0.0);
    const double clamped = std::max(0.0, std::min(next, static_cast<double>(m_waves.size())));
    m_nextWave = static_cast<size_t>(clamped);

    m_queue.clear();
    for (const Supersonic::Json::Value& entry : saved["spawn_queue"].AsArray()) {
        Queued item;
        item.unit = entry["unit"].AsString(Ids::kRaider);
        item.hpMultiplier = entry["hp_mult"].AsFloat(1.0f);
        item.damageMultiplier = entry["dmg_mult"].AsFloat(1.0f);
        m_queue.push_back(std::move(item));
    }

    // Recomputed by the first step rather than restored. It is derived from the
    // schedule position and the queue, both of which have just been set, and a
    // stored copy could disagree with them.
    m_allSpawned = false;

    m_aliveEnemies = aliveEnemies;
}

void WaveDirector::OnEnemySpawned() { ++m_aliveEnemies; }

void WaveDirector::OnEnemyDied() {
    --m_aliveEnemies;
    CheckVictory();
}

void WaveDirector::CheckVictory() {
    if (!m_allSpawned || m_aliveEnemies > 0 || !m_state->IsPlaying()) return;
    m_bus->allWavesCleared.Emit();
    m_state->Win();
}

void WaveDirector::OnTownHallDestroyed() { m_state->Lose(); }

} // namespace WolfBrigade
