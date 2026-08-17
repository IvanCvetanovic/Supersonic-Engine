#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/Skeleton.hpp"

namespace Supersonic {

// CPU-side rig cache, doing for skeletons and clips what MeshRegistry does for
// geometry - minus the GPU half, so this file links into a test.
//
// Failures are cached too. Without that, a mesh with no rig would be re-parsed
// from disk every frame for as long as it was in the scene.
class AnimationLibrary {
public:
    static constexpr uint32_t kInvalidSkeleton = 0xFFFFFFFFu;

    // Loads the file's first skin and every animation in it. Returns
    // kInvalidSkeleton for a file with no skin.
    uint32_t Acquire(const std::string& filePath);

    const Skeleton* GetSkeleton(uint32_t id) const;
    const std::vector<AnimationClip>* GetClips(uint32_t id) const;

    // Named lookup, falling back to the first clip when the name is empty and to
    // nothing at all when it does not match - so a mistyped name leaves the mesh
    // in bind pose rather than silently playing something else.
    const AnimationClip* FindClip(uint32_t id, const std::string& name) const;

    size_t Size() const { return m_entries.size(); }

private:
    struct Entry {
        Skeleton skeleton;
        std::vector<AnimationClip> clips;
    };

    std::vector<Entry> m_entries;
    std::unordered_map<std::string, uint32_t> m_lookup;
};

} // namespace Supersonic
