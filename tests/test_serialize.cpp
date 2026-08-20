// Regression tests for scene persistence.
//
// Deserialize used to read the file into a string it never looked at, wipe the
// registry, and fabricate three hardcoded entities while reporting success.
// Loading a scene destroyed the user's work and everything in the save file.

#include "TestHarness.hpp"
#include "core/Json.hpp"
#include "core/SceneSerializer.hpp"
#include "core/PrefabSerializer.hpp"
#include "core/ComponentCodec.hpp"
#include "core/AssetVersion.hpp"
#include "core/Components.hpp"
#include "core/PhysicsSettings.hpp"
#include "core/RenderSettings.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Supersonic;

static const std::string kScene = "test_scene_tmp.scene";
static const std::string kPrefab = "test_prefab_tmp.prefab";

static void testJsonRoundTrip() {
    const std::string text = R"({"a": 1.5, "b": "hi", "c": [1, 2, 3], "d": true, "e": null})";
    Json::Value root;
    std::string error;

    CHECK_MSG(Json::Parse(text, root, error), error);
    CHECK(root.IsObject());
    CHECK_NEAR(root["a"].AsFloat(), 1.5f);
    CHECK(root["b"].AsString() == "hi");
    CHECK_EQ(root["c"].AsArray().size(), size_t{3});
    CHECK(root["d"].AsBool());
    CHECK_MSG(!root.Has("missing"), "absent keys must report absent");
    CHECK_NEAR(root["missing"].AsFloat(42.0f), 42.0f);
}

static void testJsonRejectsGarbage() {
    Json::Value root;
    std::string error;
    CHECK(!Json::Parse("{\"a\": }", root, error));
    CHECK(!Json::Parse("{\"a\": 1,", root, error));
    CHECK(!Json::Parse("not json at all", root, error));
}

static void testTagEscaping() {
    // A tag is free-form text typed into the Inspector. Writing it raw produced
    // unparseable files the moment someone used a quote, and let a crafted tag
    // inject document structure.
    const std::string nasty = R"(My "Best" Cube\ , "Transform": {)";

    const std::string doc = "{\"Tag\": \"" + Json::Escape(nasty) + "\"}";
    Json::Value root;
    std::string error;

    CHECK_MSG(Json::Parse(doc, root, error), "escaped tag must round-trip: " + error);
    CHECK_MSG(root["Tag"].AsString() == nasty, "tag must survive escaping unchanged");
}

static void testSceneRoundTrip() {
    entt::registry source;

    const auto cube = source.create();
    source.emplace<TagComponent>(cube, R"(Weird "Name" \ Cube)");
    auto& transform = source.emplace<TransformComponent>(cube);
    transform.position = glm::vec3(1.0f, 2.0f, 3.0f);
    transform.rotation = glm::vec3(0.1f, 0.2f, 0.3f);
    transform.scale = glm::vec3(2.0f, 2.0f, 2.0f);
    source.emplace<MeshComponent>(cube, "Sphere", "", 0u, 0u);
    auto& material = source.emplace<MaterialComponent>(cube);
    material.roughness = 0.25f;
    material.metallic = 0.75f;
    source.emplace<RenderableComponent>(cube);

    const auto light = source.create();
    source.emplace<TagComponent>(light, "Sun");
    auto& lightComp = source.emplace<LightComponent>(light);
    lightComp.intensity = 3.25f;

    const auto speaker = source.create();
    source.emplace<TagComponent>(speaker, "Speaker");
    auto& audio = source.emplace<AudioSourceComponent>(speaker);
    audio.soundFile = "assets/audio/ambient.wav";
    audio.volume = 0.42f;
    audio.pitch = 1.25f;
    audio.loop = false;
    audio.referenceDistance = 2.5f;
    audio.maxDistance = 66.0f;

    const auto saved = SceneSerializer::Serialize(source, kScene);
    CHECK_MSG(saved.ok, saved.message);

    entt::registry loaded;
    const auto result = SceneSerializer::Deserialize(loaded, kScene);
    CHECK_MSG(result.ok, result.message);

    size_t count = 0;
    bool foundCube = false;
    bool foundLight = false;
    bool foundAudio = false;

    for (auto entity : loaded.view<entt::entity>()) {
        ++count;
        const auto* tag = loaded.try_get<TagComponent>(entity);
        if (!tag) continue;

        if (tag->tag == R"(Weird "Name" \ Cube)") {
            foundCube = true;
            const auto* t = loaded.try_get<TransformComponent>(entity);
            CHECK(t != nullptr);
            if (t) {
                CHECK_NEAR(t->position.x, 1.0f);
                CHECK_NEAR(t->position.z, 3.0f);
                CHECK_NEAR(t->rotation.y, 0.2f);
                CHECK_NEAR(t->scale.y, 2.0f);
            }
            const auto* m = loaded.try_get<MeshComponent>(entity);
            CHECK(m != nullptr);
            if (m) CHECK_MSG(m->primitiveType == "Sphere", "mesh type must survive a round trip");

            const auto* mat = loaded.try_get<MaterialComponent>(entity);
            CHECK(mat != nullptr);
            if (mat) CHECK_NEAR(mat->metallic, 0.75f);

            CHECK(loaded.all_of<RenderableComponent>(entity));
        } else if (tag->tag == "Sun") {
            foundLight = true;
            const auto* l = loaded.try_get<LightComponent>(entity);
            CHECK(l != nullptr);
            if (l) CHECK_NEAR(l->intensity, 3.25f);
        } else if (tag->tag == "Speaker") {
            foundAudio = true;
            // Audio sources were originally left out of the writer entirely, so
            // a save/load round trip silently dropped every one of them.
            const auto* a = loaded.try_get<AudioSourceComponent>(entity);
            CHECK(a != nullptr);
            if (a) {
                CHECK_MSG(a->soundFile == "assets/audio/ambient.wav", "clip path must survive");
                CHECK_NEAR(a->volume, 0.42f);
                CHECK_NEAR(a->pitch, 1.25f);
                CHECK_MSG(!a->loop, "loop flag must survive");
                CHECK_NEAR(a->referenceDistance, 2.5f);
                CHECK_NEAR(a->maxDistance, 66.0f);
            }
        }
    }

    CHECK_EQ(count, size_t{3});
    CHECK_MSG(foundCube, "the cube must come back with its data");
    CHECK_MSG(foundLight, "the light must come back with its data");
    CHECK_MSG(foundAudio, "the audio source must come back with its data");

    std::remove(kScene.c_str());
}

