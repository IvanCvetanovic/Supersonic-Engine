#include "core/SceneManager.hpp"

namespace Supersonic {

SerializationResult SceneManager::Save(entt::registry& registry) {
    const auto result = SceneSerializer::Serialize(registry, m_currentPath);
    // Only a successful write clears the flag. A failed save that reported the
    // scene clean would lose the work at the next Open without a warning.
    if (result.ok) m_dirty = false;
    return result;
}

SerializationResult SceneManager::SaveAs(entt::registry& registry, const std::string& path) {
    const auto result = SceneSerializer::Serialize(registry, path);
    if (result.ok) {
        m_currentPath = path;
        m_dirty = false;
    }
    return result;
}

void SceneManager::RequestLoad(const std::string& path) {
    m_pendingLoad = true;
    m_pendingNew = false;
    m_pendingPath = path;
}

void SceneManager::RequestNew() {
    m_pendingNew = true;
    m_pendingLoad = false;
    m_pendingPath.clear();
}

bool SceneManager::ApplyPending(entt::registry& registry, SerializationResult& outResult) {
    if (m_pendingNew) {
        m_pendingNew = false;
        registry.clear();
        m_currentPath = kDefaultScene;
        m_dirty = false;
        outResult = { true, "New scene." };
        return true;
    }

    if (!m_pendingLoad) return false;

    m_pendingLoad = false;
    const std::string path = m_pendingPath;
    m_pendingPath.clear();

    outResult = SceneSerializer::Deserialize(registry, path);

    // Only adopt the path on success. Deserialize is deliberately
    // non-destructive - it parses before it touches the registry - so a failed
    // load leaves the previous scene open, and the name has to agree with that
    // rather than pointing at a file that was never loaded.
    if (outResult.ok) {
        m_currentPath = path;
        m_dirty = false;
    }
    return true;
}

} // namespace Supersonic
