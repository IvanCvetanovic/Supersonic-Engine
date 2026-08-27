#pragma once

#include <glm/glm.hpp>

namespace WolfBrigade {

class Damageable;
class Match;
class Selection;

// Turning a pointer intent into orders, from `scripts/systems/commands.gd`.
//
// The thin layer between "the player touched here" and "these units go there".
// It owns no state beyond who the selection is - every question it asks is
// asked of the Selection, and every answer it gives is a call on a Unit.
//
// TWO ENTRY POINTS, AND THEY ARE NOT THE SAME, which is the whole reason this
// is its own file rather than three lines inside the input handler. A desktop
// right-click has already said "this is an order" by being the right button, so
// it only has to choose between attacking and moving. A touch tap has said
// nothing at all, so it has to work out whether the player meant to select
// something, attack something, or move - in that order, and the order matters.
class Commands {
public:
    Commands(Match& match, Selection& selection);

    // Desktop right-click: attack an enemy under the point, else move there.
    void OnCommandAt(const glm::vec2& worldPos);

    // Touch tap, context-sensitive and ordered by what the player most likely
    // meant: a building selects it, a friendly unit selects it, then - only
    // with something already selected - an enemy is attacked or the ground is
    // moved to. A tap on empty ground with nothing selected CLEARS, which is
    // how a player puts the game down.
    void ContextTap(const glm::vec2& worldPos);

    // Spread along the lane, centred on the point.
    //
    // The offset is `(i - (n-1)/2) * spacing`, so an odd count puts one unit
    // exactly on the point and an even count straddles it. A formation that
    // started AT the point and grew rightward would put the player's click on
    // the left flank instead of in the middle, which reads as the order having
    // missed.
    //
    // Only x is spread. This is a single lane.
    void MoveSelectedTo(const glm::vec2& worldPos);

private:
    // Announced only when at least one live unit actually took the order, so
    // the ground ping is a confirmation rather than a decoration. An order to
    // an empty selection pings nothing, which is what tells the player their
    // selection is gone.
    void AttackSelected(Damageable* target);

    Match* m_match{nullptr};
    Selection* m_selection{nullptr};
};

} // namespace WolfBrigade