static void testDeletedEntitiesDoNotBreakJson() {
    // storage<entt::entity>().size() counts released entities under EnTT's
    // swap_only policy, so the trailing-comma guard was comparing against the
    // wrong total and emitted invalid JSON.
    entt::registry registry;
    std::vector<entt::entity> created;
    for (int i = 0; i < 5; ++i) {
        const auto e = registry.create();
        registry.emplace<TagComponent>(e, "E" + std::to_string(i));
        created.push_back(e);
    }
    registry.destroy(created[1]);
    registry.destroy(created[3]);

    const auto saved = SceneSerializer::Serialize(registry, kScene);
    CHECK_MSG(saved.ok, saved.message);

    entt::registry loaded;
    const auto result = SceneSerializer::Deserialize(loaded, kScene);
    CHECK_MSG(result.ok, "file must still be valid JSON after deletions: " + result.message);

    size_t count = 0;
    for (auto entity : loaded.view<entt::entity>()) { (void)entity; ++count; }
    CHECK_EQ(count, size_t{3});

    std::remove(kScene.c_str());
}

static void testMalformedSceneLeavesRegistryIntact() {
    // The single most destructive behaviour in the old implementation: it
    // cleared the registry before looking at the file.
    {
        std::ofstream f(kScene);
        f << "{ this is not valid json";
    }

    entt::registry registry;
    const auto keep = registry.create();
    registry.emplace<TagComponent>(keep, "Precious");

    const auto result = SceneSerializer::Deserialize(registry, kScene);
    CHECK_MSG(!result.ok, "a malformed scene must report failure");

    size_t count = 0;
    for (auto entity : registry.view<entt::entity>()) { (void)entity; ++count; }
    CHECK_MSG(count == 1, "a failed load must NOT destroy the existing scene");

    std::remove(kScene.c_str());
}

static void testMissingSceneReportsFailure() {
    entt::registry registry;
    const auto result = SceneSerializer::Deserialize(registry, "no_such_scene_98765.scene");
    CHECK_MSG(!result.ok, "loading a missing file must fail");
}

static void testSerializeCreatesParentDirectory() {
    const std::string nested = "test_tmp_dir/inner/scene.scene";
    entt::registry registry;
    registry.emplace<TagComponent>(registry.create(), "A");

    const auto saved = SceneSerializer::Serialize(registry, nested);
    CHECK_MSG(saved.ok, "saving must create missing parent directories: " + saved.message);
    CHECK(std::filesystem::exists(nested));

    std::error_code ec;
    std::filesystem::remove_all("test_tmp_dir", ec);
}

static void testPrefabRoundTrip() {
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TagComponent>(entity, "Turret");
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(4.0f, 5.0f, 6.0f);
    registry.emplace<MeshComponent>(entity, "Sphere", "", 0u, 0u);
    auto& box = registry.emplace<BoxColliderComponent>(entity);
    box.size = glm::vec3(3.0f, 1.0f, 2.0f);
    registry.emplace<RenderableComponent>(entity);

    const auto saved = PrefabSerializer::SavePrefab(registry, entity, kPrefab);
    CHECK_MSG(saved.ok, saved.message);

    SerializationResult loadResult;
    const auto clone = PrefabSerializer::InstantiatePrefab(registry, kPrefab, &loadResult);
    CHECK_MSG(loadResult.ok, loadResult.message);
    CHECK(clone != entt::null);

    if (clone != entt::null) {
        CHECK(registry.get<TagComponent>(clone).tag == "Turret");
        CHECK_NEAR(registry.get<TransformComponent>(clone).position.y, 5.0f);
        CHECK(registry.get<MeshComponent>(clone).primitiveType == "Sphere");
        CHECK_NEAR(registry.get<BoxColliderComponent>(clone).size.x, 3.0f);
    }

    std::remove(kPrefab.c_str());
}


