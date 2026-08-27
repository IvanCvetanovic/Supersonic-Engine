#include "sim/BuildPlacement.hpp"

#include <algorithm>
#include <cmath>

#include "sim/Match.hpp"

namespace WolfBrigade {

BuildPlacement::BuildPlacement(Match& match) : m_match(&match) {}

void BuildPlacement::Begin(const std::string& buildingId) {
    // A swap, not a no-op: cancelling first is what makes the active-changed
    // signal go false and then true rather than staying true through a change
    // of building.
    if (m_active) Cancel();

    const Supersonic::Json::Value& row = m_match->Data().Building(buildingId);
    if (!row.IsObject() || row.AsObject().empty()) return;

    m_stats = Upgrades::ForBuilding(m_match->Data(), m_match->Run(), m_match->PlayerProfile(),
                                   buildingId);
    m_active = true;

    // The ghost is created at the middle of the world, and the original's
    // update runs immediately on it - so a placement that is confirmed without
    // the pointer ever moving lands at the centre rather than at the origin.
    Update(m_match->WorldLayout().width * 0.5f);

    m_match->Bus().placementActiveChanged.Emit(true);
}

void BuildPlacement::Update(float worldX) {
    if (!m_active) return;

    const float half = m_stats.bodySize.x * 0.5f;
    const float width = m_match->WorldLayout().width;

    m_candidateX = std::min(std::max(worldX, half), width - half);
    m_valid = IsValidAt(m_candidateX);
}

void BuildPlacement::Confirm(float worldX) {
    if (!m_active) return;

    // Re-clamped and re-validated BEFORE anything is spent, so the price is
    // paid for the spot the building actually lands on.
    Update(worldX);

    // An invalid spot leaves placement RUNNING. The player slides over and
    // tries again; cancelling here would make a mistap cost them the menu.
    if (!m_valid) return;

    Cost cost;
    for (const auto& [resource, amount] : m_stats.cost) cost[resource] = amount;

    // Became unaffordable since placement began - a wave arrived and the
    // player spent elsewhere. Cancel rather than place a building nobody paid
    // for.
    if (!m_match->Run().TrySpend(cost)) {
        Cancel();
        return;
    }

    Place(m_candidateX);
    Cancel();
}

void BuildPlacement::Cancel() {
    if (!m_active) return;

    m_active = false;
    m_valid = false;
    m_match->Bus().placementActiveChanged.Emit(false);
}

bool BuildPlacement::IsValidAt(float x) const {
    const float half = m_stats.bodySize.x * 0.5f;
    const float lo = x - half;
    const float hi = x + half;

    for (const auto& building : m_match->Buildings()) {
        if (building->Faction() != Factions::kPlayer) continue;
        if (!building->IsAlive()) continue;

        const Building::Rect footprint = building->Footprint();
        if (hi > footprint.min.x && lo < footprint.max.x) return false;
    }
    return true;
}

Unit* BuildPlacement::NearestWorker(float x) const {
    Unit* bestIdle = nullptr;
    float bestIdleDistance = 0.0f;
    Unit* bestAny = nullptr;
    float bestAnyDistance = 0.0f;

    for (const auto& unit : m_match->Units()) {
        if (!unit->IsAlive()) continue;
        if (!unit->IsPlayer()) continue;
        if (unit->Stats().behavior != Unit::kWorker) continue;

        const float distance = std::fabs(unit->Position().x - x);

        if (bestAny == nullptr || distance < bestAnyDistance) {
            bestAny = unit.get();
            bestAnyDistance = distance;
        }
        if (unit->CurrentState() == Unit::State::Idle &&
            (bestIdle == nullptr || distance < bestIdleDistance)) {
            bestIdle = unit.get();
            bestIdleDistance = distance;
        }
    }

    return bestIdle != nullptr ? bestIdle : bestAny;
}

void BuildPlacement::Place(float x) {
    // Not pre-placed: it starts as a site with no progress in it, which is what
    // gives a worker something to pour time into.
    Building* site = m_match->PlaceBuilding(m_stats, false,
                                            glm::vec2(x, m_match->WorldLayout().groundY));

    m_match->Bus().buildingPlaced.Emit(site);

    // Sent, not assigned: any idle worker will resume an abandoned site later,
    // which is the anti-deadlock rule the worker slice already carries. This
    // only decides who starts.
    if (Unit* worker = NearestWorker(x)) worker->CommandBuild(site);
}

} // namespace WolfBrigade
