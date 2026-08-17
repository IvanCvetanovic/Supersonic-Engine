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

    // ---- Queries ----
    //
    // Gameplay had no way to ask the world a question: no ground check, no
    // "what am I looking at", no trigger radius. Raycast in Raycast.cpp exists
    // for editor picking and tests RENDER bounds, which is the wrong shape - a
    // character's visual mesh and its collider are routinely different sizes.
    // These test colliders, which is what physics means by solid.

    struct RayHit {
        entt::entity entity{entt::null};
        glm::vec3 point{0.0f};
        glm::vec3 normal{0.0f};
        float distance{0.0f};
        bool hit{false};
    };

    // Nearest collider along the ray. Triggers are skipped unless asked for,
    // because a bullet should not stop at a checkpoint volume.
    static RayHit Raycast(entt::registry& registry, const glm::vec3& origin,
                          const glm::vec3& direction, float maxDistance = 1000.0f,
                          entt::entity ignore = entt::null,
                          bool includeTriggers = false);

    // Every collider overlapping a sphere. Appends, so a caller can accumulate
    // across several queries without clearing between them.
    static void OverlapSphere(entt::registry& registry, const glm::vec3& centre, float radius,
                              std::vector<entt::entity>& outEntities,
                              entt::entity ignore = entt::null,
                              bool includeTriggers = true);

    // Whether anything solid sits within `distance` below a point. The check
    // every character controller needs and nobody wants to write twice.
    static bool IsGrounded(entt::registry& registry, const glm::vec3& footPosition,
                           float distance = 0.15f, entt::entity ignore = entt::null);
};

} // namespace Supersonic
