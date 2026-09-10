#include "sim/Commands.hpp"

#include "sim/Match.hpp"
#include "sim/Selection.hpp"

namespace WolfBrigade {

Commands::Commands(Match& match, Selection& selection)
    : m_match(&match), m_selection(&selection) {}

void Commands::OnCommandAt(const glm::vec2& worldPos) {
    if (Damageable* enemy = m_selection->EnemyAt(worldPos)) {
        AttackSelected(enemy);
        return;
    }

    // Work before walking. The build order is also the only way to resume a
    // stalled site while idle workers do not volunteer (economy.json's
    // auto_assist_build).
    Building* building = m_selection->BuildingAt(worldPos);
    if (building != nullptr && !building->IsComplete() && BuildSelected(building)) return;

    ResourceNode* node = m_selection->ResourceAt(worldPos);
    if (node != nullptr && GatherSelected(node)) return;

    MoveSelectedTo(worldPos);
}

void Commands::ContextTap(const glm::vec2& worldPos) {
    // A building first: they are the interactive landmarks, and a tap on one is
    // an order only when it is a site and there are workers to send.
    if (Building* building = m_selection->BuildingAt(worldPos)) {
        if (!building->IsComplete() && BuildSelected(building)) return;
        m_selection->SelectBuilding(building);
        return;
    }
    if (Unit* unit = m_selection->UnitAt(worldPos)) {
        m_selection->SelectOnly(unit);
        return;
    }

    // Nothing under the tap and nothing in hand: the player is putting the game
    // down, not issuing an order into the void.
    if (!m_selection->HasSelection()) {
        m_selection->Clear();
        return;
    }

    if (Damageable* enemy = m_selection->EnemyAt(worldPos)) {
        AttackSelected(enemy);
        return;
    }

    ResourceNode* node = m_selection->ResourceAt(worldPos);
    if (node != nullptr && GatherSelected(node)) return;

    MoveSelectedTo(worldPos);
}

void Commands::AttackSelected(Damageable* target) {
    bool ordered = false;
    for (Unit* unit : m_selection->Units()) {
        if (unit == nullptr || !unit->IsAlive()) continue;
        unit->CommandAttack(target);
        ordered = true;
    }
    if (ordered) m_match->Bus().attackOrdered.Emit(target->Position());
}

bool Commands::BuildSelected(Building* site) {
    bool ordered = false;
    for (Unit* unit : m_selection->Units()) {
        if (unit == nullptr || !unit->IsAlive()) continue;
        if (unit->Stats().behavior != Unit::kWorker) continue;
        unit->CommandBuild(site);
        ordered = true;
    }
    if (ordered) m_match->Bus().moveOrdered.Emit(site->Position());
    return ordered;
}

bool Commands::GatherSelected(ResourceNode* node) {
    bool ordered = false;
    for (Unit* unit : m_selection->Units()) {
        if (unit == nullptr || !unit->IsAlive()) continue;
        if (unit->Stats().behavior != Unit::kWorker) continue;
        unit->CommandGather(node);
        ordered = true;
    }
    if (ordered) m_match->Bus().moveOrdered.Emit(node->position);
    return ordered;
}

void Commands::MoveSelectedTo(const glm::vec2& worldPos) {
    const std::vector<Unit*>& units = m_selection->Units();
    const int count = static_cast<int>(units.size());
    if (count == 0) return;

    const float spacing = m_match->WorldLayout().formationSpacing;

    bool ordered = false;
    for (int i = 0; i < count; ++i) {
        Unit* unit = units[static_cast<size_t>(i)];
        if (unit == nullptr || !unit->IsAlive()) continue;

        const float offset =
            (static_cast<float>(i) - static_cast<float>(count - 1) * 0.5f) * spacing;
        const float row = (static_cast<float>(i % 3) - 1.0f) * spacing * 0.5f;
        unit->CommandMoveTo(glm::vec2(worldPos.x + offset, worldPos.y + row));
        ordered = true;
    }

    if (ordered) m_match->Bus().moveOrdered.Emit(worldPos);
}

} // namespace WolfBrigade
