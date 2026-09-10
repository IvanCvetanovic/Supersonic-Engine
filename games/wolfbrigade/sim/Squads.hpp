#pragma once

#include <vector>

#include "sim/Unit.hpp"
#include "sim/UnitStats.hpp"
#include "sim/World.hpp"

// Group-level army control, from `scripts/systems/squads.gd`.
//
// Every can_follow player unit belongs to the GARRISON, which holds and
// defends its post, or to the WARBAND, which runs with the hero. A recruit
// joins its building's squad; the two horn buttons flip everyone at once.
// Units react on their own next thinking tick - these only relabel.
//
// Asked on horn presses and bar rebuilds, which are events, never per frame.
namespace WolfBrigade::Squads {

// How many followers are in each squad, for the horn buttons' labels.
struct Tally {
    int garrison{0};
    int warband{0};
};

// Living player units that can follow. A worker cannot and the hero leads, so
// neither is ever counted or relabelled.
inline std::vector<Unit*> Followers(const World& world) {
    std::vector<Unit*> out;
    for (Unit* unit : world.PlayerUnits()) {
        if (unit != nullptr && unit->Stats().canFollow && unit->IsAlive()) out.push_back(unit);
    }
    return out;
}

// Everyone follows the hero.
inline void RallyAll(const World& world) {
    for (Unit* unit : Followers(world)) unit->SetSquad(kWarband);
}

// Everyone goes back to their post.
inline void SendHome(const World& world) {
    for (Unit* unit : Followers(world)) unit->SetSquad(kGarrison);
}

inline Tally Counts(const World& world) {
    Tally tally;
    for (const Unit* unit : Followers(world)) {
        if (unit->Squad() == kGarrison) ++tally.garrison;
        if (unit->Squad() == kWarband) ++tally.warband;
    }
    return tally;
}

} // namespace WolfBrigade::Squads
