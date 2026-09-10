#pragma once

#include <glm/glm.hpp>

namespace WolfBrigade {

class Building;
class Damageable;
class Match;
class Selection;
struct ResourceNode;

// Turning a pointer intent into orders, from `scripts/systems/commands.gd`.
//
// The thin layer between "the player touched here" and "these units go there".
// It owns no state beyond who the selection is - every question it asks is
// asked of the Selection, and every answer it gives is a call on a Unit.
//
// TWO ENTRY POINTS, AND THEY ARE NOT THE SAME, which is the whole reason this
// is its own file rather than three lines inside the input handler. A desktop
// right-click has already said "this is an order" by being the right button, so
// it only has to choose which order. A touch tap has said nothing at all, so it
// has to work out whether the player meant to select something or order it -
// in that order, and the order matters.
//
// Rally points (a ground order while a training building is selected) are the
// original's too, and arrive with production.
class Commands {
public:
    Commands(Match& match, Selection& selection);

    // Desktop right-click: attack an enemy under the point; else send the
    // selected workers to build an unfinished site, or to gather a node, under
    // the point; else move there. Work outranks walking, so a right-click ON
    // the job assigns it - and takes the worker off park - rather than parking
    // it beside the job.
    void OnCommandAt(const glm::vec2& worldPos);

    // Touch tap, context-sensitive and ordered by what the player most likely
    // meant. A building selects it - unless it is an unfinished site and
    // workers are in hand, which is a build order, touch parity with the
    // right-click. A friendly unit selects it. Then, only with something
    // already selected: an enemy is attacked, a node is gathered, or the ground
    // is moved to. A tap on empty ground with nothing selected CLEARS, which is
    // how a player puts the game down.
    void ContextTap(const glm::vec2& worldPos);

    // Spread around the point, centred on it.
    //
    // Along the lane the offset is `(i - (n-1)/2) * spacing`, so an odd count
    // puts one unit exactly on the point and an even count straddles it. A
    // formation that started AT the point and grew rightward would put the
    // player's click on the left flank instead of in the middle, which reads as
    // the order having missed.
    //
    // Across it, rows stagger by half a spacing - back, middle, front,
    // repeating - because a move order carries y and would otherwise stack the
    // whole group on the clicked row. The unit clamps each onto the band.
    void MoveSelectedTo(const glm::vec2& worldPos);

private:
    // Each of these is announced only when at least one live unit actually took
    // the order, so the ground ping is a confirmation rather than a decoration.
    // An order to an empty selection pings nothing, which is what tells the
    // player their selection is gone.
    void AttackSelected(Damageable* target);

    // Only WORKERS take these. False when none did, so a selection of soldiers
    // right-clicking a tree falls through to a move instead.
    bool BuildSelected(Building* site);
    bool GatherSelected(ResourceNode* node);

    Match* m_match{nullptr};
    Selection* m_selection{nullptr};
};

} // namespace WolfBrigade
