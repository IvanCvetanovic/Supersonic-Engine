#include "core/SceneSerializer.hpp"
#include "core/Components.hpp"
#include "core/Json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Supersonic {

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

// One writer, used by both the on-disk save and the in-memory Play snapshot, so
// the two formats cannot drift apart.
size_t writeScene(entt::registry& registry, std::ostream& file) {
    // Collect first: the entity count must match what is actually written.
    // storage<entt::entity>().size() includes released entities under EnTT's
    // swap_only policy, which produced a trailing comma and invalid JSON.
    std::vector<entt::entity> entities;
    for (auto entity : registry.view<entt::entity>()) {
        entities.push_back(entity);
    }
    // EnTT walks its packed array back to front, so the view yields entities in
    // reverse creation order. Reversing here makes the round trip stable: a
    // scene written, loaded and written again produces byte-identical text.
    //
    // Without this, applyScene's create-in-file-order rebuild flipped the order
    // every load, so the hierarchy panel reversed itself on every undo - and,
    // worse, EditHistory's "has the scene changed?" text comparison saw a
    // difference where there was none, which spuriously recorded a step after
    // every undo and left redo permanently dead.
    std::reverse(entities.begin(), entities.end());

    // Parent links are written as an index into this array, not as a raw
    // entt::entity: handles are recycled and carry a version, so persisting
    // them would reattach to whatever occupied that slot on load.
    std::unordered_map<entt::entity, size_t> indexOf;
    indexOf.reserve(entities.size());
    for (size_t i = 0; i < entities.size(); ++i) {
        indexOf.emplace(entities[i], i);
    }

    file << "{\n  \"Scene\": \"MainScene\",\n  \"Entities\": [\n";

    for (size_t i = 0; i < entities.size(); ++i) {
        const entt::entity entity = entities[i];
        file << "    {\n";

        if (const auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
            if (hierarchy->parent != entt::null) {
                if (const auto it = indexOf.find(hierarchy->parent); it != indexOf.end()) {
                    file << "      \"Parent\": " << it->second << ",\n";
                }
            }
        }

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
            file << "        \"Type\": " << light->type << ",\n";
            file << "        \"Direction\": "; writeVec3(file, light->direction); file << ",\n";
            file << "        \"Color\": ";     writeVec3(file, light->color);     file << ",\n";
            file << "        \"Ambient\": ";   writeVec3(file, light->ambient);   file << ",\n";
            file << "        \"Intensity\": " << light->intensity << ",\n";
            file << "        \"Range\": " << light->range << ",\n";
            file << "        \"CastsShadow\": " << (light->castsShadow ? "true" : "false") << "\n";
            file << "      },\n";
        }

        if (const auto* camera = registry.try_get<CameraComponent>(entity)) {
            file << "      \"Camera\": {\n";
            file << "        \"FOV\": " << camera->fov << ",\n";
            file << "        \"NearPlane\": " << camera->nearPlane << ",\n";
            file << "        \"FarPlane\": " << camera->farPlane << ",\n";
            file << "        \"Position\": "; writeVec3(file, camera->position); file << ",\n";
            file << "        \"Yaw\": " << camera->yaw << ",\n";
            file << "        \"Pitch\": " << camera->pitch << ",\n";
            file << "        \"MovementSpeed\": " << camera->movementSpeed << ",\n";
            file << "        \"MouseSensitivity\": " << camera->mouseSensitivity << ",\n";
            // aspect is not persisted: it is recomputed from the viewport panel
            // every frame, so a stored value would be wrong on any other layout.
            file << "        \"IsPrimary\": " << (camera->isPrimary ? "true" : "false") << "\n";
            file << "      },\n";
        }

        if (const auto* mat = registry.try_get<MaterialComponent>(entity)) {
            file << "      \"Material\": {\n";
            file << "        \"Albedo\": [" << mat->albedoColor.x << ", " << mat->albedoColor.y << ", "
                 << mat->albedoColor.z << ", " << mat->albedoColor.w << "],\n";
            file << "        \"AlbedoTexture\": \"" << Json::Escape(mat->albedoTexturePath) << "\",\n";
            file << "        \"NormalTexture\": \"" << Json::Escape(mat->normalTexturePath) << "\",\n";
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
            file << ", \"IsTrigger\": " << (box->isTrigger ? "true" : "false") << " },\n";
        }

        // Neither of these was written at all. A sphere collider therefore
        // vanished on Play, on Stop, on undo and on save - silently, because a
        // missing collider looks exactly like a body that was never given one.
        if (const auto* sphere = registry.try_get<SphereColliderComponent>(entity)) {
            file << "      \"SphereCollider\": { \"Radius\": " << sphere->radius
                 << ", \"IsTrigger\": " << (sphere->isTrigger ? "true" : "false") << " },\n";
        }

        if (const auto* listener = registry.try_get<AudioListenerComponent>(entity)) {
            file << "      \"AudioListener\": { \"IsPrimary\": "
                 << (listener->isPrimary ? "true" : "false") << " },\n";
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

        if (const auto* emitter = registry.try_get<ParticleEmitterComponent>(entity)) {
            // Previously written as a bare `true`, so every emitter setting the
            // user had authored was reset by the next load, Play, Stop or undo -
            // and because the text never changed when those fields were edited,
            // the edit was not undoable in the first place.
            //
            // particles/emitAccumulator are runtime state and stay unpersisted,
            // like AudioSourceComponent::voice.
            file << "      \"ParticleEmitter\": {\n";
            file << "        \"MaxParticles\": " << emitter->maxParticles << ",\n";
            file << "        \"EmitRate\": " << emitter->emitRate << ",\n";
            file << "        \"ParticleLifetime\": " << emitter->particleLifetime << ",\n";
            file << "        \"StartColor\": [" << emitter->startColor.x << ", " << emitter->startColor.y << ", "
                 << emitter->startColor.z << ", " << emitter->startColor.w << "],\n";
            file << "        \"EndColor\": [" << emitter->endColor.x << ", " << emitter->endColor.y << ", "
                 << emitter->endColor.z << ", " << emitter->endColor.w << "],\n";
            file << "        \"VelocityRange\": "; writeVec3(file, emitter->velocityRange); file << ",\n";
            file << "        \"ParticleSize\": " << emitter->particleSize << "\n";
            file << "      },\n";
        }

        if (const auto* renderable = registry.try_get<RenderableComponent>(entity)) {
            file << "      \"Renderable\": { \"Visible\": " << (renderable->isVisible ? "true" : "false")
                 << ", \"CastsShadow\": " << (renderable->castsShadow ? "true" : "false") << " },\n";
        }

        file << "      \"HasRenderable\": " << (registry.all_of<RenderableComponent>(entity) ? "true" : "false") << "\n";
        file << "    }" << (i + 1 < entities.size() ? "," : "") << "\n";
    }

    file << "  ]\n}\n";
    return entities.size();
}

