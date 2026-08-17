// Regression tests for scene persistence.
//
// Deserialize used to read the file into a string it never looked at, wipe the
// registry, and fabricate three hardcoded entities while reporting success.
// Loading a scene destroyed the user's work and everything in the save file.

#include "TestHarness.hpp"
#include "core/Json.hpp"
#include "core/SceneSerializer.hpp"
#include "core/PrefabSerializer.hpp"
#include "core/Components.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Engine;

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

static void runTests() {
    testJsonRoundTrip();
    testJsonRejectsGarbage();
    testTagEscaping();
    testSceneRoundTrip();
    testDeletedEntitiesDoNotBreakJson();
    testMalformedSceneLeavesRegistryIntact();
    testMissingSceneReportsFailure();
    testSerializeCreatesParentDirectory();
    testPrefabRoundTrip();
    testMissingPrefabReturnsNull();
}

TEST_MAIN("test_serialize")
