#include "core/MaterialLibrary.hpp"
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
    out << "  \"AlbedoTexture\": \"" << Json::Escape(asset.albedoTexturePath) << "\",\n";
    out << "  \"NormalTexture\": \"" << Json::Escape(asset.normalTexturePath) << "\"\n";
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
    out.albedoTexturePath = root["AlbedoTexture"].AsString("");
    out.normalTexturePath = root["NormalTexture"].AsString("");
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
    return static_cast<bool>(file);
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
