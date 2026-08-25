#include "core/MaterialLibrary.hpp"
#include "core/AssetDatabase.hpp"
#include "core/AssetWatcher.hpp"
#include "core/AssetVersion.hpp"
#include "core/Log.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "core/Json.hpp"

namespace fs = std::filesystem;

namespace Supersonic {

namespace {

// A texture reference in a `.material`, written as its identity AND the path it
// had when it was saved - exactly what ComponentCodec writes into a scene, and
// resolved the same way. See AssetDatabase for why it is both rather than
// either.
void writeTextureRef(std::ostringstream& out, const char* key, const std::string& path) {
    out << "  \"" << key << "\": \"" << Json::Escape(path) << "\",\n";

    const std::string guid = AssetDatabase::Instance().GuidForPath(path);
    if (!guid.empty()) {
        out << "  \"" << key << "Guid\": \"" << guid << "\",\n";
    }
}

std::string readTextureRef(const Json::Value& root, const std::string& key) {
    const std::string path = root[key].AsString("");
    const std::string guid = root[key + "Guid"].AsString("");
    if (path.empty() && guid.empty()) return path;
    return AssetDatabase::Instance().Resolve(guid, path);
}

} // namespace


namespace {

glm::vec4 readVec4(const Json::Value& value, const glm::vec4& fallback) {
    const auto& arr = value.AsArray();
    if (arr.size() != 4) return fallback;
    return glm::vec4(arr[0].AsFloat(fallback.x), arr[1].AsFloat(fallback.y),
                     arr[2].AsFloat(fallback.z), arr[3].AsFloat(fallback.w));
}

} // namespace

std::string MaterialLibrary::Serialize(const MaterialAsset& asset) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"Version\": " << AssetVersion::kCurrent << ",\n";
    out << "  \"Material\": \"" << Json::Escape(asset.name) << "\",\n";
    out << "  \"Albedo\": [" << asset.albedoColor.x << ", " << asset.albedoColor.y << ", "
        << asset.albedoColor.z << ", " << asset.albedoColor.w << "],\n";
    out << "  \"Roughness\": " << asset.roughness << ",\n";
    out << "  \"Metallic\": " << asset.metallic << ",\n";
    out << "  \"AO\": " << asset.ao << ",\n";
    // The same identity-and-path pair a scene writes, for the same reason: a
    // `.material` names three textures, and renaming one of them used to break
    // every entity sharing that material rather than only the scene it was in.
    writeTextureRef(out, "AlbedoTexture", asset.albedoTexturePath);
    writeTextureRef(out, "NormalTexture", asset.normalTexturePath);
    writeTextureRef(out, "OrmTexture", asset.ormTexturePath);
    out << "  \"OcclusionStrength\": " << asset.occlusionStrength << "\n";
    out << "}\n";
    return out.str();
}

bool MaterialLibrary::Deserialize(const std::string& text, MaterialAsset& out, std::string& error) {
    Json::Value root;
    if (!Json::Parse(text, root, error)) return false;
    if (!root.IsObject()) {
        error = "material must be a JSON object";
        return false;
    }

    out.name = root["Material"].AsString("Material");
    out.albedoColor = readVec4(root["Albedo"], glm::vec4(1.0f));
    out.roughness = root["Roughness"].AsFloat(0.4f);
    out.metallic = root["Metallic"].AsFloat(0.1f);
    out.ao = root["AO"].AsFloat(1.0f);
    out.albedoTexturePath = readTextureRef(root, "AlbedoTexture");
    out.normalTexturePath = readTextureRef(root, "NormalTexture");
    // Absent from every .material written before packed maps existed, which
    // reads as no map and therefore as exactly the surface it always was.
    out.ormTexturePath = readTextureRef(root, "OrmTexture");
    out.occlusionStrength = root["OcclusionStrength"].AsFloat(1.0f);
    return true;
}

