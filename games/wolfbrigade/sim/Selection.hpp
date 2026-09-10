#pragma once

#include <vector>

#include <glm/glm.hpp>

namespace WolfBrigade {

class Building;
class Damageable;
class Match;
class Unit;
struct ResourceNode;

// What the player currently has picked, from `scripts/systems/selection.gd`.
//
// One of two things, never both: a SET of player units, or a SINGLE player
// building. Picking either clears the other, which is the rule that makes the
// contextual bottom bar possible - it never has to decide what to show when a
// worker and a barracks are both selected, because that cannot happen.
//
// The ring a selected unit wears is presentation and is not here. What IS here
// is the set that decides which units an order reaches, which is the half with
// consequences.
//
// SCANS ON DEMAND, NEVER PER FRAME. Every question below walks the board, and
// they are asked on a tap or a drag release rather than on a tick. That is the
// same rule the units follow for their own scans, and for the same reason.
class Selection {
public:
    explicit Selection(Match& match);

    // Subscribes to deaths so a selection cannot outlive what it points at.
    // Called by whoever owns this once the bus is ready.
    void Connect();

    bool HasSelection() const { return !m_units.empty(); }
    const std::vector<Unit*>& Units() const { return m_units; }

    bool HasBuildingSelected() const { return m_building != nullptr; }
    Building* SelectedBuilding() const { return m_building; }

    // --- The operations, each of which announces EXACTLY ONE change --------
    //
    // That is a contract rather than an accident: the bottom bar rebuilds on
    // every announcement, and an operation that emitted twice would rebuild a
    // panel the player is mid-tap on.

    void SelectOnly(Unit* unit);
    void SelectBuilding(Building* building);

    // Everything inside the rectangle. Tested against the unit's POSITION - its
    // feet on the ground line - rather than the offset centre the point-pick
    // uses. The original does the same, and the difference is deliberate: a
    // marquee is drawn around ground positions, and a tap is aimed at a body.
    void BoxSelect(const glm::vec2& min, const glm::vec2& max);

    // The primary pick. A BUILDING UNDER THE POINT WINS, because buildings are
    // the interactive landmarks and a worker standing in a doorway must not
    // steal the tap. Then a unit, then nothing.
    void SelectAt(const glm::vec2& worldPos);

    void Clear();

    // --- Picking ----------------------------------------------------------

    // The nearest player unit whose body is under the point.
    //
    // The radius is the LARGER of the authored pick radius and 0.6 of the
    // unit's longest side, so a big unit is easy to hit and a small one still
    // meets a finger-sized target. The point is measured against the body's
    // centre, half a body height above the feet.
    Unit* UnitAt(const glm::vec2& worldPos) const;

    Building* BuildingAt(const glm::vec2& worldPos) const;

    // Something of the OTHER faction to attack: a unit by pick radius first,
    // then a building by footprint. Polymorphic, because an attack order does
    // not care which it got.
    Damageable* EnemyAt(const glm::vec2& worldPos) const;

    // The nearest node with anything left in it whose body is under the point,
    // for a gather order. By the authored pick radius alone, measured to a
    // point 30 px up the node, since its origin is at its base.
    ResourceNode* ResourceAt(const glm::vec2& worldPos) const;

private:
    void Add(Unit* unit);
    void DeselectAll();

    Match* m_match{nullptr};
    std::vector<Unit*> m_units;
    Building* m_building{nullptr};
};

} // namespace WolfBrigade
