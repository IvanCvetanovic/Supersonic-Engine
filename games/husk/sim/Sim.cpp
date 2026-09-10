// The schedule (src/sim/mod.rs): one SimStep is these 22 systems, chained, in
// this order. Bevy applies each system's deferred commands before the next one
// runs; applyCommands() is that sync point.

#include "World.hpp"

namespace husk {

void step(World& w) {
    using System = void (*)(World&);
    static constexpr System kSchedule[] = {
        updateWill,
        drainOrders,
        advanceResearch,
        applyPlacements,
        retargetRaiders,
        ensureFlowFields,
        acquireTargets,
        heroStats,
        stepMovement,
        siphonChannels,
        tickProduction,
        heroActions,
        tickBuffs,
        tickAttacks,
        towerAttacks,
        rollLootDrops,
        passiveRegen,
        runTriggers,
        xpAwards,
        heroRevive,
        updateSimHash,
    };
    for (System system : kSchedule) {
        system(w);
        w.applyCommands();
    }
    // advance_tick, the 22nd
    w.tick += 1;
}

} // namespace husk
