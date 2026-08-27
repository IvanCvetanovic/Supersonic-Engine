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
    MoveSelectedTo(worldPos);
}

void Commands::ContextTap(const glm::vec2& worldPos) {
    // A building first: they are the interactive landmarks, and a tap on one is
    // never an order.
    if (Building* building = m_selection->BuildingAt(worldPos)) {
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
        unit->CommandMoveTo(glm::vec2(worldPos.x + offset, worldPos.y));
        ordered = true;
    }

    if (ordered) m_match->Bus().moveOrdered.Emit(worldPos);
}

} // namespace WolfBrigade