// Builds one entity carrying every component the engine serialises.
//
// ADD NEW COMPONENTS HERE. Both prefab tests below draw their coverage from
// this builder: the key-set comparison can only report a component that one
// writer emits and the other does not, and a component absent from this entity
// is emitted by neither.
static entt::entity makeFullyLoadedEntity(entt::registry& registry) {
    const auto entity = registry.create();
    registry.emplace<TagComponent>(entity, "Turret");

    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(4.0f, 5.0f, 6.0f);
    transform.rotation = glm::vec3(0.0f, 90.0f, 0.0f);
    transform.scale = glm::vec3(2.0f, 2.0f, 2.0f);

    registry.emplace<MeshComponent>(entity, "Sphere", "", 0u, 0u);
    registry.emplace<RenderableComponent>(entity);

    auto& material = registry.emplace<MaterialComponent>(entity);
    material.albedoColor = glm::vec4(0.9f, 0.2f, 0.1f, 1.0f);
    material.albedoTexturePath = "assets/textures/turret.png";
    material.metallic = 0.75f;
    material.roughness = 0.25f;

    auto& light = registry.emplace<LightComponent>(entity);
    light.type = 1;
    light.intensity = 3.5f;
    light.range = 12.0f;
    light.ambient = glm::vec3(0.2f, 0.3f, 0.5f);
    light.ambientGround = glm::vec3(0.4f, 0.2f, 0.1f);
    light.innerAngle = 0.21f;
    light.outerAngle = 0.44f;
    light.castsShadow = false;

    auto& camera = registry.emplace<CameraComponent>(entity);
    camera.fov = 72.0f;
    camera.farPlane = 500.0f;

    auto& script = registry.emplace<ScriptComponent>(entity);
    script.scriptName = "OscillatorScript";
    script.isEnabled = false;

    auto& audio = registry.emplace<AudioSourceComponent>(entity);
    audio.soundFile = "assets/audio/turret.wav";
    audio.volume = 0.35f;
    audio.loop = false;
    audio.maxDistance = 77.0f;

    registry.emplace<AudioListenerComponent>(entity).isPrimary = false;

    auto& animator = registry.emplace<AnimatorComponent>(entity);
    animator.clipName = "Fire";
    animator.speed = 1.75f;
    animator.loop = false;
    animator.blendDuration = 0.6f;

    auto& body = registry.emplace<RigidBodyComponent>(entity);
    body.mass = 12.0f;
    body.useGravity = false;
    body.restitution = 0.85f;
    body.friction = 1.2f;
    body.linearDamping = 0.25f;
    body.angularVelocity = glm::vec3(0.0f, 3.5f, 0.0f);
    body.freezeRotation = true;

    auto& box = registry.emplace<BoxColliderComponent>(entity);
    box.size = glm::vec3(3.0f, 1.0f, 2.0f);
    box.isTrigger = true;

    registry.emplace<SphereColliderComponent>(entity).radius = 4.25f;

    auto& capsule = registry.emplace<CapsuleColliderComponent>(entity);
    capsule.radius = 0.35f;
    capsule.height = 1.85f;
    capsule.center = glm::vec3(0.0f, 0.9f, 0.0f);

    auto& hud = registry.emplace<UITextComponent>(entity);
    hud.text = "Ammo 12/30";
    hud.anchor = UIAnchor::BottomRight;
    hud.offset = glm::vec2(48.0f, 32.0f);
    hud.fontSize = 44.0f;
    hud.shadow = false;

    auto& bar = registry.emplace<UIPanelComponent>(entity);
    bar.anchor = UIAnchor::TopCenter;
    bar.size = glm::vec2(480.0f, 24.0f);
    bar.fill = 0.35f;
    bar.drawTrack = true;
    bar.cornerRadius = 12.0f;

    auto& menuButton = registry.emplace<UIButtonComponent>(entity);
    menuButton.label = "Resume";
    menuButton.anchor = UIAnchor::MiddleLeft;
    menuButton.size = glm::vec2(220.0f, 48.0f);
    menuButton.fontSize = 26.0f;
    menuButton.enabled = false;

    auto& emitter = registry.emplace<ParticleEmitterComponent>(entity);
    emitter.maxParticles = 512u;
    emitter.emitRate = 33.0f;
    emitter.particleSize = 0.5f;

    return entity;
}

// The failure this is here to catch is silent: a prefab that comes back looking
// fine but missing whatever the prefab writer never learned about. Rather than
// list the components by hand - which is exactly the enumeration that fell out
// of date - compare against what the scene writer emits for the same entity.
// Any component added to one path and not the other fails here.
//
// Both paths call ComponentCodec today, so this cannot fail as written. It is
// here for the day someone re-forks the writer "just for prefabs", which is
// exactly how the two formats diverged the first time.
static void testPrefabCarriesEverythingASceneDoes() {
    entt::registry registry;
    const auto entity = makeFullyLoadedEntity(registry);

    const auto saved = PrefabSerializer::SavePrefab(registry, entity, kPrefab);
    CHECK_MSG(saved.ok, saved.message);

    std::ifstream in(kPrefab);
    std::stringstream buffer;
    buffer << in.rdbuf();
    in.close();

    Json::Value prefab;
    std::string error;
    CHECK_MSG(Json::Parse(buffer.str(), prefab, error), error);

    Json::Value scene;
    CHECK_MSG(Json::Parse(SceneSerializer::SerializeToString(registry), scene, error), error);
    const auto& sceneEntities = scene["Entities"].AsArray();
    CHECK(sceneEntities.size() == 1);

    if (sceneEntities.size() == 1) {
        for (const auto& [key, value] : sceneEntities[0].AsObject()) {
            // Parent is an index into a scene's entity array; a standalone
            // prefab has nothing to index into, so it is the one exception.
            if (key == "Parent") continue;
            CHECK_MSG(prefab.Has(key),
                      "a scene stores \"" + key + "\" but a prefab of the same entity does not");
        }
    }

    std::remove(kPrefab.c_str());
}

