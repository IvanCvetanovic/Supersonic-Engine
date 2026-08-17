#pragma once

#include <string>

#include <entt/entt.hpp>

namespace Engine {

// Both operations report what happened so the editor can surface it. The old
// bool return was discarded at every call site, so a failed save was invisible
// to anyone not watching the console.
struct SerializationResult {
    bool ok{false};
    std::string message;
};

class SceneSerializer {
public:
    static SerializationResult Serialize(entt::registry& registry, const std::string& filepath);
    static SerializationResult Deserialize(entt::registry& registry, const std::string& filepath);
};

} // namespace Engine
