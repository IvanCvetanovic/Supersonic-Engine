#include "sim/Selection.hpp"

#include <algorithm>
#include <cmath>

#include "sim/Match.hpp"

namespace WolfBrigade {

namespace {

// Godot's Rect2.has_point: the near edges are inside, the far edges are NOT.
// Copied rather than approximated, because a marquee dragged to exactly a
// unit's x is the case a player hits constantly.
bool contains(const glm::vec2& min, const glm::vec2& max, const glm::vec2& point) {
    return point.x >= min.x && point.x < max.x && point.y >= min.y && point.y < max.y;
}

} // namespace

Selection::Selection(Match& match) : m_match(&match) {}

void Selection::Connect() {
    // A selection may not outlive what it points at. The port keeps dead
    // entities as tombstones, so nothing dangles - but a dead unit still in the
    // set would silently swallow every order the player gave it.
    m_match->Bus().unitDied.Connect([this](Unit* unit) {
        const auto it = std::find(m_units.begin(), m_units.end(), unit);
        if (it == m_units.end()) return;
        m_units.erase(it);
        m_match->Bus().selectionChanged.Emit();
    });

    m_match->Bus().buildingDestroyed.Connect([this](Building* building) {
        if (m_building != building) return;
        m_building = nullptr;
        m_match->Bus().selectionChanged.Emit();
    });
}

void Selection::SelectOnly(Unit* unit) {
    DeselectAll();
    Add(unit);
    m_match->Bus().selectionChanged.Emit();
}

void Selection::SelectBuilding(Building* building) {
    DeselectAll();
    if (building != nullptr) m_building = building;
    m_match->Bus().selectionChanged.Emit();
}

void Selection::BoxSelect(const glm::vec2& min, const glm::vec2& max) {
    DeselectAll();
    for (const auto& unit : m_match->Units()) {
        if (!unit->IsPlayer()) continue;
        if (contains(min, max, unit->Position())) Add(unit.get());
    }
    m_match->Bus().selectionChanged.Emit();
}

void Selection::SelectAt(const glm::vec2& worldPos) {
    if (Building* building = BuildingAt(worldPos)) {
        SelectBuilding(building);
        return;
    }
    if (Unit* unit = UnitAt(worldPos)) {
        SelectOnly(unit);
        return;
    }
    Clear();
}

void Selection::Clear() {
    DeselectAll();
    m_match->Bus().selectionChanged.Emit();
}

Unit* Selection::UnitAt(const glm::vec2& worldPos) const {
    const float pickRadius = m_match->WorldLayout().pickRadius;

    Unit* best = nullptr;
    float bestDistance = 0.0f;

    for (const auto& unit : m_match->Units()) {
        if (!unit->IsPlayer()) continue;

        // Half a body up from the feet: the point a player is aiming at is the
        // body, not the ground it stands on.
        const glm::vec2 centre =
            unit->Position() + glm::vec2(0.0f, -unit->Stats().bodySize.y * 0.5f);
        const float distance = glm::length(worldPos - centre);

        const float radius = std::max(
            pickRadius, std::max(unit->Stats().bodySize.x, unit->Stats().bodySize.y) * 0.6f);

        if (distance <= radius && (best == nullptr || distance < bestDistance)) {
            best = unit.get();
            bestDistance = distance;
        }
    }
    return best;
}

Building* Selection::BuildingAt(const glm::vec2& worldPos) const {
    for (const auto& building : m_match->Buildings()) {
        if (building->Faction() != Factions::kPlayer) continue;
        if (building->ContainsPoint(worldPos)) return building.get();
    }
    return nullptr;
}

Damageable* Selection::EnemyAt(const glm::vec2& worldPos) const {
    const float pickRadius = m_match->WorldLayout().pickRadius;

    Unit* best = nullptr;
    float bestDistance = 0.0f;

    for (const auto& unit : m_match->Units()) {
        if (unit->IsPlayer() || !unit->IsAlive()) continue;

        const glm::vec2 centre =
            unit->Position() + glm::vec2(0.0f, -unit->Stats().bodySize.y * 0.5f);
        const float distance = glm::length(worldPos - centre);

        const float radius = std::max(
            pickRadius, std::max(unit->Stats().bodySize.x, unit->Stats().bodySize.y) * 0.6f);

        if (distance <= radius && (best == nullptr || distance < bestDistance)) {
            best = unit.get();
            bestDistance = distance;
        }
    }
    if (best != nullptr) return best;

    // Units first, buildings second, and only then. A raider standing in front
    // of its own wall is what the player meant to hit.
    for (const auto& building : m_match->Buildings()) {
        if (building->Faction() == Factions::kPlayer || !building->IsAlive()) continue;
        if (building->ContainsPoint(worldPos)) return building.get();
    }
    return nullptr;
}

ResourceNode* Selection::ResourceAt(const glm::vec2& worldPos) const {
    const float pickRadius = m_match->WorldLayout().pickRadius;

    ResourceNode* best = nullptr;
    float bestDistance = 0.0f;

    for (const auto& node : m_match->Nodes()) {
        if (node->IsEmpty()) continue;

        const glm::vec2 centre = node->position + glm::vec2(0.0f, -30.0f);
        const float distance = glm::length(worldPos - centre);

        if (distance <= pickRadius && (best == nullptr || distance < bestDistance)) {
            best = node.get();
            bestDistance = distance;
        }
    }
    return best;
}

void Selection::Add(Unit* unit) {
    if (unit == nullptr) return;
    if (std::find(m_units.begin(), m_units.end(), unit) != m_units.end()) return;
    m_units.push_back(unit);
}

void Selection::DeselectAll() {
    m_units.clear();
    m_building = nullptr;
}

} // namespace WolfBrigade