// Structural check run BEFORE the registry is touched.
//
// applyScene has to clear before it can populate, so anything that would make
// population fail must be caught while the live scene is still intact. This is
// the same principle that made Deserialize non-destructive on a parse failure,
// extended to a document that parses but is not a usable scene.
bool validateSceneArray(const Json::Array& entities, std::string& error) {
    size_t objectCount = 0;
    for (const auto& node : entities) {
        if (!node.IsObject()) continue;
        ++objectCount;
    }

    size_t index = 0;
    for (const auto& node : entities) {
        if (!node.IsObject()) continue;
        if (node.Has("Parent")) {
            const double raw = node["Parent"].AsNumber(-1.0);
            if (raw < 0.0 || static_cast<size_t>(raw) >= objectCount) {
                error = "entity " + std::to_string(index) +
                        " has an out-of-range Parent index (" + std::to_string(raw) + ")";
                return false;
            }
        }
        ++index;
    }
    return true;
}

// One reader, shared by the on-disk load and the Play-mode restore.
SerializationResult applyScene(entt::registry& registry, const Json::Array& entities,
                               const std::string& source) {
    std::string error;
    if (!validateSceneArray(entities, error)) {
        return { false, source + " is structurally invalid: " + error + " (scene left untouched)." };
    }

    registry.clear();

    // Created up front so a Parent reference resolves even when the parent
    // appears later in the array.
    std::vector<entt::entity> created;
    created.reserve(entities.size());
    for (const auto& node : entities) {
        if (!node.IsObject()) continue;
        created.push_back(registry.create());
    }

    size_t cursor = 0;
    for (const auto& node : entities) {
        if (!node.IsObject()) continue;
        const entt::entity entity = created[cursor++];

        if (node.Has("Parent")) {
            const auto parentIndex = static_cast<size_t>(node["Parent"].AsNumber(-1.0));
            if (parentIndex < created.size() && created[parentIndex] != entity) {
                registry.emplace<HierarchyComponent>(entity, created[parentIndex]);
            }
        }

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
                m["Primitive"].AsString("Cube"), m["Path"].AsString(""), 0u, 0u);
        }

        if (node.Has("Light")) {
            const auto& l = node["Light"];
            auto& light = registry.emplace<LightComponent>(entity);
            light.type = static_cast<int>(l["Type"].AsNumber(0.0));
            light.direction = readVec3(l["Direction"], glm::vec3(0.6f, 1.0f, 0.5f));
            light.color = readVec3(l["Color"], glm::vec3(1.0f));
            light.ambient = readVec3(l["Ambient"], glm::vec3(0.12f));
            light.intensity = l["Intensity"].AsFloat(1.5f);
            light.range = l["Range"].AsFloat(25.0f);
            light.castsShadow = l["CastsShadow"].AsBool(true);
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
            camera.movementSpeed = c["MovementSpeed"].AsFloat(3.5f);
            camera.mouseSensitivity = c["MouseSensitivity"].AsFloat(0.1f);
            // Defaults true, matching the component, so a scene authored before
            // the flag existed still yields a usable camera.
            camera.isPrimary = c["IsPrimary"].AsBool(true);
            camera.updateCameraVectors();
        }

        if (node.Has("Material")) {
            const auto& m = node["Material"];
            auto& material = registry.emplace<MaterialComponent>(entity);
            material.albedoColor = readVec4(m["Albedo"], glm::vec4(1.0f));
            material.albedoTexturePath = m["AlbedoTexture"].AsString("");
            material.normalTexturePath = m["NormalTexture"].AsString("");
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
            box.isTrigger = node["BoxCollider"]["IsTrigger"].AsBool(false);
        }

        if (node.Has("SphereCollider")) {
            auto& sphere = registry.emplace<SphereColliderComponent>(entity);
            sphere.radius = node["SphereCollider"]["Radius"].AsFloat(0.5f);
            sphere.isTrigger = node["SphereCollider"]["IsTrigger"].AsBool(false);
        }

        if (node.Has("AudioListener")) {
            auto& listener = registry.emplace<AudioListenerComponent>(entity);
            listener.isPrimary = node["AudioListener"]["IsPrimary"].AsBool(true);
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
                s["Name"].AsString("RotatorScript"), s["Enabled"].AsBool(true));
        }

        if (node.Has("ParticleEmitter")) {
            auto& emitter = registry.emplace<ParticleEmitterComponent>(entity);
            const auto& e = node["ParticleEmitter"];
            // Older scenes wrote a bare `true` here. AsBool on an object returns
            // the fallback, so those still load - they just get the defaults,
            // which is exactly what they stored.
            if (e.IsObject()) {
                emitter.maxParticles = static_cast<uint32_t>(e["MaxParticles"].AsNumber(100.0));
                emitter.emitRate = e["EmitRate"].AsFloat(10.0f);
                emitter.particleLifetime = e["ParticleLifetime"].AsFloat(2.0f);
                emitter.startColor = readVec4(e["StartColor"], glm::vec4(1.0f, 0.6f, 0.1f, 1.0f));
                emitter.endColor = readVec4(e["EndColor"], glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
                emitter.velocityRange = readVec3(e["VelocityRange"], glm::vec3(0.5f, 2.0f, 0.5f));
                emitter.particleSize = e["ParticleSize"].AsFloat(0.08f);
            }
        }

        if (node["HasRenderable"].AsBool(false)) {
            auto& renderable = registry.emplace<RenderableComponent>(entity);
            if (node.Has("Renderable")) {
                renderable.isVisible = node["Renderable"]["Visible"].AsBool(true);
                renderable.castsShadow = node["Renderable"]["CastsShadow"].AsBool(true);
            }
        }
    }

    return { true, "Loaded " + std::to_string(cursor) + " entities from " + source + "." };
}

} // namespace

