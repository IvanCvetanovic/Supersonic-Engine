#pragma once

#include <string>

#include "sim/Building.hpp"
#include "sim/GameData.hpp"
#include "sim/Unit.hpp"
#include "sim/World.hpp"

// Population, from the supply block of `scripts/core/game_state.gd`.
//
// DERIVED on every ask, never stored. The cap is what the player's COMPLETE
// buildings provide - the Town Hall and the farms. The use is every living
// player unit plus every unit waiting in a player building's training queue:
// a queued unit RESERVES its space, or a player could queue past the cap and
// have the units arrive over it. Deriving it is what keeps saves and restarts
// consistent without anything to restore.
//
// The original asks the scene tree for its groups; this asks the World for the
// same two lists. Asked on clicks and on the auto-train decision tick, never
// per frame per unit - the original's rule for it too.
namespace WolfBrigade::Supply {

// What one of this unit occupies, from the data - which is where a QUEUED unit
// is priced, since it has no stats yet. One when the row does not say.
inline int Of(const GameData& data, const std::string& unitId) {
    return static_cast<int>(data.Unit(unitId)["supply"].AsNumber(1.0));
}

inline int Cap(const World& world) {
    int cap = 0;
    for (const Building* building : world.PlayerBuildings()) {
        if (building != nullptr && building->IsComplete()) cap += building->Stats().supply;
    }
    return cap;
}

inline int Used(const World& world, const GameData& data) {
    int used = 0;
    for (const Unit* unit : world.PlayerUnits()) {
        if (unit != nullptr && unit->IsAlive()) used += unit->Stats().supply;
    }
    // A destroyed building's queue went with it. In the original it leaves the
    // group when it is freed; nothing is freed here, so it is asked.
    for (const Building* building : world.PlayerBuildings()) {
        if (building == nullptr || !building->IsAlive()) continue;
        for (const std::string& unitId : building->TrainQueue()) used += Of(data, unitId);
    }
    return used;
}

// Room for one more of this unit. Enforced when training is queued; enemies
// never ask.
inline bool HasRoomFor(const World& world, const GameData& data, const std::string& unitId) {
    return Used(world, data) + Of(data, unitId) <= Cap(world);
}

} // namespace WolfBrigade::Supply
