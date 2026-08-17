#pragma once

#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Supersonic {

class PhysicsSystem {
public:
    // One resolved (or merely detected, for triggers) overlap. Returned rather
    // than stored, because systems here are stateless free functions over the
    // registry and a file-static contact list would not survive two scenes.
    struct Contact {
        entt::entity a{entt::null};
        entt::entity b{entt::null};

        // Points from a toward b, so b is pushed along +normal.
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        float penetration{0.0f};

        // Trigger overlaps are reported and deliberately not resolved.
        bool isTrigger{false};
    };

    // Advances one fixed step. outContacts, when given, is cleared and filled
    // with this step's overlaps - the editor uses it to show that collision is
    // actually happening rather than requiring you to infer it from motion.
    static void Update(entt::registry& registry, float deltaTime,
                       std::vector<Contact>* outContacts = nullptr);

    // Broadphase, exposed for testing. Returns candidate pairs by index into
    // the proxy list; a pair here has overlapping bounds and nothing more.
    struct Proxy {
        entt::entity entity{entt::null};

        // Index back into the caller's body list. Carried here because the sweep
        // sorts the proxies, so a pair's indices refer to the sorted order and
        // there would otherwise be no way back that is not a search.
        size_t index{0};
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
        float inverseMass{0.0f};
        bool isTrigger{false};
    };

    static void SweepAndPrune(std::vector<Proxy>& proxies,
                              std::vector<std::pair<size_t, size_t>>& outPairs);
};

} // namespace Supersonic
