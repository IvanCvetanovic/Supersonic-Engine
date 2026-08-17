#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace Supersonic {

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
};

// Loads, caches and writes .material assets.
//
// Vulkan-free by design, like MeshRegistry's CPU half and AnimationLibrary: the
// GPU never sees a material, only the resolved numbers in the push constant.
class MaterialLibrary {
public:
    static constexpr uint32_t kInvalidMaterial = 0xFFFFFFFFu;

    // Loads the asset, or returns the id already cached for that path. A file
    // that cannot be read is cached as a miss, so it is not retried every frame.
    uint32_t Acquire(const std::string& path);

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

    std::vector<Entry> m_entries;
    std::unordered_map<std::string, uint32_t> m_lookup;
    std::string m_emptyPath;
};

} // namespace Supersonic
