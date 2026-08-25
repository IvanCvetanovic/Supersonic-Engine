#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace Supersonic {

class AssetWatcher;

// A material as a shared asset on disk, rather than a per-entity component.
//
// Materials were authored per entity: every object carried its own copy of the
// same numbers, and retuning a look meant editing each one by hand with no way
// to tell which were meant to match. This is the same data, owned once and
// referenced by path.
struct MaterialAsset {
    std::string name{"Material"};
    glm::vec4 albedoColor{1.0f, 1.0f, 1.0f, 1.0f};
    float roughness{0.4f};
    float metallic{0.1f};
    float ao{1.0f};
    std::string albedoTexturePath;
    std::string normalTexturePath;

    // Occlusion, roughness and metallic packed as glTF packs them: R, G, B.
    std::string ormTexturePath;
    float occlusionStrength{1.0f};
};

// Loads, caches and writes .material assets.
//
// Vulkan-free by design, like MeshRegistry's CPU half and AnimationLibrary: the
// GPU never sees a material, only the resolved numbers in the push constant.
class MaterialLibrary {
public:
    static constexpr uint32_t kInvalidMaterial = 0xFFFFFFFFu;

    // The watcher to tell when this library writes a file, so the engine does
    // not read its own writes back as somebody else's edit.
    //
    // Held here rather than called at the two places that write today, because
    // "acknowledge after writing" is an invariant and a call site is a place to
    // forget it. Optional: a test builds a library with no watcher and the
    // acknowledgement is a null check.
    void SetWatcher(AssetWatcher* watcher) { m_watcher = watcher; }

    // Loads the asset, or returns the id already cached for that path. A file
    // that cannot be read is cached as a miss, so it is not retried every frame.
    uint32_t Acquire(const std::string& path);

    // Rewrites cached texture references that name `from` so they name `to`,
    // and returns how many changed.
    //
    // In memory only: the .material on disk keeps the old path, and does not
    // need not to, because it also stores the identity - so the next load
    // resolves the reference to wherever the file is now. Rewriting the file
    // here would mean writing to disk from something the user asked to READ.
    size_t Repoint(const std::string& from, const std::string& to);

    // Re-reads a cached asset from disk, KEEPING ITS ID.
    //
    // Not Acquire again: an id is an index into m_entries, and every
    // MaterialComponent in the scene is holding one. Pushing a new entry would
    // leave all of them pointing at the values from before the edit, which is
    // the failure this exists to fix, only quieter.
    //
    // A file that no longer parses leaves the PREVIOUS values in place and
    // returns false. A save is not atomic, so a poll can land between the
    // truncate and the write and read an empty file; blanking the asset there
    // would turn every object using it white for a frame, and if the editor
    // then saved, would write that blank back over the real one.
    //
    // Returns false for a path that was never acquired - there is nothing
    // cached to reload, and loading it here would give an id to a material no
    // component has asked for.
    bool Reload(const std::string& path);

    // Mutable, because the inspector edits the shared asset in place - which is
    // the point: one edit updates every entity using it.
    MaterialAsset* Get(uint32_t id);
    const MaterialAsset* Get(uint32_t id) const;

    // Writes the asset back to its own path. Returns false and leaves the file
    // untouched when the directory cannot be created or the write fails.
    bool Save(uint32_t id) const;

    // Creates a new asset on disk and returns its id, or kInvalidMaterial when
    // the file could not be written.
    uint32_t Create(const std::string& path, const MaterialAsset& asset);

    const std::string& PathOf(uint32_t id) const;

    size_t Size() const { return m_entries.size(); }

    // Parsing and formatting, exposed so a test can round-trip without a disk.
    static std::string Serialize(const MaterialAsset& asset);
    static bool Deserialize(const std::string& text, MaterialAsset& out, std::string& error);

private:
    struct Entry {
        MaterialAsset asset;
        std::string path;
        bool valid{false};
    };

    // Mutable is not needed: Save() is const and only the POINTER is a member,
    // so telling the watcher through it does not modify this object.
    AssetWatcher* m_watcher{nullptr};

    std::vector<Entry> m_entries;
    std::unordered_map<std::string, uint32_t> m_lookup;
    std::string m_emptyPath;
};

} // namespace Supersonic
