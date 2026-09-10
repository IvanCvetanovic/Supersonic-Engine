#pragma once

#include <string>

#include <glm/glm.hpp>

#include "core/Json.hpp"

namespace WolfBrigade {

struct EventBus;
class GameState;
class Lane;

// A neutral landmark either side can hold, from `scripts/entities/capture_point.gd`.
//
// A tug of war on presence. Player units inside the radius pull the capture
// progress toward +1, enemies toward -1, and both at once freeze it. At +1 the
// player holds it and it pays its bonus; at -1 the enemy holds it, which is pure
// denial - enemies have no economy to feed. Coming back across 0 drops it to
// neutral first, so every flip passes through a fight.
//
// Each point is a unique building with its own reward:
//   - income: trickles `resource` at `rate` a second while the player holds it;
//   - army_damage: multiplies the player's unit damage by `mult` while held.
//
// Presence is scanned on a ~8 Hz tick and measured along the lane only, like
// every combat question; the progress and the income accrue every step from the
// tick's verdict.
class CapturePoint {
public:
    static constexpr double kTick = 0.125;
    static constexpr const char* kBonusIncome = "income";
    static constexpr const char* kBonusArmyDamage = "army_damage";

    CapturePoint(GameState& state, EventBus& bus) : m_state(&state), m_bus(&bus) {}

    // Takes its army bonus with it, as the original's _exit_tree does. A point
    // that outlived its board without deregistering would keep multiplying the
    // next board's army.
    ~CapturePoint();

    CapturePoint(const CapturePoint&) = delete;
    CapturePoint& operator=(const CapturePoint&) = delete;

    // One entry of a level's capture_points. The legacy flat form,
    // {resource, rate}, reads as an income point.
    void Setup(const Supersonic::Json::Value& config);

    void SetPosition(const glm::vec2& position) { m_position = position; }
    glm::vec2 Position() const { return m_position; }

    // Frozen with the board once the run is decided. `lane` answers the
    // presence question: the original scans its two faction groups, and the
    // lane holds the same two lists.
    void Step(double delta, const Lane& lane);

    double Progress() const { return m_progress; }
    const std::string& Holder() const { return m_holder; }
    const std::string& BonusKind() const { return m_bonusKind; }
    const std::string& Resource() const { return m_resource; }
    double Rate() const { return m_rate; }
    double ArmyMult() const { return m_armyMult; }
    double Radius() const { return m_radius; }
    double CaptureTime() const { return m_captureTime; }

    // Sets the tug and lets the holder follow - what the original's harness
    // does with `progress = x` and `_update_holder()`.
    void ForceProgress(double progress);

    // Only the tug and the holder persist; the geometry comes back from the
    // level's data on every boot. Index-aligned to the level's list.
    Supersonic::Json::Value ToSave() const;

    // Clamps the saved tug, and re-registers a restored player-held banner.
    void FromSave(const Supersonic::Json::Value& saved);

private:
    // +1 only player units inside, -1 only enemies, 0 empty or contested.
    int ScanPresence(const Lane& lane) const;

    // The holder changes only at the extremes, or when the tug crosses back
    // through 0 to neutral - and every change is announced.
    void UpdateHolder();
    void SyncArmyBonus();

    GameState* m_state{nullptr};
    EventBus* m_bus{nullptr};
    glm::vec2 m_position{0.0f};

    std::string m_bonusKind{kBonusIncome};
    std::string m_resource{"wood"};
    double m_rate{1.0};
    double m_armyMult{1.0};
    double m_radius{150.0};
    double m_captureTime{5.0};

    double m_progress{0.0};   // -1 enemy .. +1 player
    std::string m_holder;     // "", or a faction

    // The scan clock RESETS to zero rather than subtracting the tick, as the
    // original's does - so at a 0.1 s step it scans every other step.
    double m_tickAccumulator{0.0};
    int m_presence{0};
    double m_incomeAccumulator{0.0};
};

} // namespace WolfBrigade
