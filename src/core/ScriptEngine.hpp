#pragma once

#include <glm/glm.hpp>

#include <vector>

#include <string>

#include <entt/entt.hpp>

namespace Supersonic {

class ScriptEngine {
public:
    // Registers the built-in scripts with ScriptRegistry. Call once at startup.
    static void RegisterBuiltInScripts();

    static void Update(entt::registry& registry, float deltaTime);

    // Deferred entity commands.
    //
    // spawn and destroy both mutate the registry, and scripts run inside a view
    // over it - creating an entity there invalidates the iteration the caller
    // is in the middle of. Requests are queued during the script pass and
    // applied by the caller afterwards, which is also why the ABI's spawn
    // returns nothing: the entity does not exist yet.
    struct PendingCommands {
        struct Spawn {
            std::string prefabPath;
            glm::vec3 position{0.0f};
        };
        std::vector<Spawn> spawns;
        std::vector<entt::entity> destroys;

        bool Empty() const { return spawns.empty() && destroys.empty(); }
        void Clear() { spawns.clear(); destroys.clear(); }
    };

    // Applies whatever the frame's scripts asked for, and clears the queue.
    // Returns how many entities were created and destroyed.
    static void ApplyPendingCommands(entt::registry& registry, size_t* outSpawned = nullptr,
                                     size_t* outDestroyed = nullptr);
};

} // namespace Supersonic
