#include "sim/Lane.hpp"

#include <algorithm>
#include <cmath>

#include "sim/Unit.hpp"

namespace WolfBrigade {

namespace {
const std::vector<Unit*>& emptyList() {
    static const std::vector<Unit*> kEmpty;
    return kEmpty;
}
} // namespace

void Lane::Register(Unit* unit) {
    if (unit == nullptr) return;
    std::vector<Unit*>& list = m_lists[unit->Faction()];
    if (std::find(list.begin(), list.end(), unit) != list.end()) return;
    list.push_back(unit);
}

void Lane::Unregister(Unit* unit) {
    if (unit == nullptr) return;
    const auto it = m_lists.find(unit->Faction());
    if (it == m_lists.end()) return;
    std::vector<Unit*>& list = it->second;
    list.erase(std::remove(list.begin(), list.end(), unit), list.end());
}

const std::vector<Unit*>& Lane::ListOf(const std::string& faction) const {
    const auto it = m_lists.find(faction);
    return it == m_lists.end() ? emptyList() : it->second;
}

int Lane::CountOf(const std::string& faction) const {
    return static_cast<int>(ListOf(faction).size());
}

Unit* Lane::NearestEnemy(const std::string& faction, float x, float maxRange) const {
    // Anything that is not the player is treated as the enemy's enemy, which
    // mirrors Factions.units_group: a unit tagged with something nobody
    // recognises never joins the player's side by accident.
    const std::string enemy = (faction == Factions::kPlayer) ? Factions::kEnemy : Factions::kPlayer;

    Unit* best = nullptr;
    float bestDistance = maxRange;

    for (Unit* candidate : ListOf(enemy)) {
        if (candidate == nullptr || !candidate->IsAlive()) continue;
        const float distance = std::fabs(candidate->Position().x - x);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    }
    return best;
}

} // namespace WolfBrigade