// And the values have to survive, not just the keys.
static void testPrefabRoundTripsEveryField() {
    entt::registry registry;
    const auto entity = makeFullyLoadedEntity(registry);

    const auto saved = PrefabSerializer::SavePrefab(registry, entity, kPrefab);
    CHECK_MSG(saved.ok, saved.message);

    SerializationResult loadResult;
    const auto clone = PrefabSerializer::InstantiatePrefab(registry, kPrefab, &loadResult);
    CHECK_MSG(loadResult.ok, loadResult.message);
    CHECK(clone != entt::null);

    // try_get rather than get: a prefab that dropped a component has to name
    // which one, not crash the suite on undefined behaviour and leave the
    // reason to a debugger.
    if (clone != entt::null) {
        CHECK(clone != entity);

        const auto* transform = registry.try_get<TransformComponent>(clone);
        CHECK(transform != nullptr);
        if (transform) CHECK_NEAR(transform->scale.x, 2.0f);

        const auto* material = registry.try_get<MaterialComponent>(clone);
        CHECK(material != nullptr);
        if (material) {
            CHECK_NEAR(material->metallic, 0.75f);
            CHECK(material->albedoTexturePath == "assets/textures/turret.png");
        }

        // Every one of these was dropped by the five-component writer.
        const auto* light = registry.try_get<LightComponent>(clone);
        CHECK_MSG(light != nullptr, "prefab lost its LightComponent");
        if (light) {
            CHECK(light->type == 1);
            CHECK_NEAR(light->intensity, 3.5f);
            CHECK_NEAR(light->ambient.z, 0.5f);
            CHECK_NEAR(light->ambientGround.x, 0.4f);
            CHECK_NEAR(light->innerAngle, 0.21f);
            CHECK_NEAR(light->outerAngle, 0.44f);
            CHECK_MSG(!light->castsShadow, "castsShadow must survive as false, not default to true");
        }

        const auto* camera = registry.try_get<CameraComponent>(clone);
        CHECK_MSG(camera != nullptr, "prefab lost its CameraComponent");
        if (camera) CHECK_NEAR(camera->fov, 72.0f);

        const auto* script = registry.try_get<ScriptComponent>(clone);
        CHECK_MSG(script != nullptr, "prefab lost its ScriptComponent");
        if (script) {
            CHECK(script->scriptName == "OscillatorScript");
            CHECK(!script->isEnabled);
            // Per-entity runtime state must NOT come across, or a cloned
            // oscillator swings around the original's position instead of
            // its own.
            CHECK_MSG(!script->baselineCaptured,
                      "a clone must capture its own baseline, not inherit one");
        }

        const auto* audio = registry.try_get<AudioSourceComponent>(clone);
        CHECK_MSG(audio != nullptr, "prefab lost its AudioSourceComponent");
        if (audio) {
            CHECK(audio->soundFile == "assets/audio/turret.wav");
            CHECK_NEAR(audio->volume, 0.35f);
            CHECK(!audio->loop);
            // voice is an AudioSystem-owned handle. Copying it would leave two
            // entities driving one voice, which goes wrong silently.
            CHECK_MSG(audio->voice == 0xFFFFFFFFu,
                      "a clone must not inherit the original's voice handle");
        }

        const auto* listener = registry.try_get<AudioListenerComponent>(clone);
        CHECK_MSG(listener != nullptr, "prefab lost its AudioListenerComponent");
        if (listener) CHECK(!listener->isPrimary);

        const auto* animator = registry.try_get<AnimatorComponent>(clone);
        CHECK_MSG(animator != nullptr, "prefab lost its AnimatorComponent");
        if (animator) {
            CHECK(animator->clipName == "Fire");
            CHECK_NEAR(animator->speed, 1.75f);
            CHECK_NEAR(animator->blendDuration, 0.6f);
            // Transition state is derived, and restoring a half-finished
            // cross-fade would put the rig in a pose the scene was never in.
            CHECK_MSG(animator->blendRemaining == 0.0f,
                      "a clone must not resume the original's transition");
        }

        const auto* body = registry.try_get<RigidBodyComponent>(clone);
        CHECK_MSG(body != nullptr, "prefab lost its RigidBodyComponent");
        if (body) {
            CHECK_NEAR(body->mass, 12.0f);
            CHECK(!body->useGravity);
            CHECK_NEAR(body->restitution, 0.85f);
            CHECK_NEAR(body->friction, 1.2f);
            CHECK_NEAR(body->linearDamping, 0.25f);
            CHECK_NEAR(body->angularVelocity.y, 3.5f);
            CHECK_MSG(body->freezeRotation,
                      "a body that must not tip over has to come back that way");
        }

        const auto* box = registry.try_get<BoxColliderComponent>(clone);
        CHECK_MSG(box != nullptr, "prefab lost its BoxColliderComponent");
        if (box) {
            CHECK_MSG(box->isTrigger,
                      "a trigger volume that comes back solid is a gameplay bug, "
                      "not a cosmetic one");
        }

        const auto* sphere = registry.try_get<SphereColliderComponent>(clone);
        CHECK_MSG(sphere != nullptr, "prefab lost its SphereColliderComponent");
        if (sphere) CHECK_NEAR(sphere->radius, 4.25f);

        const auto* capsule = registry.try_get<CapsuleColliderComponent>(clone);
        CHECK_MSG(capsule != nullptr, "prefab lost its CapsuleColliderComponent");
        if (capsule) {
            CHECK_NEAR(capsule->radius, 0.35f);
            // The one field a character controller is built around: a capsule
            // that comes back the wrong height stands with its feet in the
            // floor or hovers above it.
            CHECK_NEAR(capsule->height, 1.85f);
            CHECK_NEAR(capsule->center.y, 0.9f);
        }

        const auto* hud = registry.try_get<UITextComponent>(clone);
        CHECK_MSG(hud != nullptr, "prefab lost its UITextComponent");
        if (hud) {
            CHECK(hud->text == "Ammo 12/30");
            CHECK_MSG(hud->anchor == UIAnchor::BottomRight,
                      "an anchor that resets to TopLeft moves the whole HUD");
            CHECK_NEAR(hud->fontSize, 44.0f);
            CHECK(!hud->shadow);
        }

        const auto* bar = registry.try_get<UIPanelComponent>(clone);
        CHECK_MSG(bar != nullptr, "prefab lost its UIPanelComponent");
        if (bar) {
            CHECK_NEAR(bar->fill, 0.35f);
            CHECK(bar->drawTrack);
            CHECK_NEAR(bar->size.x, 480.0f);
        }

        const auto* menu = registry.try_get<UIButtonComponent>(clone);
        CHECK_MSG(menu != nullptr, "prefab lost its UIButtonComponent");
        if (menu) {
            CHECK(menu->label == "Resume");
            CHECK(menu->anchor == UIAnchor::MiddleLeft);
            CHECK_NEAR(menu->size.x, 220.0f);
            CHECK_MSG(!menu->enabled, "a disabled button must come back disabled");
            // Interaction state is derived; a clone must not arrive pressed.
            CHECK_MSG(!menu->pressed && !menu->clicked,
                      "a clone must not inherit a press that was in flight");
        }

        const auto* emitter = registry.try_get<ParticleEmitterComponent>(clone);
        CHECK_MSG(emitter != nullptr, "prefab lost its ParticleEmitterComponent");
        if (emitter) CHECK(emitter->maxParticles == 512u);
    }

    std::remove(kPrefab.c_str());
}

