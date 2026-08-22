#pragma once

#include <string>

#include <entt/entt.hpp>

#include "core/SceneSerializer.hpp"

namespace Supersonic {

// Which scene is open, and whether it has unsaved changes.
//
// Nothing owned this. `kScenePath` was a constexpr in EditorLayer.cpp and it
// was the only argument Save, Open and both keyboard shortcuts ever passed, so
// the engine had exactly one scene by construction: a .scene file in the
// content browser got an icon and no behaviour, "Save As" did not exist, and
// GamePackager wrote that same literal into every manifest it produced -
// meaning "package the current scene" shipped whatever was last written to
// MainScene.scene regardless of what was actually open.
//
// Loading is DEFERRED rather than immediate. A load clears and refills the
// registry, and the editor asks for one from inside its own UI build, while
// panels are iterating views over that registry. Doing it there invalidates
// what the caller is walking. RequestLoad records the wish; ApplyPending
// performs it at a point in the frame where nothing is mid-iteration.
class SceneManager {
public:
    static constexpr const char* kDefaultScene = "assets/scenes/MainScene.scene";

    const std::string& CurrentPath() const { return m_currentPath; }
    bool IsDirty() const { return m_dirty; }

    // Set by whatever mutates the scene. Kept as a plain flag rather than
    // inferred from the registry: EnTT's on_update sinks never fire for this
    // codebase, which mutates through registry.get<T>() references.
    void MarkDirty() { m_dirty = true; }

    // Records a scene that something else opened, without going through the
    // deferred path. Startup is the one place this is right: the app
    // deserializes the manifest's scene (or --scene) directly, before there is
    // a frame to defer into, and without this the manager would still be
    // naming the default scene - so "Save" would write the open scene over
    // MainScene.scene, and packaging would ship the wrong level.
    void AdoptLoaded(const std::string& path) {
        m_currentPath = path;
        m_dirty = false;
        m_pendingLoad = false;
        m_pendingNew = false;
        m_pendingPath.clear();
    }

    SerializationResult Save(entt::registry& registry);
    SerializationResult SaveAs(entt::registry& registry, const std::string& path);

    // Queues a load. Nothing happens to the registry until ApplyPending runs.
    void RequestLoad(const std::string& path);
    void RequestNew();

    bool HasPending() const { return m_pendingLoad || m_pendingNew; }

    // Performs a queued load or new-scene, and reports what happened. Returns
    // false when there was nothing to do, so a caller can skip the rest.
    bool ApplyPending(entt::registry& registry, SerializationResult& outResult);

private:
    std::string m_currentPath{kDefaultScene};
    bool m_dirty{false};

    bool m_pendingLoad{false};
    bool m_pendingNew{false};
    std::string m_pendingPath;
};

} // namespace Supersonic
