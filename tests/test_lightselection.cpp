// Tests for which lights survive the eight-light cap.
//
// The scene UBO holds a fixed array of eight and the loop that filled it took
// lights in registry order and stopped at the ninth. EnTT views walk a pool in
// REVERSE insertion order, so the eight a scene got were the eight authored
// LAST - which meant a level with twelve lamps in it dropped its sun, because
// the sun is the first thing anyone places. The sun is also the only light the
// shader can shadow, so the whole scene lost its shadows as a side effect of
// someone adding a twelfth lamp at the other end of the level.
//
// Nothing said so, in the log or anywhere else.

#include "TestHarness.hpp"

#include "core/LightSelection.hpp"
#include "core/Components.hpp"

#include <algorithm>
#include <string>

using namespace Supersonic;

namespace {

entt::entity makeLight(entt::registry& registry, LightType type, const glm::vec3& position,
                       float range, const char* tag) {
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = position;

    auto& light = registry.emplace<LightComponent>(entity);
    light.type = static_cast<int>(type);
    light.range = range;

    auto& name = registry.emplace<TagComponent>(entity);
    name.tag = tag;
    return entity;
}

std::string tagOf(const entt::registry& registry, entt::entity entity) {
    const auto* tag = registry.try_get<TagComponent>(entity);
    return tag ? tag->tag : std::string{};
}

bool chose(const entt::registry& registry, const std::vector<entt::entity>& chosen,
           const std::string& tag) {
    return std::any_of(chosen.begin(), chosen.end(), [&](entt::entity e) {
        return tagOf(registry, e) == tag;
    });
}

} // namespace

// The bug, exactly. A sun placed first, then more lamps than the cap holds.
static void testTheSunSurvivesTwelveLampsAuthoredAfterIt() {
    entt::registry registry;
    makeLight(registry, LightType::Directional, glm::vec3(0.0f), 25.0f, "Sun");
    for (int i = 0; i < 12; ++i) {
        makeLight(registry, LightType::Point, glm::vec3(0.0f, 1.5f, -4.0f * i), 9.0f,
                  ("Lamp" + std::to_string(i)).c_str());
    }

    const auto chosen = SelectLights(registry, glm::vec3(0.0f, 3.0f, 10.0f), 8);

    CHECK_EQ(chosen.size(), size_t{8});
    CHECK_MSG(chose(registry, chosen, "Sun"),
              "the directional light must survive any number of lamps authored after it");
    CHECK_MSG(tagOf(registry, chosen.front()) == "Sun",
              "and must come first, because only lights[0] is shadowed");
}

// Directional lights are not ranked by distance - they have no position that
// means anything - so several of them all sort ahead of the local ones.
static void testEveryDirectionalOutranksEveryLamp() {
    entt::registry registry;
    makeLight(registry, LightType::Point, glm::vec3(0.0f, 1.0f, 9.0f), 9.0f, "VeryNearLamp");
    makeLight(registry, LightType::Directional, glm::vec3(0.0f), 25.0f, "KeyLight");
    makeLight(registry, LightType::Directional, glm::vec3(0.0f), 25.0f, "FillLight");

    const auto chosen = SelectLights(registry, glm::vec3(0.0f, 3.0f, 10.0f), 2);

    CHECK_EQ(chosen.size(), size_t{2});
    CHECK(chose(registry, chosen, "KeyLight"));
    CHECK(chose(registry, chosen, "FillLight"));
    CHECK_MSG(!chose(registry, chosen, "VeryNearLamp"),
              "a lamp one metre away still loses to a directional light");
}

// Ranked by distance to the EDGE of a light's reach, not to the light. A wide
// lamp lighting the whole shot must beat a pinpoint one nearer the camera that
// illuminates nothing.
static void testRangeCountsNotJustDistance() {
    entt::registry registry;
    makeLight(registry, LightType::Point, glm::vec3(0.0f, 1.0f, -30.0f), 60.0f, "Floodlight");
    makeLight(registry, LightType::Point, glm::vec3(0.0f, 1.0f, -20.0f), 0.5f, "Pinprick");

    const auto chosen = SelectLights(registry, glm::vec3(0.0f, 3.0f, 10.0f), 1);

    CHECK_EQ(chosen.size(), size_t{1});
    CHECK_MSG(tagOf(registry, chosen.front()) == "Floodlight",
              "the floodlight reaches the camera and the pinprick does not, "
              "even though the pinprick is ten metres closer");
}

