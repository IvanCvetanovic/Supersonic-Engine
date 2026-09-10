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

    // The ghost is created at the middle of the world on the band's top row,
    // and the original's update runs immediately on it - so a placement that
    // is confirmed without the pointer ever moving lands there rather than at
    // the origin.
    const Match::Layout& layout = m_match->WorldLayout();
    Update(glm::vec2(layout.width * 0.5f, layout.groundY));

    m_match->Bus().placementActiveChanged.Emit(true);
}

void BuildPlacement::Update(const glm::vec2& worldPos) {
    if (!m_active) return;

    const float half = m_stats.bodySize.x * 0.5f;
    const Match::Layout& layout = m_match->WorldLayout();

    m_candidate.x = std::min(std::max(worldPos.x, half), layout.width - half);
    m_candidate.y =
        std::min(std::max(worldPos.y, layout.groundY), layout.groundY + layout.laneDepth);
    m_valid = IsValidAt(m_candidate.x, m_candidate.y);
}

void BuildPlacement::Confirm(const glm::vec2& worldPos) {
    if (!m_active) return;

    // Re-clamped and re-validated BEFORE anything is spent, so the price is
    // paid for the spot the building actually lands on.
    Update(worldPos);

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

    Place(m_candidate);
    Cancel();
}

void BuildPlacement::Cancel() {
    if (!m_active) return;

    m_active = false;
    m_valid = false;
    m_match->Bus().placementActiveChanged.Emit(false);
}

bool BuildPlacement::IsValidAt(float x, float y) const {
    const float half = m_stats.bodySize.x * 0.5f;
    const float lo = x - half;
    const float hi = x + half;
    const float gap = m_match->WorldLayout().buildingRowGap;

    for (const auto& building : m_match->Buildings()) {
        if (building->Faction() != Factions::kPlayer) continue;
        if (!building->IsAlive()) continue;

        const Building::Rect footprint = building->Footprint();
        if (hi > footprint.min.x && lo < footprint.max.x &&
            std::fabs(y - building->Position().y) < gap) {
            return false;
        }
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

void BuildPlacement::Place(const glm::vec2& at) {
    // Not pre-placed: it starts as a site with no progress in it, which is what
    // gives a worker something to pour time into.
    Building* site = m_match->PlaceBuilding(m_stats, false, at);

    m_match->Bus().buildingPlaced.Emit(site);

    // Sent, not assigned: while the economy's auto_assist_build is on, any idle
    // worker resumes an abandoned site later. This only decides who starts.
    if (Unit* worker = NearestWorker(at.x)) worker->CommandBuild(site);
}

} // namespace WolfBrigade
