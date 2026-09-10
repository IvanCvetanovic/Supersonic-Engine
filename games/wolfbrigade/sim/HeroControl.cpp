#include "sim/HeroControl.hpp"

#include "sim/Unit.hpp"

namespace WolfBrigade {

HeroControl::HeroControl(EventBus& bus) : m_bus(&bus) {
    m_diedConnection = bus.unitDied.Connect([this](Unit* unit) { OnUnitDied(unit); });
}

HeroControl::~HeroControl() { m_bus->unitDied.Disconnect(m_diedConnection); }

bool HeroControl::IsActive() const { return m_unit != nullptr && m_unit->IsAlive(); }

Unit* HeroControl::ActiveUnit() const { return IsActive() ? m_unit : nullptr; }

void HeroControl::SetKeyboardDir(const glm::vec2& dir) {
    m_keyboardDir = dir;
    PushDir();
}

void HeroControl::SetStickDir(const glm::vec2& dir) {
    m_stickDir = dir;
    PushDir();
}

void HeroControl::AttackAt(const glm::vec2& worldPos) {
    if (IsActive()) m_unit->ControlledAttackAt(worldPos);
}

void HeroControl::AttackPressed() {
    if (IsActive()) m_unit->ControlledAttackAuto();
}

void HeroControl::UseAbility(int index) {
    if (IsActive()) m_unit->UseAbility(index);
}

double HeroControl::AbilityCooldownLeft(int index) const {
    if (!IsActive()) return 0.0;
    const auto& slots = m_unit->Stats().abilities;
    if (index < 0 || index >= static_cast<int>(slots.size())) return 0.0;
    return m_unit->AbilityCooldownLeft(slots[static_cast<size_t>(index)]);
}

void HeroControl::PushDir() {
    if (!IsActive()) return;
    m_unit->SetControlDir(m_stickDir != glm::vec2(0.0f) ? m_stickDir : m_keyboardDir);
}

void HeroControl::Possess(Unit* unit) {
    if (unit == nullptr || !unit->IsAlive() || !unit->Stats().controllable) return;
    EndControl();
    m_unit = unit;
    m_stickDir = glm::vec2(0.0f);
    unit->SetControlled(true);
    activeChanged.Emit(true);
}

void HeroControl::Forget() {
    m_unit = nullptr;
    m_keyboardDir = glm::vec2(0.0f);
    m_stickDir = glm::vec2(0.0f);
}

void HeroControl::EndControl() {
    if (m_unit == nullptr) return;
    m_unit->SetControlled(false);   // a dead unit ignores this and stays dead
    m_unit = nullptr;
    m_stickDir = glm::vec2(0.0f);
    activeChanged.Emit(false);
}

// A death releases control at once and hands the respawn, or the defeat, to
// whoever is listening - with where the hero fell, because the respawn
// building chosen is the one nearest to that.
void HeroControl::OnUnitDied(Unit* unit) {
    if (unit == nullptr || unit != m_unit) return;
    const glm::vec2 at = m_unit->Position();
    EndControl();
    heroLost.Emit(at);
}

} // namespace WolfBrigade