// The near lamp wins, which is the plain case the whole thing exists for.
static void testTheNearerLampWins() {
    entt::registry registry;
    makeLight(registry, LightType::Point, glm::vec3(0.0f, 1.0f, -120.0f), 9.0f, "Far");
    makeLight(registry, LightType::Point, glm::vec3(0.0f, 1.0f, 6.0f), 9.0f, "Near");

    const auto chosen = SelectLights(registry, glm::vec3(0.0f, 3.0f, 10.0f), 1);
    CHECK_EQ(chosen.size(), size_t{1});
    CHECK(tagOf(registry, chosen.front()) == "Near");
}

// Two lights that rank the same keep their registry order rather than swapping
// between frames as floats compare differently, which would flicker.
static void testEqualLightsKeepAStableOrder() {
    entt::registry registry;
    makeLight(registry, LightType::Point, glm::vec3(3.0f, 1.0f, 8.0f), 9.0f, "First");
    makeLight(registry, LightType::Point, glm::vec3(-3.0f, 1.0f, 8.0f), 9.0f, "Second");

    const auto viewPosition = glm::vec3(0.0f, 3.0f, 10.0f);
    const auto once = SelectLights(registry, viewPosition, 8);
    const auto again = SelectLights(registry, viewPosition, 8);

    CHECK_EQ(once.size(), size_t{2});
    CHECK_MSG(once == again, "the same scene and camera must choose the same lights");
}

// A parented lamp is where its parent put it. gatherLights read the LOCAL
// transform for years, which is the departure ARCHITECTURE recorded before it
// was fixed - selection must not reintroduce it.
static void testAParentedLampIsRankedWhereItActuallyIs() {
    entt::registry registry;

    // Local position says it is at the origin, next to the camera. The world
    // matrix says its parent carried it far away.
    const auto entity = makeLight(registry, LightType::Point, glm::vec3(0.0f, 1.0f, 8.0f),
                                  9.0f, "Carried");
    auto& world = registry.emplace<WorldTransformComponent>(entity);
    world.matrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, -200.0f));

    makeLight(registry, LightType::Point, glm::vec3(0.0f, 1.0f, 7.0f), 9.0f, "Stationary");

    const auto chosen = SelectLights(registry, glm::vec3(0.0f, 3.0f, 10.0f), 1);
    CHECK_EQ(chosen.size(), size_t{1});
    CHECK_MSG(tagOf(registry, chosen.front()) == "Stationary",
              "the carried lamp is 200m away, whatever its local transform says");
}

// Under the cap, nothing is dropped and nothing is reordered away.
static void testEverythingSurvivesUnderTheCap() {
    entt::registry registry;
    makeLight(registry, LightType::Point, glm::vec3(0.0f, 1.0f, 5.0f), 9.0f, "A");
    makeLight(registry, LightType::Point, glm::vec3(1.0f, 1.0f, 5.0f), 9.0f, "B");

    const auto chosen = SelectLights(registry, glm::vec3(0.0f, 3.0f, 10.0f), 8);
    CHECK_EQ(chosen.size(), size_t{2});
    CHECK(chose(registry, chosen, "A"));
    CHECK(chose(registry, chosen, "B"));

    entt::registry empty;
    CHECK_EQ(SelectLights(empty, glm::vec3(0.0f), 8).size(), size_t{0});
}

static void runTests() {
    testTheSunSurvivesTwelveLampsAuthoredAfterIt();
    testEveryDirectionalOutranksEveryLamp();
    testRangeCountsNotJustDistance();
    testTheNearerLampWins();
    testEqualLightsKeepAStableOrder();
    testAParentedLampIsRankedWhereItActuallyIs();
    testEverythingSurvivesUnderTheCap();
}

TEST_MAIN("test_lightselection", 19)
