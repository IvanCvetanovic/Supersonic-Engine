#include "core/AnimationLibrary.hpp"
#include "core/Log.hpp"

#include <iostream>

#include "core/GltfLoader.hpp"

namespace Supersonic {

uint32_t AnimationLibrary::Acquire(const std::string& filePath) {
    if (filePath.empty()) return kInvalidSkeleton;

    if (const auto it = m_lookup.find(filePath); it != m_lookup.end()) {
        return it->second;
    }

    GltfLoader::Scene scene = GltfLoader::Load(filePath);
    if (!scene.ok || scene.skeletons.empty() || scene.skeletons.front().empty()) {
        // Cached as a miss, so a rigless mesh is not re-parsed every frame for
        // as long as it stays in the scene.
        m_lookup.emplace(filePath, kInvalidSkeleton);
        return kInvalidSkeleton;
    }

    if (scene.skeletons.size() > 1) {
        SUPERSONIC_LOG_INFO("AnimationLibrary") << filePath << " has " << scene.skeletons.size()
                  << " skins; only the first is used." << std::endl;
    }

    Entry entry;
    entry.skeleton = std::move(scene.skeletons.front());
    entry.clips = std::move(scene.clips);

    const auto id = static_cast<uint32_t>(m_entries.size());
    m_entries.push_back(std::move(entry));
    m_lookup.emplace(filePath, id);

    SUPERSONIC_LOG_INFO("AnimationLibrary") << "Loaded rig from " << filePath << " ("
              << m_entries[id].skeleton.joints.size() << " joints, "
              << m_entries[id].clips.size() << " clip(s))." << std::endl;
    return id;
}

const Skeleton* AnimationLibrary::GetSkeleton(uint32_t id) const {
    if (id >= m_entries.size()) return nullptr;
    return &m_entries[id].skeleton;
}

const std::vector<AnimationClip>* AnimationLibrary::GetClips(uint32_t id) const {
    if (id >= m_entries.size()) return nullptr;
    return &m_entries[id].clips;
}

const AnimationClip* AnimationLibrary::FindClip(uint32_t id, const std::string& name) const {
    const auto* clips = GetClips(id);
    if (!clips || clips->empty()) return nullptr;

    if (name.empty()) return &clips->front();

    for (const auto& clip : *clips) {
        if (clip.name == name) return &clip;
    }
    return nullptr;
}

} // namespace Supersonic