static void testMissingPrefabReturnsNull() {
    // The old version never opened the file and returned a valid entity for a
    // path that did not exist, making a missing asset indistinguishable from a
    // working one.
    entt::registry registry;
    SerializationResult result;
    const auto entity = PrefabSerializer::InstantiatePrefab(registry, "no_such_prefab_4242.prefab", &result);

    CHECK_MSG(entity == entt::null, "a missing prefab must not produce an entity");
    CHECK(!result.ok);
}

static void testReadLayersOverAnEntityThatAlreadyHasComponents() {
    // ComponentCodec::Read is documented as applying onto an EXISTING entity,
    // leaving unmentioned components alone. Every branch used plain emplace,
    // which is an ENTT_ASSERT in a debug build and undefined behaviour in a
    // release one the moment the component is already there. It went unnoticed
    // because the two callers that matter - loading a scene and instantiating a
    // prefab - both start from a fresh entity.
    entt::registry registry;
    const auto entity = registry.create();

    auto& existing = registry.emplace<TransformComponent>(entity);
    existing.position = glm::vec3(9.0f, 9.0f, 9.0f);
    registry.emplace<TagComponent>(entity, "Original");

    Json::Value node;
    std::string error;
    CHECK(Json::Parse(R"({"Transform": {"Position": [1.0, 2.0, 3.0],
                                        "Rotation": [0.0, 0.0, 0.0],
                                        "Scale": [1.0, 1.0, 1.0]}})", node, error));

    ComponentCodec::Read(registry, entity, node);

    const auto& transform = registry.get<TransformComponent>(entity);
    CHECK_MSG(::test::nearly(transform.position.x, 1.0f),
              "Read must overwrite a component the entity already had");
    CHECK_NEAR(transform.position.z, 3.0f);

    // And must leave alone what the node did not mention.
    CHECK_MSG(registry.all_of<TagComponent>(entity),
              "a component the node does not describe must survive");
    CHECK(registry.get<TagComponent>(entity).tag == "Original");
}

namespace {
// free_list(), not size(): EnTT's swap_only deletion leaves released handles in
// the storage, so size() counts entities that no longer exist.
size_t countEntities(entt::registry& registry) {
    return static_cast<size_t>(registry.storage<entt::entity>().free_list());
}
} // namespace

static void testUnversionedScenesStillLoad() {
    // Every scene written before versioning existed has no Version key, which
    // reads as 0. Those must keep loading: refusing them would make adding the
    // field itself the breaking change it exists to prevent.
    const std::string path = "test_unversioned_tmp.scene";
    {
        std::ofstream f(path);
        f << R"({"Scene": "Old", "Entities": [
                  {"Tag": "Cube",
                   "Transform": {"Position": [4.0, 0.0, 0.0],
                                 "Rotation": [0.0, 0.0, 0.0],
                                 "Scale": [1.0, 1.0, 1.0]}}]})";
    }

    entt::registry registry;
    const auto result = SceneSerializer::Deserialize(registry, path);
    std::remove(path.c_str());

    CHECK_MSG(result.ok, "a scene with no Version must still load: " + result.message);
    CHECK_EQ(countEntities(registry), size_t{1});
}

