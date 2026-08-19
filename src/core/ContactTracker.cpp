#include "core/ContactTracker.hpp"

#include <algorithm>

namespace Supersonic {

ContactTracker::PairKey ContactTracker::keyFor(entt::entity a, entt::entity b) {
    auto lo = static_cast<uint64_t>(entt::to_integral(a));
    auto hi = static_cast<uint64_t>(entt::to_integral(b));
    // Ordered, so the same pair arriving as (a,b) one frame and (b,a) the next
    // is recognised as the same pair rather than reported as an exit and an
    // immediate re-enter. The solver does not promise a stable order: the
    // broadphase sorts its proxies on X, so two bodies swapping positions
    // swaps their order in every pair they appear in.
    if (lo > hi) std::swap(lo, hi);
    return (lo << 32) | hi;
}

void ContactTracker::Update(const std::vector<PhysicsSystem::Contact>& contacts) {
    m_events.clear();
    m_current.clear();

    const auto record = [this](entt::entity self, entt::entity other,
                               const glm::vec3& normal, bool isTrigger, Phase phase) {
        if (self == entt::null) return;
        m_events[self].push_back(Event{other, normal, isTrigger, phase});
    };

    for (const auto& contact : contacts) {
        if (contact.a == entt::null || contact.b == entt::null) continue;

        const PairKey key = keyFor(contact.a, contact.b);

        // A pair can be reported more than once in a frame, because the fixed
        // step may run several times and the app concatenates each step's
        // contacts. Only the first occurrence decides the phase; the rest are
        // the same touch continuing within one frame.
        if (!m_current.insert(key).second) continue;

        const Phase phase = m_previous.count(key) ? Phase::Stay : Phase::Enter;

        // The normal points from a toward b, so b is pushed along +normal.
        // Flipping it for b means a script always reads a normal pointing away
        // from itself, which is what "which way did I get hit from" needs.
        record(contact.a, contact.b, contact.normal, contact.isTrigger, phase);
        record(contact.b, contact.a, -contact.normal, contact.isTrigger, phase);
    }

    // Anything that was touching last frame and is not now has stopped.
    for (const PairKey key : m_previous) {
        if (m_current.count(key)) continue;

        const auto a = static_cast<entt::entity>(static_cast<uint32_t>(key >> 32));
        const auto b = static_cast<entt::entity>(static_cast<uint32_t>(key & 0xFFFFFFFFull));

        // No normal to report: the bodies are apart, so any direction would be
        // invented. Zero is the honest answer and scripts can test for it.
        record(a, b, glm::vec3(0.0f), false, Phase::Exit);
        record(b, a, glm::vec3(0.0f), false, Phase::Exit);
    }

    m_previous = m_current;
}

const std::vector<ContactTracker::Event>* ContactTracker::For(entt::entity entity) const {
    const auto it = m_events.find(entity);
    return it == m_events.end() ? nullptr : &it->second;
}

size_t ContactTracker::CountFor(entt::entity entity) const {
    const auto* events = For(entity);
    return events ? events->size() : 0;
}

void ContactTracker::Clear() {
    m_events.clear();
    m_previous.clear();
    m_current.clear();
}

} // namespace Supersonic
