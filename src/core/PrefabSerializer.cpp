#include "core/PrefabSerializer.hpp"
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
    ComponentCodec::Write(registry, entity, file, "  ");
    file << "}\n";
    file.flush();

    if (!file) {
        return { false, "Write to " + filepath + " failed." };
    }
    return { true, "Saved prefab to " + filepath + "." };
}

entt::entity PrefabSerializer::InstantiatePrefab(entt::registry& registry, const std::string& filepath,
                                                 SerializationResult* outResult) {
    auto report = [outResult](bool ok, const std::string& message) {
        if (outResult) *outResult = { ok, message };
    };

    // The original version never opened the file at all; it created a hardcoded
    // cube and reported success even for a path that did not exist.
    std::ifstream file(filepath);
    if (!file.is_open()) {
        report(false, "No prefab at " + filepath + ".");
        return entt::null;
    }

    std::stringstream ss;
    ss << file.rdbuf();

    Json::Value root;
    std::string error;
    if (!Json::Parse(ss.str(), root, error) || !root.IsObject()) {
        report(false, "Could not parse " + filepath + ": " + error);
        return entt::null;
    }

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