static void testAFutureSceneIsRefusedAndChangesNothing() {
    // The case worth spending anything on. Loading a file from a newer build
    // and silently dropping the fields this one does not know about is how a
    // user loses work by opening a scene in the wrong build and saving it.
    const std::string path = "test_future_tmp.scene";
    {
        std::ofstream f(path);
        f << R"({"Version": 9999, "Scene": "FromTheFuture", "Entities": [{"Tag": "Ghost"}]})";
    }

    entt::registry registry;
    const auto existing = registry.create();
    registry.emplace<TagComponent>(existing, "AlreadyHere");

    const auto result = SceneSerializer::Deserialize(registry, path);
    std::remove(path.c_str());

    CHECK_MSG(!result.ok, "a newer format version must be refused");
    CHECK_MSG(result.message.find("newer build") != std::string::npos,
              "the message should say why: " + result.message);

    // And, like a parse failure, it must leave the open scene alone.
    CHECK_EQ(countEntities(registry), size_t{1});
    CHECK(registry.get<TagComponent>(existing).tag == "AlreadyHere");
}

static void testSavedScenesCarryTheCurrentVersion() {
    const std::string path = "test_version_roundtrip_tmp.scene";
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TagComponent>(entity, "Cube");
    registry.emplace<TransformComponent>(entity);

    CHECK(SceneSerializer::Serialize(registry, path).ok);

    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    in.close();

    Json::Value root;
    std::string error;
    CHECK(Json::Parse(buffer.str(), root, error));
    CHECK_EQ(static_cast<int>(root["Version"].AsNumber(0.0)), AssetVersion::kCurrent);

    entt::registry reloaded;
    CHECK(SceneSerializer::Deserialize(reloaded, path).ok);
    std::remove(path.c_str());
    CHECK_EQ(countEntities(reloaded), size_t{1});
}


static void testTheScenesLookSurvivesARoundTrip() {
    // Bloom lived on the BloomPass, which the scene file never sees, so a look
    // tuned in the editor lasted until the next reload. Moving it into the
    // registry also puts it inside undo, redo and the Play snapshot - all three
    // work by serialising the registry, and none of them had to learn about
    // bloom to get it.
    const std::string path = "test_rendering_tmp.scene";

    {
        entt::registry registry;
        RenderSettings look;
        look.bloomThreshold = 2.75f;
        look.bloomSoftKnee = 0.125f;
        look.bloomIntensity = 1.5f;
        look.exposure = 0.4f;
        look.fogDensity = 0.033f;
        look.fogColor[0] = 0.9f;
        look.fogColor[1] = 0.2f;
        look.fogColor[2] = 0.1f;
        registry.ctx().insert_or_assign<RenderSettings>(std::move(look));

        const auto entity = registry.create();
        registry.emplace<TagComponent>(entity, "Lit");
        registry.emplace<TransformComponent>(entity);

        CHECK_MSG(SceneSerializer::Serialize(registry, path).ok, "the scene must save");
    }

    entt::registry loaded;
    const auto result = SceneSerializer::Deserialize(loaded, path);
    std::remove(path.c_str());

    CHECK_MSG(result.ok, "and load: " + result.message);
    CHECK_MSG(loaded.ctx().contains<RenderSettings>(), "with its look");

    const auto& look = loaded.ctx().get<RenderSettings>();
    CHECK_NEAR(look.bloomThreshold, 2.75f);
    CHECK_NEAR(look.bloomSoftKnee, 0.125f);
    CHECK_NEAR(look.bloomIntensity, 1.5f);
    CHECK_NEAR(look.exposure, 0.4f);

    // Fog too, colour included - a density that survives while its colour
    // resets to the default is a scene that loads grey haze instead of the
    // sunset it was authored with.
    CHECK_NEAR(look.fogDensity, 0.033f);
    CHECK_NEAR(look.fogColor[0], 0.9f);
    CHECK_NEAR(look.fogColor[1], 0.2f);
    CHECK_NEAR(look.fogColor[2], 0.1f);
}

static void testASceneWithNoLookGetsTheDefaultOne() {
    // Every scene written before this block existed. They must load, and they
    // must not inherit whatever the previously open scene was tuned to.
    entt::registry registry;
    RenderSettings stale;
    stale.bloomThreshold = 9.0f;
    registry.ctx().insert_or_assign<RenderSettings>(std::move(stale));

    const std::string path = "test_nolook_tmp.scene";
    {
        std::ofstream f(path);
        f << R"({"Version": )" << AssetVersion::kCurrent << R"(, "Scene": "Old", "Entities": []})";
    }

    const auto result = SceneSerializer::Deserialize(registry, path);
    std::remove(path.c_str());
    CHECK_MSG(result.ok, "must load: " + result.message);

    CHECK_MSG(std::fabs(registry.ctx().get<RenderSettings>().bloomThreshold - 1.0f) < 1e-6f,
              "a scene with no Rendering block must not keep the last one's look");
}

static void testWorldPhysicsSurvivesARoundTrip() {
    // Gravity and the ground plane belong to the scene, not to any entity, so
    // they cannot go through ComponentCodec and are the one thing in the file a
    // per-entity round trip would never notice going missing.
    const std::string path = "test_physics_tmp.scene";

    {
        entt::registry registry;
        PhysicsSettings settings;
        settings.gravity = glm::vec3(0.0f, -1.62f, 0.0f); // the moon
        settings.hasGroundPlane = true;
        settings.groundPlaneY = -12.5f;
        registry.ctx().insert_or_assign<PhysicsSettings>(std::move(settings));

        const auto entity = registry.create();
        registry.emplace<TagComponent>(entity, "Lander");
        registry.emplace<TransformComponent>(entity);

        CHECK_MSG(SceneSerializer::Serialize(registry, path).ok, "the scene must save");
    }

    entt::registry loaded;
    const auto result = SceneSerializer::Deserialize(loaded, path);
    std::remove(path.c_str());

    CHECK_MSG(result.ok, "the scene must load: " + result.message);
    CHECK_MSG(loaded.ctx().contains<PhysicsSettings>(),
              "loading a scene must bring its physics settings with it");

    const auto& settings = loaded.ctx().get<PhysicsSettings>();
    CHECK_NEAR(settings.gravity.y, -1.62f);
    CHECK_MSG(settings.hasGroundPlane, "the ground plane switch must survive");
    CHECK_NEAR(settings.groundPlaneY, -12.5f);
}

