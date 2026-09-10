#pragma once

#include <glm/glm.hpp>

#include "sim/EventBus.hpp"

namespace WolfBrigade {

class Unit;

// Who the player is steering, from `scripts/systems/hero_control.gd`.
//
// The hero is ALWAYS under the player's control. The match possesses one at
// boot and on a restore, and a fresh one after a death; there is no selection
// behind it - buildings answer to proximity and the army to its squads - so
// possession changes only on a death and a respawn.
//
// It never reads raw input, which is the original's fourth rule: whoever owns
// the keys, the stick and the clicks turns them into these intents.
//
// Hero() is the leader the warband follows. The original publishes it as a
// static on the class, so it outlives the scene; here the Match answers
// World::Hero() from the HeroControl it owns.
//
// The camera follow is not here. It is presentation, and the port leaves it
// to the layer.
class HeroControl {
public:
    explicit HeroControl(EventBus& bus);
    ~HeroControl();

    HeroControl(const HeroControl&) = delete;
    HeroControl& operator=(const HeroControl&) = delete;

    // A living unit is possessed.
    bool IsActive() const;

    // The possessed unit while active, else null.
    Unit* ActiveUnit() const;

    // The leader ref: the possessed unit, null from a death until a respawn.
    Unit* Hero() const { return m_unit; }

    // --- Intents -----------------------------------------------------------

    // Steering, every frame while the keys are down.
    void SetKeyboardDir(const glm::vec2& dir);

    // The virtual stick, which outranks the keyboard while it is held.
    void SetStickDir(const glm::vec2& dir);

    // A desktop click: strike toward the point.
    void AttackAt(const glm::vec2& worldPos);

    // The touch ATTACK button: strike the nearest enemy in reach. Cooldown-
    // gated by the unit, so holding it attacks at the unit's own rate.
    void AttackPressed();

    // Q and E, or the on-screen buttons: cast ability slot `index`.
    void UseAbility(int index);

    // Seconds of cooldown left on slot `index`; 0 when ready, when nobody is
    // possessed, or when there is no such slot. The ability buttons poll this
    // to paint themselves.
    double AbilityCooldownLeft(int index) const;

    // --- Possession --------------------------------------------------------

    // Take control of `unit` - at boot, on a restore, on a respawn. Refuses
    // anything null, dead, or not data-flagged controllable, and keeps what
    // it had.
    void Possess(Unit* unit);

    // Drops the possession without a word, for a board about to be cleared:
    // the unit is going with it, and nobody is left to tell.
    void Forget();

    // The original's two node signals. activeChanged fires on every
    // possession and release; heroLost when the possessed unit dies, with
    // where it fell, and the match runs the respawn from there.
    Signal<bool> activeChanged;
    Signal<const glm::vec2&> heroLost;

private:
    void EndControl();
    void PushDir();
    void OnUnitDied(Unit* unit);

    EventBus* m_bus{nullptr};
    int m_diedConnection{0};

    Unit* m_unit{nullptr};
    glm::vec2 m_keyboardDir{0.0f};
    glm::vec2 m_stickDir{0.0f};
};

} // namespace WolfBrigade
