#include "core/SceneSerializer.hpp"
#include "core/AssetVersion.hpp"
#include "core/Components.hpp"
#include "core/Json.hpp"
#include "core/ComponentCodec.hpp"
#include "core/PhysicsSettings.hpp"
#include "core/RenderSettings.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Supersonic {

namespace {

// The per-entity component format lives in ComponentCodec, so scenes and
// prefabs cannot describe the same entity differently.
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

    file << "{\n  \"Version\": " << AssetVersion::kCurrent
         << ",\n  \"Scene\": \"MainScene\",\n";

    // World-level physics, before the entities. It is scene state that no
    // entity owns, so it cannot go through ComponentCodec, and a scene that
    // did not write it reads back as the defaults.
    {
        static const PhysicsSettings kDefaults;
        const PhysicsSettings* stored = registry.ctx().find<PhysicsSettings>();
        const PhysicsSettings& physics = stored ? *stored : kDefaults;
        file << "  \"Physics\": { \"Gravity\": ["
             << physics.gravity.x << ", " << physics.gravity.y << ", "
             << physics.gravity.z << "], \"GroundPlane\": "
             << (physics.hasGroundPlane ? "true" : "false")
             << ", \"GroundPlaneY\": " << physics.groundPlaneY << " },\n";
    }

    // Scene-level rendering, for the same reason as the physics block: it
    // belongs to the scene and no entity owns it.
    {
        static const RenderSettings kRenderDefaults;
        const RenderSettings* storedRender = registry.ctx().find<RenderSettings>();
        const RenderSettings& rendering = storedRender ? *storedRender : kRenderDefaults;
        file << "  \"Rendering\": { \"BloomThreshold\": "
             << rendering.bloomThreshold
             << ", \"BloomSoftKnee\": " << rendering.bloomSoftKnee
             << ", \"BloomIntensity\": " << rendering.bloomIntensity
             << ", \"Exposure\": " << rendering.exposure << " },\n";
    }

    file << "  \"Entities\": [\n";

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

        ComponentCodec::Write(registry, entity, file, "      ");

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

// The world-level half, applied AFTER applyScene, because applyScene clears the
// registry first.
//
// Always assigned, never merely read: registry.clear() leaves the context
// untouched, so a scene loaded over another one would otherwise silently inherit
// the previous scene's gravity and keep its ground plane.
void applyPhysicsSettings(entt::registry& registry, const Json::Value& root) {
    PhysicsSettings physics;

    if (root.Has("Physics")) {
        const auto& node = root["Physics"];
        if (node["Gravity"].IsArray()) {
            const auto& g = node["Gravity"].AsArray();
            if (g.size() == 3) {
                physics.gravity = glm::vec3(static_cast<float>(g[0].AsNumber(0.0)),
                                            static_cast<float>(g[1].AsNumber(-9.81)),
                                            static_cast<float>(g[2].AsNumber(0.0)));
            }
        }
        // The defaults matter: a scene written before this existed has neither
        // key, and the plane it was authored against is now off. That is the
        // intended break - a scene that wants the plane has to say so.
        physics.hasGroundPlane = node["GroundPlane"].AsBool(false);
        physics.groundPlaneY = node["GroundPlaneY"].AsFloat(0.0f);
    }

    registry.ctx().insert_or_assign<PhysicsSettings>(std::move(physics));

    // Same shape, same reasoning: always assigned so a scene loaded over
    // another cannot inherit its look, and absent keys read as the defaults so
    // every scene written before this block existed still loads.
    RenderSettings rendering;
    if (root.Has("Rendering")) {
        const auto& node = root["Rendering"];
        rendering.bloomThreshold = node["BloomThreshold"].AsFloat(rendering.bloomThreshold);
        rendering.bloomSoftKnee = node["BloomSoftKnee"].AsFloat(rendering.bloomSoftKnee);
        rendering.bloomIntensity = node["BloomIntensity"].AsFloat(rendering.bloomIntensity);
        rendering.exposure = node["Exposure"].AsFloat(rendering.exposure);
    }
    registry.ctx().insert_or_assign<RenderSettings>(std::move(rendering));
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

        // Tag included: the codec owns every component, so there is exactly
        // one place that knows how an entity is written and read.
        ComponentCodec::Read(registry, entity, node);
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
    const SerializationResult result = applyScene(registry, root["Entities"].AsArray(), "snapshot");
    if (result.ok) applyPhysicsSettings(registry, root);
    return result;
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

    // Checked before the registry is touched, alongside the parse, and for the
    // same reason: refusing has to leave the open scene exactly as it was.
    const int version = AssetVersion::Read(root);
    if (!AssetVersion::IsReadable(version)) {
        return { false, AssetVersion::TooNewMessage(filepath, version) + " (scene left untouched)" };
    }
    AssetVersion::Migrate(root, version);

    const SerializationResult result = applyScene(registry, root["Entities"].AsArray(), filepath);
    if (result.ok) applyPhysicsSettings(registry, root);
    return result;
}

} // namespace Supersonic
