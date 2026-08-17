#include "core/SceneSerializer.hpp"
#include "core/Components.hpp"
#include "core/Json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace Engine {

namespace {

void writeVec3(std::ostream& os, const glm::vec3& v) {
    os << "[" << v.x << ", " << v.y << ", " << v.z << "]";
}

glm::vec3 readVec3(const Json::Value& value, const glm::vec3& fallback) {
    const auto& arr = value.AsArray();
    if (arr.size() != 3) return fallback;
    return glm::vec3(arr[0].AsFloat(fallback.x),
                     arr[1].AsFloat(fallback.y),
                     arr[2].AsFloat(fallback.z));
}

glm::vec4 readVec4(const Json::Value& value, const glm::vec4& fallback) {
    const auto& arr = value.AsArray();
    if (arr.size() != 4) return fallback;
    return glm::vec4(arr[0].AsFloat(fallback.x),
                     arr[1].AsFloat(fallback.y),
                     arr[2].AsFloat(fallback.z),
                     arr[3].AsFloat(fallback.w));
}

} // namespace

SerializationResult SceneSerializer::Serialize(entt::registry& registry, const std::string& filepath) {
    // ofstream will not create missing parent directories, and assets/scenes is
    // not in the repository, so saving used to fail silently on a fresh clone.
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

    // Collect first: the entity count must match what is actually written.
    // storage<entt::entity>().size() includes released entities under EnTT's
    // swap_only policy, which produced a trailing comma and invalid JSON.
    std::vector<entt::entity> entities;
    for (auto entity : registry.view<entt::entity>()) {
        entities.push_back(entity);
    }

    file << "{\n  \"Scene\": \"MainScene\",\n  \"Entities\": [\n";

    for (size_t i = 0; i < entities.size(); ++i) {
        const entt::entity entity = entities[i];
        file << "    {\n";

        if (const auto* tag = registry.try_get<TagComponent>(entity)) {
            file << "      \"Tag\": \"" << Json::Escape(tag->tag) << "\",\n";
        }

        if (const auto* transform = registry.try_get<TransformComponent>(entity)) {
            file << "      \"Transform\": {\n";
            file << "        \"Position\": "; writeVec3(file, transform->position); file << ",\n";
            file << "        \"Rotation\": "; writeVec3(file, transform->rotation); file << ",\n";
            file << "        \"Scale\": ";    writeVec3(file, transform->scale);    file << "\n";
            file << "      },\n";
        }

        if (const auto* mesh = registry.try_get<MeshComponent>(entity)) {
            file << "      \"Mesh\": {\n";
            file << "        \"Primitive\": \"" << Json::Escape(mesh->primitiveType) << "\",\n";
            file << "        \"Path\": \"" << Json::Escape(mesh->filePath) << "\"\n";
            file << "      },\n";
        }

        if (const auto* light = registry.try_get<LightComponent>(entity)) {
            file << "      \"Light\": {\n";
            file << "        \"Direction\": "; writeVec3(file, light->direction); file << ",\n";
            file << "        \"Color\": ";     writeVec3(file, light->color);     file << ",\n";
            file << "        \"Ambient\": ";   writeVec3(file, light->ambient);   file << ",\n";
            file << "        \"Intensity\": " << light->intensity << "\n";
            file << "      },\n";
        }

        if (const auto* camera = registry.try_get<CameraComponent>(entity)) {
            file << "      \"Camera\": {\n";
            file << "        \"FOV\": " << camera->fov << ",\n";
            file << "        \"NearPlane\": " << camera->nearPlane << ",\n";
            file << "        \"FarPlane\": " << camera->farPlane << ",\n";
            file << "        \"Position\": "; writeVec3(file, camera->position); file << ",\n";
            file << "        \"Yaw\": " << camera->yaw << ",\n";
            file << "        \"Pitch\": " << camera->pitch << "\n";
            file << "      },\n";
        }

        if (const auto* mat = registry.try_get<MaterialComponent>(entity)) {
            file << "      \"Material\": {\n";
            file << "        \"Albedo\": [" << mat->albedoColor.x << ", " << mat->albedoColor.y << ", "
                 << mat->albedoColor.z << ", " << mat->albedoColor.w << "],\n";
            file << "        \"Roughness\": " << mat->roughness << ",\n";
            file << "        \"Metallic\": " << mat->metallic << ",\n";
            file << "        \"AO\": " << mat->ao << "\n";
            file << "      },\n";
        }

        if (const auto* body = registry.try_get<RigidBodyComponent>(entity)) {
            file << "      \"RigidBody\": {\n";
            file << "        \"Velocity\": "; writeVec3(file, body->velocity); file << ",\n";
            file << "        \"Mass\": " << body->mass << ",\n";
            file << "        \"UseGravity\": " << (body->useGravity ? "true" : "false") << ",\n";
            file << "        \"IsKinematic\": " << (body->isKinematic ? "true" : "false") << "\n";
            file << "      },\n";
        }

        if (const auto* box = registry.try_get<BoxColliderComponent>(entity)) {
            file << "      \"BoxCollider\": { \"Size\": ";
            writeVec3(file, box->size);
            file << " },\n";
        }

        if (const auto* audio = registry.try_get<AudioSourceComponent>(entity)) {
            // voice/failedToLoad are runtime state owned by AudioSystem and are
            // deliberately not persisted.
            file << "      \"AudioSource\": {\n";
            file << "        \"Clip\": \"" << Json::Escape(audio->soundFile) << "\",\n";
            file << "        \"Volume\": " << audio->volume << ",\n";
            file << "        \"Pitch\": " << audio->pitch << ",\n";
            file << "        \"Playing\": " << (audio->isPlaying ? "true" : "false") << ",\n";
            file << "        \"Loop\": " << (audio->loop ? "true" : "false") << ",\n";
            file << "        \"ReferenceDistance\": " << audio->referenceDistance << ",\n";
            file << "        \"MaxDistance\": " << audio->maxDistance << "\n";
            file << "      },\n";
        }

        if (const auto* script = registry.try_get<ScriptComponent>(entity)) {
            file << "      \"Script\": {\n";
            file << "        \"Name\": \"" << Json::Escape(script->scriptName) << "\",\n";
            file << "        \"Enabled\": " << (script->isEnabled ? "true" : "false") << "\n";
            file << "      },\n";
        }

        if (registry.all_of<ParticleEmitterComponent>(entity)) {
            file << "      \"ParticleEmitter\": true,\n";
        }

        file << "      \"HasRenderable\": " << (registry.all_of<RenderableComponent>(entity) ? "true" : "false") << "\n";
        file << "    }" << (i + 1 < entities.size() ? "," : "") << "\n";
    }

    file << "  ]\n}\n";
    file.flush();

    if (!file) {
        return { false, "Write error while saving " + filepath + "." };
    }

    return { true, "Saved " + std::to_string(entities.size()) + " entities to " + filepath + "." };
}