uint32_t MaterialLibrary::Acquire(const std::string& path) {
    if (path.empty()) return kInvalidMaterial;

    if (const auto it = m_lookup.find(path); it != m_lookup.end()) {
        return m_entries[it->second].valid ? it->second : kInvalidMaterial;
    }

    Entry entry;
    entry.path = path;

    std::ifstream file(path);
    if (!file) {
        SUPERSONIC_LOG_ERROR("MaterialLibrary") << "Could not open " << path << "." << std::endl;
        // Cached as a miss so a broken reference is not retried every frame.
        const auto id = static_cast<uint32_t>(m_entries.size());
        m_entries.push_back(std::move(entry));
        m_lookup.emplace(path, id);
        return kInvalidMaterial;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    std::string error;
    if (!Deserialize(buffer.str(), entry.asset, error)) {
        SUPERSONIC_LOG_ERROR("MaterialLibrary") << path << " is not a valid material: " << error << std::endl;
        const auto id = static_cast<uint32_t>(m_entries.size());
        m_entries.push_back(std::move(entry));
        m_lookup.emplace(path, id);
        return kInvalidMaterial;
    }

    entry.valid = true;
    const auto id = static_cast<uint32_t>(m_entries.size());
    m_entries.push_back(std::move(entry));
    m_lookup.emplace(path, id);

    SUPERSONIC_LOG_INFO("MaterialLibrary") << "Loaded " << path << "." << std::endl;
    return id;
}

size_t MaterialLibrary::Repoint(const std::string& from, const std::string& to) {
    if (from.empty() || to.empty() || from == to) return 0;

    size_t changed = 0;
    for (Entry& entry : m_entries) {
        if (!entry.valid) continue;
        for (std::string* field : {&entry.asset.albedoTexturePath,
                                   &entry.asset.normalTexturePath,
                                   &entry.asset.ormTexturePath}) {
            if (*field == from) {
                *field = to;
                ++changed;
            }
        }
    }

    // Deliberately NOT the entry's own path. A .material that was itself
    // renamed keeps its cache key, so re-keying m_lookup here would leave the
    // entry reachable under a path no component names any more while the
    // component - re-pointed to the new path - misses the cache and loads a
    // second copy of the same asset. The component's path is what matters, and
    // the miss that follows re-reads the file, which is correct.
    return changed;
}

bool MaterialLibrary::Reload(const std::string& path) {
    const auto it = m_lookup.find(path);
    if (it == m_lookup.end()) return false;
    const uint32_t id = it->second;

    std::ifstream file(path);
    if (!file) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();

    // Into a temporary, so a parse that fails half way through does not leave
    // the live asset holding the fields it managed to read before it gave up.
    MaterialAsset parsed;
    std::string error;
    if (!Deserialize(buffer.str(), parsed, error)) {
        SUPERSONIC_LOG_ERROR("MaterialLibrary")
            << path << " changed but no longer parses (" << error
            << "); keeping the values already loaded." << std::endl;
        return false;
    }

    m_entries[id].asset = std::move(parsed);
    // Promoted, not just refreshed: a path that failed to load is cached as a
    // miss so it is not retried every frame, and fixing the file on disk is
    // precisely how someone expects to clear that.
    m_entries[id].valid = true;

    SUPERSONIC_LOG_INFO("MaterialLibrary") << "Reloaded " << path << "." << std::endl;
    return true;
}

MaterialAsset* MaterialLibrary::Get(uint32_t id) {
    if (id >= m_entries.size() || !m_entries[id].valid) return nullptr;
    return &m_entries[id].asset;
}

const MaterialAsset* MaterialLibrary::Get(uint32_t id) const {
    if (id >= m_entries.size() || !m_entries[id].valid) return nullptr;
    return &m_entries[id].asset;
}

const std::string& MaterialLibrary::PathOf(uint32_t id) const {
    if (id >= m_entries.size()) return m_emptyPath;
    return m_entries[id].path;
}

bool MaterialLibrary::Save(uint32_t id) const {
    if (id >= m_entries.size() || !m_entries[id].valid) return false;
    const Entry& entry = m_entries[id];

    std::error_code ec;
    const fs::path path(entry.path);
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
    }

    std::ofstream file(entry.path, std::ios::trunc);
    if (!file) {
        SUPERSONIC_LOG_ERROR("MaterialLibrary") << "Could not write " << entry.path << "." << std::endl;
        return false;
    }
    file << Serialize(entry.asset);
    if (!file) return false;

    // Before returning, and after the stream is known good. The file is still
    // open here, so close it first: on Windows the write time is not settled
    // until the handle is released, and acknowledging early records the time
    // from BEFORE the flush - which the next poll then sees as a change, which
    // is the exact loop this is here to prevent.
    file.close();
    if (m_watcher) m_watcher->Acknowledge(entry.path);
    return true;
}

uint32_t MaterialLibrary::Create(const std::string& path, const MaterialAsset& asset) {
    if (path.empty()) return kInvalidMaterial;

    Entry entry;
    entry.path = path;
    entry.asset = asset;
    entry.valid = true;

    // Replaces any cached miss for the same path, so creating a material where
    // a broken reference used to point resolves it rather than staying broken.
    uint32_t id;
    if (const auto it = m_lookup.find(path); it != m_lookup.end()) {
        id = it->second;
        m_entries[id] = std::move(entry);
    } else {
        id = static_cast<uint32_t>(m_entries.size());
        m_entries.push_back(std::move(entry));
        m_lookup.emplace(path, id);
    }

    if (!Save(id)) {
        m_entries[id].valid = false;
        return kInvalidMaterial;
    }
    SUPERSONIC_LOG_INFO("MaterialLibrary") << "Created " << path << "." << std::endl;
    return id;
}

} // namespace Supersonic
