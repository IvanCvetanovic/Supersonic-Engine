#include "core/SceneSerializer.hpp"
#include "core/AssetVersion.hpp"
#include "core/Components.hpp"
#include "core/Json.hpp"
#include "core/AssetDatabase.hpp"
#include "core/ComponentCodec.hpp"
#include "core/AssetDatabase.hpp"
#include "core/EnvironmentSettings.hpp"
#include "core/PhysicsSettings.hpp"
#include "core/SimulationClock.hpp"
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

    // How often the world thinks, in hertz. One scene, one answer - so it is a
    // setting rather than a component, for the same reason gravity is.
    //
    // Written as a RATE and stored as a step, because a rate is the number a
    // person has an opinion about: HUSK simulates at 20, and nobody has ever
    // wanted to type 0.05.
    {
        const SimulationClock* clock = registry.ctx().find<SimulationClock>();
        const float step = (clock && clock->fixedDelta > 0.0f) ? clock->fixedDelta
                                                               : (1.0f / 60.0f);
        file << "  \"Simulation\": { \"TickRate\": " << (1.0f / step) << " },\n";
    }

    // The surroundings, for the same reason: there is one sky, so making it a
    // component would mean deciding what two of them meant.
    //
    // The HDRI is an asset reference like any other, so it carries its identity
    // beside its path - a renamed environment would otherwise fall silently
    // back to the analytic hemisphere, which looks like somebody turned the
    // lighting off.
    {
        static const EnvironmentSettings kEnvironmentDefaults;
        const EnvironmentSettings* storedEnvironment =
            registry.ctx().find<EnvironmentSettings>();
        const EnvironmentSettings& environment =
            storedEnvironment ? *storedEnvironment : kEnvironmentDefaults;

        file << "  \"Environment\": { \"Hdri\": \"" << Json::Escape(environment.hdriPath)
             << "\"";
        const std::string guid = AssetDatabase::Instance().GuidForPath(environment.hdriPath);
        if (!guid.empty()) file << ", \"HdriGuid\": \"" << guid << "\"";
        file << ", \"Intensity\": " << environment.intensity << " },\n";
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
             << ", \"Exposure\": " << rendering.exposure
             << ", \"FogDensity\": " << rendering.fogDensity
             << ", \"FogColor\": [" << rendering.fogColor[0] << ", "
             << rendering.fogColor[1] << ", " << rendering.fogColor[2]
             << "]"
             // As a WORD rather than a number. A scene file is meant to be
             // read and hand-edited, and "Background": 1 tells a reader
             // nothing about which one that is - and silently means something
             // else the day a third mode is added in the middle.
             << ", \"Background\": \""
             << (rendering.background == RenderSettings::Background::Color ? "Color" : "Sky")
             << "\", \"BackgroundColor\": [" << rendering.backgroundColor[0] << ", "
             << rendering.backgroundColor[1] << ", " << rendering.backgroundColor[2]
             << "] },\n";
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

        // The other end of a joint, as an index for exactly the reason a parent
        // link is one: a handle carries a version and is recycled, so writing
        // it down reattaches the joint to whatever occupies that slot next time
        // the scene is loaded - which would be a rope tied to a lamp.
        //
        // A joint with no connected body is anchored to a point in the WORLD
        // and needs nothing written; that is what the absence of this key means
        // on the way back in.
        if (const auto* joint = registry.try_get<JointComponent>(entity)) {
            if (joint->connectedBody != entt::null) {
                if (const auto it = indexOf.find(joint->connectedBody); it != indexOf.end()) {
                    file << "      \"JointConnectedBody\": " << it->second << ",\n";
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
        if (node.Has("JointConnectedBody")) {
            const double raw = node["JointConnectedBody"].AsNumber(-1.0);
            if (raw < 0.0 || static_cast<size_t>(raw) >= objectCount) {
                error = "entity " + std::to_string(index) +
                        " has an out-of-range JointConnectedBody index (" +
                        std::to_string(raw) + ")";
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
void applyEnvironmentSettings(entt::registry& registry, const Json::Value& root) {
    EnvironmentSettings environment;
    if (root.Has("Environment")) {
        const auto& node = root["Environment"];
        const std::string path = node["Hdri"].AsString("");
        const std::string guid = node["HdriGuid"].AsString("");
        environment.hdriPath =
            (path.empty() && guid.empty()) ? path : AssetDatabase::Instance().Resolve(guid, path);
        environment.intensity = node["Intensity"].AsFloat(1.0f);
    }
    // Absent from every scene written before this existed, which reads as the
    // analytic hemisphere - exactly what those scenes had.
    registry.ctx().insert_or_assign<EnvironmentSettings>(std::move(environment));
}

void applyPhysicsSettings(entt::registry& registry, const Json::Value& root) {
    PhysicsSettings physics;

    // Absent from every scene written before the tick could be authored, and
    // the fallback is 60 - which is what those scenes ran at, because it was
    // the only rate there was.
    //
    // OUTSIDE the Has() check, which it used to be inside, against the rule
    // stated at the top of this file: a scene with no Simulation block silently
    // kept the previous scene's rate, so loading a 20 Hz level and then a 60 Hz
    // one that predates the block ran the second at 20.
    {
        const float rate = root.Has("Simulation")
                               ? root["Simulation"]["TickRate"].AsFloat(60.0f)
                               : 60.0f;
        // Clamped rather than trusted. A rate of zero would divide by zero on
        // the way to a step, and a hand-edited scene is a place people put
        // zero. The ceiling is high enough for anything real and low enough
        // that a typo cannot ask for a million ticks a frame.
        const float clamped = std::clamp(rate, 1.0f, 480.0f);

        // THE WHOLE CLOCK, not just the rate. A fresh SimulationClock is
        // assigned over whatever was there, so the tick counter, the dropped
        // time and the overstep fraction all start from zero.
        //
        // They did not. registry.clear() leaves the context alone and nothing
        // else ever wrote them, so the tick counter was a count of every step
        // the PROCESS had run - it carried across a scene load, across Stop and
        // Play, across loading a different level entirely.
        //
        // That is not merely untidy, and the reason is worth stating because
        // nothing about it is visible from the field: SecondsF() narrows
        // tick * fixedDelta to a float, and a script's elapsed time is derived
        // from it. The narrowing happens BEFORE the subtraction, so the value a
        // script integrates sits on a lattice whose spacing grows with the tick
        // count. The same logical tick of the same scene therefore produced a
        // different number depending on what the process had done beforehand -
        // and a script driving std::sin off it moved to a different place.
        SimulationClock clock;
        clock.fixedDelta = 1.0f / clamped;
        registry.ctx().insert_or_assign<SimulationClock>(std::move(clock));
    }

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
        rendering.fogDensity = node["FogDensity"].AsFloat(rendering.fogDensity);
        if (node["FogColor"].IsArray()) {
            const auto& c = node["FogColor"].AsArray();
            if (c.size() == 3) {
                for (int i = 0; i < 3; ++i) {
                    rendering.fogColor[i] = c[static_cast<size_t>(i)].AsFloat(rendering.fogColor[i]);
                }
            }
        }

        // ANYTHING THAT IS NOT "Color" IS THE SKY, including a missing key,
        // an empty string and a word nobody recognises. Every scene written
        // before this existed has no key here and must keep the background it
        // has always had, so the default has to be the one that changes
        // nothing - and a typo falling back to the visible default is easier
        // to spot than one falling back to a flat colour, which looks
        // deliberate.
        rendering.background = node["Background"].AsString("Sky") == "Color"
                                   ? RenderSettings::Background::Color
                                   : RenderSettings::Background::Sky;

        if (node["BackgroundColor"].IsArray()) {
            const auto& c = node["BackgroundColor"].AsArray();
            if (c.size() == 3) {
                for (int i = 0; i < 3; ++i) {
                    rendering.backgroundColor[i] =
                        c[static_cast<size_t>(i)].AsFloat(rendering.backgroundColor[i]);
                }
            }
        }
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

    // Per LOAD, so the count below belongs to this scene and not to whatever
    // was read before it.
    AssetDatabase::Instance().ResetStats();

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

        // After the read, because the component has to exist before its other
        // end can be filled in. `created` is populated up front, so a joint may
        // point forwards in the array as well as backwards.
        if (node.Has("JointConnectedBody")) {
            if (auto* joint = registry.try_get<JointComponent>(entity)) {
                const auto index = static_cast<size_t>(
                    node["JointConnectedBody"].AsNumber(-1.0));
                if (index < created.size() && created[index] != entity) {
                    joint->connectedBody = created[index];
                }
            }
        }
    }

    // Everything the scene asked for that no asset answers to, in one line
    // rather than one per reference.
    AssetDatabase::Instance().ReportUnresolved();

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
    if (result.ok) applyEnvironmentSettings(registry, root);
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
    if (result.ok) applyEnvironmentSettings(registry, root);
    return result;
}

} // namespace Supersonic
