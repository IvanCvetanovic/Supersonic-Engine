#include "core/PrefabSerializer.hpp"
#include "core/Components.hpp"
#include "core/Json.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace Supersonic {

namespace {

void writeVec3(std::ostream& os, const glm::vec3& v) {
    os << "[" << v.x << ", " << v.y << ", " << v.z << "]";
}

glm::vec3 readVec3(const Json::Value& value, const glm::vec3& fallback) {
    const auto& arr = value.AsArray();
    if (arr.size() != 3) return fallback;
    return glm::vec3(arr[0].AsFloat(fallback.x), arr[1].AsFloat(fallback.y), arr[2].AsFloat(fallback.z));
}

} // namespace

SerializationResult PrefabSerializer::SavePrefab(entt::registry& registry, entt::entity entity, const std::string& filepath) {
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

    const std::string tag = registry.all_of<TagComponent>(entity)
                          ? registry.get<TagComponent>(entity).tag
                          : "Prefab Entity";

    // Writes the actual component values. The previous version emitted five
    // "HasX" booleans and no data, so a prefab could not reconstruct anything.
    file << "{\n";
    file << "  \"Tag\": \"" << Json::Escape(tag) << "\"";

    if (const auto* transform = registry.try_get<TransformComponent>(entity)) {
        file << ",\n  \"Transform\": {\n";
        file << "    \"Position\": "; writeVec3(file, transform->position); file << ",\n";
        file << "    \"Rotation\": "; writeVec3(file, transform->rotation); file << ",\n";
        file << "    \"Scale\": ";    writeVec3(file, transform->scale);    file << "\n";
        file << "  }";
    }

    if (const auto* mesh = registry.try_get<MeshComponent>(entity)) {
        file << ",\n  \"Mesh\": { \"Primitive\": \"" << Json::Escape(mesh->primitiveType)
             << "\", \"Path\": \"" << Json::Escape(mesh->filePath) << "\" }";
    }

    if (const auto* mat = registry.try_get<MaterialComponent>(entity)) {
        file << ",\n  \"Material\": { \"Roughness\": " << mat->roughness
             << ", \"Metallic\": " << mat->metallic
             << ", \"AO\": " << mat->ao << " }";
    }

    if (const auto* body = registry.try_get<RigidBodyComponent>(entity)) {
        file << ",\n  \"RigidBody\": { \"Mass\": " << body->mass
             << ", \"UseGravity\": " << (body->useGravity ? "true" : "false") << " }";
    }

    if (const auto* box = registry.try_get<BoxColliderComponent>(entity)) {
        file << ",\n  \"BoxCollider\": { \"Size\": ";
        writeVec3(file, box->size);
        file << " }";
    }

    file << ",\n  \"HasRenderable\": " << (registry.all_of<RenderableComponent>(entity) ? "true" : "false");
    file << ",\n  \"HasParticleEmitter\": " << (registry.all_of<ParticleEmitterComponent>(entity) ? "true" : "false");
    file << "\n}\n";
    file.flush();

    if (!file) {
        return { false, "Write error while saving " + filepath + "." };
    }
    return { true, "Saved prefab to " + filepath + "." };
}

entt::entity PrefabSerializer::InstantiatePrefab(entt::registry& registry, const std::string& filepath,
                                                 SerializationResult* outResult) {
    auto report = [outResult](bool ok, const std::string& message) {
        if (outResult) *outResult = { ok, message };
    };

    // The old version never opened the file at all; it created a hardcoded cube
    // and reported success even for a path that did not exist.
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

    registry.emplace<TagComponent>(entity, root["Tag"].AsString("Prefab Entity"));

    auto& transform = registry.emplace<TransformComponent>(entity);
    if (root.Has("Transform")) {
        const auto& t = root["Transform"];
        transform.position = readVec3(t["Position"], glm::vec3(0.0f, 2.0f, 0.0f));
        transform.rotation = readVec3(t["Rotation"], glm::vec3(0.0f));
        transform.scale = readVec3(t["Scale"], glm::vec3(1.0f));
    } else {
        transform.position = glm::vec3(0.0f, 2.0f, 0.0f);
    }

    if (root.Has("Mesh")) {
        registry.emplace<MeshComponent>(entity,
            root["Mesh"]["Primitive"].AsString("Cube"),
            root["Mesh"]["Path"].AsString(""),
            0u, 0u);
    } else {
        registry.emplace<MeshComponent>(entity, "Cube", "", 0u, 0u);
    }

    if (root.Has("Material")) {
        auto& mat = registry.emplace<MaterialComponent>(entity);
        mat.roughness = root["Material"]["Roughness"].AsFloat(0.4f);
        mat.metallic = root["Material"]["Metallic"].AsFloat(0.1f);
        mat.ao = root["Material"]["AO"].AsFloat(1.0f);
    } else {
        registry.emplace<MaterialComponent>(entity);
    }

    if (root.Has("RigidBody")) {
        auto& body = registry.emplace<RigidBodyComponent>(entity);
        body.mass = root["RigidBody"]["Mass"].AsFloat(1.0f);
        body.useGravity = root["RigidBody"]["UseGravity"].AsBool(true);
    }

    if (root.Has("BoxCollider")) {
        auto& box = registry.emplace<BoxColliderComponent>(entity);
        box.size = readVec3(root["BoxCollider"]["Size"], glm::vec3(1.0f));
    }

    if (root["HasParticleEmitter"].AsBool(false)) {
        registry.emplace<ParticleEmitterComponent>(entity);
    }

    if (root["HasRenderable"].AsBool(true)) {
        registry.emplace<RenderableComponent>(entity);
    }

    report(true, "Instantiated prefab from " + filepath + ".");
    return entity;
}

} // namespace Supersonic
