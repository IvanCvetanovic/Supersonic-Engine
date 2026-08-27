#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace WolfBrigade {

// One signal, with however many listeners.
//
// Godot has this built in; C++ does not, and hand-writing a listener list per
// event would be the same twenty lines twenty times.
//
// Emission iterates a COPY of the slot list. A listener that disconnects itself
// - which the original's harnesses do routinely, and which a UI panel does
// every time it closes - would otherwise invalidate the iteration it is being
// called from. The copy costs an allocation on an event that fires a handful of
// times a second and buys the ability to reason about the callback at all.
template <typename... Args>
class Signal {
public:
    using Slot = std::function<void(Args...)>;

    // A handle, not an index: disconnecting one listener must not renumber the
    // others, or a panel closing takes the HUD's subscription with it.
    int Connect(Slot slot) {
        const int id = m_next++;
        m_slots.emplace_back(id, std::move(slot));
        return id;
    }

    void Disconnect(int id) {
        for (size_t i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].first != id) continue;
            m_slots.erase(m_slots.begin() + static_cast<ptrdiff_t>(i));
            return;
        }
    }

    void Emit(Args... args) const {
        const std::vector<std::pair<int, Slot>> snapshot = m_slots;
        for (const auto& [id, slot] : snapshot) {
            (void)id;
            if (slot) slot(args...);
        }
    }

    int ListenerCount() const { return static_cast<int>(m_slots.size()); }

private:
    std::vector<std::pair<int, Slot>> m_slots;
    int m_next{1};
};

// The single hub for global game events, from `scripts/core/event_bus.gd`.
//
// No gameplay logic - only the signals - so emitters and listeners never have
// to know about each other. That is the original's third architecture rule, and
// it is what makes the port's systems droppable one at a time: a signal with no
// listeners is a harmless no-op, so a system that has not been ported yet is
// simply one nobody is listening for.
//
// Signals nothing emits or consumes yet are declared anyway, exactly as the
// GDScript declares them, so a later slice wires to an API that is already
// there rather than changing this file.
struct EventBus {
    // A resource total changed. `resource` is an id like "wood".
    Signal<const std::string&, int> resourcesChanged;

    // An upgrade was researched, and future spawns are affected.
    Signal<const std::string&> upgradeResearched;

    // Something took damage: where, how much, and whose it was - the last for
    // the colour of the floating number. Emitted BEFORE the death check, so a
    // killing blow still shows its number.
    Signal<const glm::vec2&, int, const std::string&> damageDealt;

    // The selection was ordered somewhere, and a ground ping confirms the tap.
    // Emitted only when an order actually issues.
    Signal<const glm::vec2&> moveOrdered;
    Signal<const glm::vec2&> attackOrdered;

    // Building lifecycle. A raw pointer rather than a handle, because the thing
    // that owns the buildings is the thing listening - and it is the only one
    // that can outlive them.
    // A building was PLACED - a site, with no progress in it yet. Distinct
    // from completed, which is the same building some time later.
    Signal<class Building*> buildingPlaced;

    Signal<class Building*> buildingCompleted;
    Signal<class Building*> buildingDestroyed;

    // Placement mode opened or closed.
    //
    // This is the edge the gesture machine has been waiting for.
    // GestureMachine::SetPlacementMode has existed since the input slice with
    // nothing in the port ever calling it from real state - a flag only a test
    // ever set. In the original, build_placement emits this and the input
    // controller follows it, which is what stops a drag drawing a marquee while
    // a ghost is up.
    Signal<bool> placementActiveChanged;

    // A building finished training something; whoever owns the world spawns it
    // at the point given. The building deliberately does NOT create the unit
    // itself - it has no way to, and should not learn one.
    Signal<const std::string&, const glm::vec2&> unitTrained;

    // A unit entered the world, and a unit left it.
    //
    // These are two of the three this file's previous comment promised would
    // "arrive with the slices that emit them". This is that slice: the match
    // boot emits unitSpawned from inside its own spawn path, and a unit emits
    // unitDied from the death routine.
    //
    // unitSpawned is emitted by the SPAWN path only, never by a restore.
    // Snapshot::Restore recounts the enemies it rebuilt and hands the number to
    // WaveDirector::FromSave precisely because a restore bypasses the spawn
    // function - so a restore that also emitted would double-count and the
    // recount would silently repair it. The number of emissions is the only
    // thing that can see that mistake, which is why the director hears about an
    // enemy by listening here rather than by being told at the call site.
    //
    // A raw Unit*, for the same reason the building signals carry a Building*:
    // the thing that owns the units is the thing listening.
    Signal<class Unit*> unitSpawned;
    Signal<class Unit*> unitDied;

    // Wave and win/lose flow. The selection and placement signals arrive with
    // the input slice that emits them.
    Signal<int> waveStarted;
    Signal<int> waveCleared;
    Signal<> allWavesCleared;
    Signal<> gameWon;
    Signal<> gameLost;
};

} // namespace WolfBrigade
