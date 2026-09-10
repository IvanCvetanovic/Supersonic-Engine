#include "sim/CapturePoint.hpp"

#include <algorithm>

#include "sim/EventBus.hpp"
#include "sim/GameState.hpp"
#include "sim/Lane.hpp"
#include "sim/UnitStats.hpp"

namespace WolfBrigade {

CapturePoint::~CapturePoint() {
    if (m_state != nullptr) m_state->ClearArmyBonus(this);
}

void CapturePoint::Setup(const Supersonic::Json::Value& config) {
    m_radius = config["radius"].AsNumber(150.0);
    m_captureTime = std::max(config["capture_time"].AsNumber(5.0), 0.1);

    const Supersonic::Json::Value& bonus = config["bonus"];
    const bool flat = !bonus.IsObject() || bonus.AsObject().empty();
    if (flat) {
        m_bonusKind = kBonusIncome;
        m_resource = config["resource"].AsString("wood");
        m_rate = config["rate"].AsNumber(1.0);
        m_armyMult = 1.0;
    } else {
        m_bonusKind = bonus["kind"].AsString(kBonusIncome);
        m_resource = bonus["resource"].AsString("wood");
        m_rate = bonus["rate"].AsNumber(1.0);
        m_armyMult = std::max(bonus["mult"].AsNumber(1.0), 0.01);
    }
}

void CapturePoint::Step(double delta, const Lane& lane) {
    if (!m_state->IsPlaying()) return;

    m_tickAccumulator += delta;
    if (m_tickAccumulator >= kTick) {
        m_tickAccumulator = 0.0;
        m_presence = ScanPresence(lane);
    }

    if (m_presence != 0) {
        const double before = m_progress;
        m_progress = std::clamp(
            m_progress + static_cast<double>(m_presence) * delta / m_captureTime, -1.0, 1.0);
        if (m_progress != before) UpdateHolder();
    }

    if (m_holder == Factions::kPlayer && m_bonusKind == kBonusIncome) {
        m_incomeAccumulator += m_rate * delta;
        if (m_incomeAccumulator >= 1.0) {
            const int whole = static_cast<int>(m_incomeAccumulator);
            m_incomeAccumulator -= static_cast<double>(whole);
            m_state->Add(m_resource, whole);
        }
    }
}

int CapturePoint::ScanPresence(const Lane& lane) const {
    // Along the lane only: the band's rows are scenery to this, as to every
    // combat question. The nearest enemy OF the enemy is a player unit.
    const float reach = static_cast<float>(m_radius);
    const bool player = lane.NearestEnemy(Factions::kEnemy, m_position.x, reach) != nullptr;
    const bool enemy = lane.NearestEnemy(Factions::kPlayer, m_position.x, reach) != nullptr;
    if (player == enemy) return 0;
    return player ? 1 : -1;
}

void CapturePoint::UpdateHolder() {
    std::string next = m_holder;
    if (m_progress >= 1.0) {
        next = Factions::kPlayer;
    } else if (m_progress <= -1.0) {
        next = Factions::kEnemy;
    } else if ((m_holder == Factions::kPlayer && m_progress <= 0.0) ||
               (m_holder == Factions::kEnemy && m_progress >= 0.0)) {
        next.clear();
    }

    if (next != m_holder) {
        m_holder = next;
        SyncArmyBonus();
        m_bus->captureChanged.Emit(this, m_holder);
    }
}

void CapturePoint::SyncArmyBonus() {
    if (m_bonusKind == kBonusArmyDamage && m_holder == Factions::kPlayer) {
        m_state->SetArmyBonus(this, m_armyMult);
    } else {
        m_state->ClearArmyBonus(this);
    }
}

void CapturePoint::ForceProgress(double progress) {
    m_progress = progress;
    UpdateHolder();
}

Supersonic::Json::Value CapturePoint::ToSave() const {
    Supersonic::Json::Object out;
    out["progress"] = Supersonic::Json::Value(m_progress);
    out["holder"] = Supersonic::Json::Value(m_holder);
    return Supersonic::Json::Value(std::move(out));
}

void CapturePoint::FromSave(const Supersonic::Json::Value& saved) {
    m_progress = std::clamp(saved["progress"].AsNumber(0.0), -1.0, 1.0);
    m_holder = saved["holder"].AsString("");
    SyncArmyBonus();
}

} // namespace WolfBrigade
