#include "sim/GameState.hpp"

#include <algorithm>
#include <cmath>

namespace WolfBrigade {

namespace {

// Godot's int(round(x)), which rounds half away from zero rather than to even.
// std::lround does the same; std::round returns a double that then truncates,
// which is the same answer by a longer route.
int roundToInt(float value) { return static_cast<int>(std::lround(value)); }

} // namespace

void GameState::Reset() {
    m_resources.clear();
    m_upgrades.clear();

    const float multiplier = DifficultyMultiplier("starting_resources_mult");

    for (const auto& [resource, value] :
         m_data->Economy()["starting_resources"].AsObject()) {
        const float base = static_cast<float>(static_cast<int>(value.AsNumber()));
        m_resources[resource] = roundToInt(base * multiplier) + StartingResourceBonus(resource);
    }

    m_currentWave = 0;
    m_phase = Phase::Playing;

    // Announced rather than assumed. A HUD built before this runs shows the
    // boot defaults until something tells it otherwise, and nothing polls.
    //
    // The ORDER differs from the original - Godot iterates a Dictionary in
    // insertion order and this is a std::map, so food is announced before wood.
    // Nothing downstream depends on it: each emission names its own resource.
    for (const auto& [resource, amount] : m_resources) {
        m_bus->resourcesChanged.Emit(resource, amount);
    }
}

int GameState::Amount(const std::string& resource) const {
    const auto it = m_resources.find(resource);
    return it == m_resources.end() ? 0 : it->second;
}

void GameState::Add(const std::string& resource, int n) {
    const int total = Amount(resource) + n;
    m_resources[resource] = total;
    m_bus->resourcesChanged.Emit(resource, total);
}

bool GameState::CanAfford(const Cost& cost) const {
    for (const auto& [resource, price] : cost) {
        if (Amount(resource) < price) return false;
    }
    return true;
}

bool GameState::TrySpend(const Cost& cost) {
    // Checked in full before anything is deducted. A loop that spent as it went
    // would leave a player who could afford the wood but not the food having
    // paid the wood for nothing.
    if (!CanAfford(cost)) return false;

    for (const auto& [resource, price] : cost) {
        const int total = Amount(resource) - price;
        m_resources[resource] = total;
        m_bus->resourcesChanged.Emit(resource, total);
    }
    return true;
}

std::string GameState::CurrentDifficulty() const {
    if (!m_difficulty.empty() && m_data->DifficultyPreset(m_difficulty).IsObject() &&
        !m_data->DifficultyPreset(m_difficulty).AsObject().empty()) {
        return m_difficulty;
    }
    return m_data->DifficultyDefault();
}

float GameState::DifficultyMultiplier(const std::string& key) const {
    return m_data->DifficultyPreset(CurrentDifficulty())[key].AsFloat(1.0f);
}

int GameState::ScaleWaveCount(int baseCount) const {
    if (baseCount <= 0) return 0;

    // At least one. Easy multiplies by 0.7, and a wave listing a single brute
    // would otherwise round to zero - a scripted wave quietly losing its
    // centrepiece rather than being easier.
    return std::max(1, roundToInt(static_cast<float>(baseCount) *
                                  DifficultyMultiplier("wave_size_mult")));
}

bool GameState::IsResearched(const std::string& id) const {
    const auto it = m_upgrades.find(id);
    return it != m_upgrades.end() && it->second;
}

void GameState::MarkResearched(const std::string& id) { m_upgrades[id] = true; }

void GameState::Win() {
    if (m_phase != Phase::Playing) return;
    m_phase = Phase::Won;
    m_bus->gameWon.Emit();
}

void GameState::Lose() {
    if (m_phase != Phase::Playing) return;
    m_phase = Phase::Lost;
    m_bus->gameLost.Emit();
}

int GameState::StartingResourceBonus(const std::string& resource) const {
    int total = 0;
    for (const auto& [id, definition] : m_data->MetaUpgrades().AsObject()) {
        const auto owned = m_metaLevels.find(id);
        if (owned == m_metaLevels.end() || owned->second <= 0) continue;

        // "starting_resources" is a pseudo-entity: an upgrade that grants a
        // resource at the start of a run targets it the same way one that
        // toughens soldiers targets "soldier". Only this function reads it.
        const float perLevel =
            definition["effects"]["starting_resources"][resource].AsFloat(0.0f);
        total += roundToInt(perLevel * static_cast<float>(owned->second));
    }
    return total;
}

} // namespace WolfBrigade