static void testLoadingASceneReplacesTheWorldPhysicsRatherThanKeepingIt() {
    // registry.clear() does not touch the context, so a scene loaded over
    // another one would inherit the previous scene's gravity and keep a ground
    // plane it never asked for. That is the kind of leak that shows up as one
    // level behaving like the level before it.
    entt::registry registry;
    PhysicsSettings stale;
    stale.gravity = glm::vec3(0.0f, -30.0f, 0.0f);
    stale.hasGroundPlane = true;
    registry.ctx().insert_or_assign<PhysicsSettings>(std::move(stale));

    // Written at the CURRENT version deliberately. An older scene is migrated
    // into keeping the ground plane it was authored against, which is the test
    // below; this one is about a current scene that simply says nothing.
    const std::string path = "test_physics_plain_tmp.scene";
    {
        std::ofstream f(path);
        f << R"({"Version": )" << AssetVersion::kCurrent << R"(, "Scene": "Plain", "Entities": [
                  {"Tag": "Cube",
                   "Transform": {"Position": [0.0, 0.0, 0.0],
                                 "Rotation": [0.0, 0.0, 0.0],
                                 "Scale": [1.0, 1.0, 1.0]}}]})";
    }

    const auto result = SceneSerializer::Deserialize(registry, path);
    std::remove(path.c_str());
    CHECK_MSG(result.ok, "the scene must load: " + result.message);

    const auto& settings = registry.ctx().get<PhysicsSettings>();
    CHECK_NEAR(settings.gravity.y, -9.81f);
    CHECK_MSG(!settings.hasGroundPlane,
              "a scene that does not ask for a ground plane must not inherit one");
}


static void testAnOlderSceneKeepsTheGroundPlaneItWasAuthoredAgainst() {
    // The world ground plane used to be unconditional, so a level needed a
    // collider only under the parts you could fall off. Every scene written
    // before the switch leans on that, and reading one with the new default
    // would drop everything in it out of the world with no message.
    //
    // This is what AssetVersion::Migrate exists for, and until now it was an
    // empty hook. The bar is not "the file still parses", it is "the scene still
    // behaves as its author built it".
    const std::string path = "test_old_scene_tmp.scene";
    {
        std::ofstream f(path);
        f << R"({"Version": 1, "Scene": "Old", "Entities": [
                  {"Tag": "Crate",
                   "Transform": {"Position": [0.0, 4.0, 0.0],
                                 "Rotation": [0.0, 0.0, 0.0],
                                 "Scale": [1.0, 1.0, 1.0]}}]})";
    }

    entt::registry registry;
    const auto result = SceneSerializer::Deserialize(registry, path);
    std::remove(path.c_str());

    CHECK_MSG(result.ok, "an older scene must still load: " + result.message);
    CHECK_MSG(registry.ctx().contains<PhysicsSettings>(),
              "an older scene must come back with physics settings");
    CHECK_MSG(registry.ctx().get<PhysicsSettings>().hasGroundPlane,
              "a scene older than the switch must keep the ground plane it was "
              "authored against");

    // And an unversioned scene - everything written before versions existed -
    // is older still, so it gets the same treatment.
    const std::string ancient = "test_ancient_scene_tmp.scene";
    {
        std::ofstream f(ancient);
        f << R"({"Scene": "Ancient", "Entities": []})";
    }
    entt::registry old;
    const auto ancientResult = SceneSerializer::Deserialize(old, ancient);
    std::remove(ancient.c_str());
    CHECK_MSG(ancientResult.ok, "an unversioned scene must still load");
    CHECK_MSG(old.ctx().get<PhysicsSettings>().hasGroundPlane,
              "and must keep its ground plane too");
}

static void testAnOlderSceneThatSaysWhatItWantsIsTakenAtItsWord() {
    // The migration fills a gap, it does not overrule. A file that predates the
    // version bump but already carries the key was hand-edited to say something,
    // and quietly replacing it would be the migration lying about the author's
    // intent.
    const std::string path = "test_old_explicit_tmp.scene";
    {
        std::ofstream f(path);
        f << R"({"Version": 1, "Scene": "Old",
                 "Physics": {"GroundPlane": false},
                 "Entities": []})";
    }

    entt::registry registry;
    const auto result = SceneSerializer::Deserialize(registry, path);
    std::remove(path.c_str());

    CHECK_MSG(result.ok, "the scene must load: " + result.message);
    CHECK_MSG(!registry.ctx().get<PhysicsSettings>().hasGroundPlane,
              "a scene that says it wants no ground plane must not be given one");
}


