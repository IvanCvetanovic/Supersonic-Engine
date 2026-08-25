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

    // Re-reads a rig already in the cache, KEEPING ITS ID.
    //
    // Ids are indices into m_entries and every SkinnedMeshComponent holds one,
    // so a reload must re-parse in place rather than push. Returns false for a
    // path nobody cached, and for a re-parse that failed - in which case the
    // rig that was there is left exactly as it was, because a .glb caught
    // half-written should not put a character into bind pose permanently.
    //
    // ONLY safe to call between frames, from the asset-watcher callback. The
    // re-parse frees the storage GetSkeleton, GetClips and FindClip hand out,
    // so anything holding one of those pointers across this call is left
    // pointing at freed memory. No system holds one across a frame today; the
    // rule is written down so none starts.
    bool Reload(const std::string& filePath);

    // Bumped every time an id's contents are replaced by Reload.
    //
    // An id alone cannot say "the same rig, re-exported", and that is precisely
    // the case a reload creates. Anything that caches something DERIVED from a
    // rig - a joint palette, a bind-pose bounding box - has to notice, and
    // comparing ids will not tell it. Returns 0 for an id that is not loaded.
    uint32_t GenerationOf(uint32_t id) const;

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

        // Starts at 1, so 0 can mean "no rig" without colliding with a real
        // generation of a rig that has never been reloaded.
        uint32_t generation{1};
    };

    std::vector<Entry> m_entries;
    std::unordered_map<std::string, uint32_t> m_lookup;
};

} // namespace Supersonic
