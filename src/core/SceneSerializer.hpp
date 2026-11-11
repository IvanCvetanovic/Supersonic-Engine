#pragma once

#include <string>

#include <entt/entt.hpp>

namespace Supersonic {

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

    // In-memory variants, used by Play/Stop to snapshot and restore the scene
    // without touching the disk. Same format, so a snapshot is just a scene
    // file that never gets written.
    static std::string SerializeToString(entt::registry& registry);

    // The same scene, with every float written at the digits that read back as the
    // SAME float (nine), where SerializeToString writes the stream's default six.
    //
    // For the editor's Play snapshot and its undo history, which are text the
    // editor writes and reads back inside one run. At six digits a position of
    // 1234.5678 came back from Play then Stop as 1234.57, for every entity, and
    // every undo rounded every transform it restored: "Stop restores the scene as
    // it was" was not true, and a smaller edit than the sixth digit was not seen
    // as a change. A scene FILE keeps its six: that is what is on disk, what a
    // game's byte-exact baselines hold, and what a person reads, so
    // SerializeToString and Serialize are untouched and this is a separate call.
    static std::string SerializeSnapshot(entt::registry& registry);
    static SerializationResult DeserializeFromString(entt::registry& registry, const std::string& text);
};

} // namespace Supersonic