static void testInstantiatingAPrefabTwiceParsesItOnce() {
    // Every spawn used to open the file, read it into a string and run the
    // whole JSON parser over it, to produce a document identical to the last
    // one. A game that spawns units from a prefab does that at whatever rate it
    // spawns units.
    PrefabSerializer::ClearCache();
    CHECK_EQ(PrefabSerializer::CachedPrefabCount(), size_t{0});

    entt::registry registry;
    const auto source = registry.create();
    registry.emplace<TagComponent>(source, "Grunt");
    registry.emplace<TransformComponent>(source, glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(PrefabSerializer::SavePrefab(registry, source, kPrefab).ok);

    // Saving drops the cache, so this starts from nothing either way.
    CHECK_EQ(PrefabSerializer::CachedPrefabCount(), size_t{0});

    entt::registry target;
    for (int i = 0; i < 25; ++i) {
        const auto spawned = PrefabSerializer::InstantiatePrefab(target, kPrefab);
        CHECK_MSG(spawned != entt::null, "spawn " + std::to_string(i) + " must succeed");
    }

    CHECK_MSG(PrefabSerializer::CachedPrefabCount() == 1,
              "twenty-five spawns must parse one document: got " +
                  std::to_string(PrefabSerializer::CachedPrefabCount()));

    // And every one of them is a real, complete entity - a cache that handed
    // back something emptier after the first would be worse than the parse.
    int count = 0;
    for (auto entity : target.view<TagComponent>()) {
        CHECK(target.get<TagComponent>(entity).tag == "Grunt");
        CHECK_NEAR(target.get<TransformComponent>(entity).position.y, 2.0f);
        ++count;
    }
    CHECK_EQ(count, 25);

    std::remove(kPrefab.c_str());
    PrefabSerializer::ClearCache();
}

static void testSavingAPrefabInvalidatesWhatWasParsedFromIt() {
    // Otherwise saving a prefab and dragging it straight back into the scene
    // gives you the version it used to be, which is the kind of thing that
    // reads as "the editor did not save my change".
    PrefabSerializer::ClearCache();

    entt::registry registry;
    const auto source = registry.create();
    registry.emplace<TagComponent>(source, "Before");
    registry.emplace<TransformComponent>(source);
    CHECK(PrefabSerializer::SavePrefab(registry, source, kPrefab).ok);

    entt::registry target;
    const auto first = PrefabSerializer::InstantiatePrefab(target, kPrefab);
    CHECK(first != entt::null);
    CHECK(target.get<TagComponent>(first).tag == "Before");

    // Change it and save over the same path.
    registry.get<TagComponent>(source).tag = "After";
    CHECK(PrefabSerializer::SavePrefab(registry, source, kPrefab).ok);

    const auto second = PrefabSerializer::InstantiatePrefab(target, kPrefab);
    CHECK(second != entt::null);
    CHECK_MSG(target.get<TagComponent>(second).tag == "After",
              "the second spawn must be the saved version, not the cached one");

    std::remove(kPrefab.c_str());
    PrefabSerializer::ClearCache();
}

static void testABrokenPrefabIsNotCached() {
    // Caching a failure would make a prefab that was broken once stay broken
    // until the editor restarts, even after the file is fixed.
    PrefabSerializer::ClearCache();

    {
        std::ofstream f(kPrefab);
        f << "{ this is not json";
    }

    entt::registry target;
    SerializationResult result;
    CHECK_MSG(PrefabSerializer::InstantiatePrefab(target, kPrefab, &result) == entt::null,
              "a malformed prefab must not instantiate");
    CHECK(!result.ok);
    CHECK_MSG(PrefabSerializer::CachedPrefabCount() == 0,
              "and must not be remembered as if it had");

    // Fixed on disk, and it works without anyone clearing anything.
    {
        entt::registry registry;
        const auto source = registry.create();
        registry.emplace<TagComponent>(source, "Fixed");
        registry.emplace<TransformComponent>(source);
        CHECK(PrefabSerializer::SavePrefab(registry, source, kPrefab).ok);
    }
    const auto repaired = PrefabSerializer::InstantiatePrefab(target, kPrefab);
    CHECK_MSG(repaired != entt::null, "a repaired prefab must load");
    CHECK(target.get<TagComponent>(repaired).tag == "Fixed");

    std::remove(kPrefab.c_str());
    PrefabSerializer::ClearCache();
}

static void runTests() {
    testInstantiatingAPrefabTwiceParsesItOnce();
    testSavingAPrefabInvalidatesWhatWasParsedFromIt();
    testABrokenPrefabIsNotCached();
    testAnOlderSceneKeepsTheGroundPlaneItWasAuthoredAgainst();
    testAnOlderSceneThatSaysWhatItWantsIsTakenAtItsWord();
    testTheScenesLookSurvivesARoundTrip();
    testASceneWithNoLookGetsTheDefaultOne();
    testWorldPhysicsSurvivesARoundTrip();
    testLoadingASceneReplacesTheWorldPhysicsRatherThanKeepingIt();
    testUnversionedScenesStillLoad();
    testAFutureSceneIsRefusedAndChangesNothing();
    testSavedScenesCarryTheCurrentVersion();
    testReadLayersOverAnEntityThatAlreadyHasComponents();
    testJsonRoundTrip();
    testJsonRejectsGarbage();
    testTagEscaping();
    testSceneRoundTrip();
    testDeletedEntitiesDoNotBreakJson();
    testMalformedSceneLeavesRegistryIntact();
    testMissingSceneReportsFailure();
    testSerializeCreatesParentDirectory();
    testPrefabRoundTrip();
    testPrefabCarriesEverythingASceneDoes();
    testPrefabRoundTripsEveryField();
    testMissingPrefabReturnsNull();
}

TEST_MAIN("test_serialize", 250)
