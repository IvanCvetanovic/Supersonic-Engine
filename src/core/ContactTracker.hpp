#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/PhysicsSystem.hpp"

namespace Supersonic {

// Turns a frame's contact list into something a script can ask questions of.
//
// PhysicsSystem has always produced contacts. SupersonicApp accumulated them
// across the frame's fixed steps and handed them to the editor, which displayed
// a COUNT - and nothing else read them. A trigger volume therefore could not
// fire: the engine knew the player had entered it, computed the overlap, and
// threw the fact away. Non-resolving trigger volumes were a feature with no
// consumer.
//
// Two things have to be added to a raw contact list before gameplay can use it.
//
// It has to be indexed by entity, because a script runs per entity and asking
// "what did I touch" should not be a scan of every contact in the scene.
//
// And it has to be diffed against the previous frame, because "started
// touching" and "stopped touching" are the events games are written against.
// A list of what is currently overlapping cannot express either: a door that
// opens on Enter would re-open every frame, and one that closes on Exit would
// never close at all, because the contact is simply absent rather than reported
// as gone.
class ContactTracker {
public:
    enum class Phase : int {
        Enter = 0,  // first frame of this pair touching
        Stay  = 1,  // still touching
        Exit  = 2,  // touched last frame, not this one
    };

    struct Event {
        entt::entity other{entt::null};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        bool isTrigger{false};
        Phase phase{Phase::Enter};
    };

    // Call once per frame with everything that touched during it. Both halves
    // of each pair get an event, with the normal flipped for the second, so a
    // script always reads a normal pointing away from itself.
    void Update(const std::vector<PhysicsSystem::Contact>& contacts);

    const std::vector<Event>* For(entt::entity entity) const;
    size_t CountFor(entt::entity entity) const;

    // Drops everything, including the previous frame's pairs, so the next
    // Update reports Enter rather than Stay. Needed on Stop and on scene load:
    // carrying pairs across would report an exit for entities that no longer
    // exist, or a stay for a pair that is only touching in the new scene.
    void Clear();

    size_t TrackedEntityCount() const { return m_events.size(); }

private:
    // Ordered so the key is the same whichever way round the pair arrives.
    using PairKey = uint64_t;
    static PairKey keyFor(entt::entity a, entt::entity b);

    std::unordered_map<entt::entity, std::vector<Event>> m_events;
    std::unordered_set<PairKey> m_previous;
    std::unordered_set<PairKey> m_current;
};

} // namespace Supersonic
