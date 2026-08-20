#include "core/PrefabSerializer.hpp"

#include <unordered_map>
#include "core/AssetVersion.hpp"
#include "core/ComponentCodec.hpp"
#include "core/Components.hpp"
#include "core/Json.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace Supersonic {

SerializationResult PrefabSerializer::SavePrefab(entt::registry& registry, entt::entity entity,
                                                 const std::string& filepath) {
    if (entity == entt::null || !registry.valid(entity)) {
        return { false, "Cannot save prefab: no valid entity selected." };
    }

    const std::filesystem::path path(filepath);
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return { false, "Could not create " + path.parent_path().string() + ": " + ec.message() };
        }
    }

    std::ofstream file(filepath);
    if (!file.is_open()) {
        return { false, "Failed to open " + filepath + " for writing." };
    }

    // The same writer the scene uses, so a prefab carries everything an entity
    // has rather than the five components this file used to know about. A
    // prefab of a scripted, lit, animated entity used to come back as a bare
    // mesh, and nothing said so.
    file << "{\n";
    file << "  \"Version\": " << AssetVersion::kCurrent << ",\n";
    ComponentCodec::Write(registry, entity, file, "  ");
    file << "}\n";
    file.flush();

    if (!file) {
        return { false, "Write to " + filepath + " failed." };
    }
    // The file on disk has changed, so whatever was parsed from it is now the
    // previous version. Without this, saving a prefab and immediately dragging
    // it back into the scene gives you what it used to be.
    ClearCache();

    return { true, "Saved prefab to " + filepath + "." };
}

namespace {

// Prefabs that have already been read and parsed, by path.
//
// Every spawn used to open the file, read it into a string and run the whole
// JSON parser over it. That is a syscall, an allocation of the file's contents
// and a full parse to produce a document that is identical to the one produced
// the last time - and a game that spawns units from a prefab does this at
// whatever rate it spawns units. A parsed document is the thing worth keeping;
// the file is not going to have changed between two spawns in the same frame.
//
// A function-local static for the same reason the others in this library are:
// one definition however many translation units reach it.
//
// Holds the PARSED document rather than the text, because parsing is the
// expensive half and holding the text would mean doing it again anyway.
std::unordered_map<std::string, Json::Value>& prefabCache() {
    static std::unordered_map<std::string, Json::Value> cache;
    return cache;
}

} // namespace

void PrefabSerializer::ClearCache() { prefabCache().clear(); }

std::size_t PrefabSerializer::CachedPrefabCount() { return prefabCache().size(); }

entt::entity PrefabSerializer::InstantiatePrefab(entt::registry& registry, const std::string& filepath,
                                                 SerializationResult* outResult) {
    auto report = [outResult](bool ok, const std::string& message) {
        if (outResult) *outResult = { ok, message };
    };

    auto& cache = prefabCache();
    auto cached = cache.find(filepath);

    if (cached == cache.end()) {
        // The original version never opened the file at all; it created a
        // hardcoded cube and reported success even for a path that did not
        // exist.
        std::ifstream file(filepath);
        if (!file.is_open()) {
            report(false, "No prefab at " + filepath + ".");
            return entt::null;
        }

        std::stringstream ss;
        ss << file.rdbuf();

        Json::Value parsed;
        std::string error;
        if (!Json::Parse(ss.str(), parsed, error) || !parsed.IsObject()) {
            report(false, "Could not parse " + filepath + ": " + error);
            return entt::null;
        }

        // Only a document that parsed is kept. Caching a failure would make a
        // prefab that was broken once stay broken until the editor restarts,
        // even after the file is fixed.
        cached = cache.emplace(filepath, std::move(parsed)).first;
    }

    const Json::Value& root = cached->second;

    const entt::entity entity = registry.create();

    // A prefab describes one entity, so it carries no parent link: that is an
    // index into a scene's entity array and means nothing on its own.
    ComponentCodec::Read(registry, entity, root);

    // A prefab with no transform still needs one, or it cannot be placed.
    if (!registry.all_of<TransformComponent>(entity)) {
        auto& transform = registry.emplace<TransformComponent>(entity);
        transform.position = glm::vec3(0.0f, 2.0f, 0.0f);
    }
    if (!registry.all_of<TagComponent>(entity)) {
        registry.emplace<TagComponent>(entity, "Prefab Entity");
    }

    report(true, "Instantiated " + filepath + ".");
    return entity;
}

} // namespace Supersonic