std::string SceneSerializer::SerializeToString(entt::registry& registry) {
    std::ostringstream out;
    writeScene(registry, out);
    return out.str();
}

SerializationResult SceneSerializer::DeserializeFromString(entt::registry& registry, const std::string& text) {
    Json::Value root;
    std::string error;
    if (!Json::Parse(text, root, error)) {
        return { false, "snapshot could not be parsed: " + error };
    }
    if (!root.IsObject() || !root["Entities"].IsArray()) {
        return { false, "snapshot has no Entities array" };
    }
    return applyScene(registry, root["Entities"].AsArray(), "snapshot");
}

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

    const size_t count = writeScene(registry, file);
    file.flush();

    if (!file) {
        return { false, "Write error while saving " + filepath + "." };
    }
    return { true, "Saved " + std::to_string(count) + " entities to " + filepath + "." };
}

SerializationResult SceneSerializer::Deserialize(entt::registry& registry, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return { false, "No scene file at " + filepath + "." };
    }

    std::stringstream ss;
    ss << file.rdbuf();

    Json::Value root;
    std::string error;
    if (!Json::Parse(ss.str(), root, error)) {
        // Parse BEFORE touching the registry. The original implementation
        // cleared the scene first and then never read the file at all, so a
        // load destroyed the user's work whatever the file contained.
        return { false, "Could not parse " + filepath + ": " + error + " (scene left untouched)." };
    }

    if (!root.IsObject() || !root["Entities"].IsArray()) {
        return { false, filepath + " is not a scene file (no Entities array); scene left untouched." };
    }

    return applyScene(registry, root["Entities"].AsArray(), filepath);
}

} // namespace Supersonic