SerializationResult SceneSerializer::Deserialize(entt::registry& registry, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return { false, "No scene file at " + filepath + "." };
    }

    std::stringstream ss;
    ss << file.rdbuf();
    const std::string content = ss.str();

    Json::Value root;
    std::string error;
    if (!Json::Parse(content, root, error)) {
        // Parse BEFORE touching the registry. The previous implementation
        // cleared the scene first and then never read the file at all, so a
        // load destroyed the user's work whatever the file contained.
        return { false, "Could not parse " + filepath + ": " + error + " (scene left untouched)." };
    }

    if (!root.IsObject() || !root["Entities"].IsArray()) {
        return { false, filepath + " is not a scene file (no Entities array); scene left untouched." };
    }

    const auto& entities = root["Entities"].AsArray();

    registry.clear();

    size_t loaded = 0;
    for (const auto& node : entities) {
        if (!node.IsObject()) continue;

        const entt::entity entity = registry.create();
        ++loaded;

        if (node.Has("Tag")) {
            registry.emplace<TagComponent>(entity, node["Tag"].AsString("Entity"));
        }

        if (node.Has("Transform")) {
            const auto& t = node["Transform"];
            auto& transform = registry.emplace<TransformComponent>(entity);
            transform.position = readVec3(t["Position"], glm::vec3(0.0f));
            transform.rotation = readVec3(t["Rotation"], glm::vec3(0.0f));
            transform.scale = readVec3(t["Scale"], glm::vec3(1.0f));
        }

        if (node.Has("Mesh")) {
            const auto& m = node["Mesh"];
            registry.emplace<MeshComponent>(entity,
                m["Primitive"].AsString("Cube"),
                m["Path"].AsString(""),
                0u, 0u);
        }

        if (node.Has("Light")) {
            const auto& l = node["Light"];
            auto& light = registry.emplace<LightComponent>(entity);
            light.direction = readVec3(l["Direction"], glm::vec3(0.6f, 1.0f, 0.5f));
            light.color = readVec3(l["Color"], glm::vec3(1.0f));
            light.ambient = readVec3(l["Ambient"], glm::vec3(0.12f));
            light.intensity = l["Intensity"].AsFloat(1.5f);
        }

        if (node.Has("Camera")) {
            const auto& c = node["Camera"];
            auto& camera = registry.emplace<CameraComponent>(entity);
            camera.fov = c["FOV"].AsFloat(45.0f);
            camera.nearPlane = c["NearPlane"].AsFloat(0.1f);
            camera.farPlane = c["FarPlane"].AsFloat(100.0f);
            camera.position = readVec3(c["Position"], glm::vec3(0.0f, 1.2f, 4.0f));
            camera.yaw = c["Yaw"].AsFloat(-90.0f);
            camera.pitch = c["Pitch"].AsFloat(-10.0f);
            camera.updateCameraVectors();
        }

        if (node.Has("Material")) {
            const auto& m = node["Material"];
            auto& material = registry.emplace<MaterialComponent>(entity);
            material.albedoColor = readVec4(m["Albedo"], glm::vec4(1.0f));
            material.roughness = m["Roughness"].AsFloat(0.4f);
            material.metallic = m["Metallic"].AsFloat(0.1f);
            material.ao = m["AO"].AsFloat(1.0f);
        }

        if (node.Has("RigidBody")) {
            const auto& r = node["RigidBody"];
            auto& body = registry.emplace<RigidBodyComponent>(entity);
            body.velocity = readVec3(r["Velocity"], glm::vec3(0.0f));
            body.mass = r["Mass"].AsFloat(1.0f);
            body.useGravity = r["UseGravity"].AsBool(true);
            body.isKinematic = r["IsKinematic"].AsBool(false);
        }

        if (node.Has("BoxCollider")) {
            auto& box = registry.emplace<BoxColliderComponent>(entity);
            box.size = readVec3(node["BoxCollider"]["Size"], glm::vec3(1.0f));
        }

        if (node.Has("AudioSource")) {
            const auto& a = node["AudioSource"];
            auto& audio = registry.emplace<AudioSourceComponent>(entity);
            audio.soundFile = a["Clip"].AsString("assets/audio/ambient.wav");
            audio.volume = a["Volume"].AsFloat(0.8f);
            audio.pitch = a["Pitch"].AsFloat(1.0f);
            audio.isPlaying = a["Playing"].AsBool(true);
            audio.loop = a["Loop"].AsBool(true);
            audio.referenceDistance = a["ReferenceDistance"].AsFloat(1.5f);
            audio.maxDistance = a["MaxDistance"].AsFloat(40.0f);
        }

        if (node.Has("Script")) {
            const auto& s = node["Script"];
            registry.emplace<ScriptComponent>(entity,
                s["Name"].AsString("RotatorScript"),
                s["Enabled"].AsBool(true));
        }

        if (node["ParticleEmitter"].AsBool(false)) {
            registry.emplace<ParticleEmitterComponent>(entity);
        }

        if (node["HasRenderable"].AsBool(false)) {
            registry.emplace<RenderableComponent>(entity);
        }
    }

    return { true, "Loaded " + std::to_string(loaded) + " entities from " + filepath + "." };
}

} // namespace Engine
