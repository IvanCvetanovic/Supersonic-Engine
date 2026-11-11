// A game's own component, described once, saved and edited from one list of fields.
//
// What is checked is everything that does not need a window: the component goes
// through the scene file and comes back, a field the file does not mention keeps its
// default (which is what makes adding a field later safe), a bad value degrades one
// field and not the whole component, and what the inspector is offered - the fields,
// in order, with their types, editing the live component - is what the visitor sees.
// The ImGui widgets themselves are a window's business.

#include "TestHarness.hpp"

#include "core/ComponentCodec.hpp"
#include "core/ComponentFields.hpp"
#include "core/Components.hpp"
#include "core/SceneSerializer.hpp"

#include <limits>
#include <string>
#include <vector>

using namespace Supersonic;

namespace {

struct Boss {
    int hitPoints{100};
    float rage{0.25f};
    bool enraged{false};
    std::string title{"the Unnamed"};
    glm::vec2 patrol{1.0f, 2.0f};
    glm::vec3 lair{0.0f, 1.0f, 0.0f};
    glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
};

void registerBoss() {
    ComponentFields::Clear();
    ComponentCodec::ClearRegisteredComponents();
    const bool ok = RegisterDescribedComponent<Boss>(
        "Boss",
        ComponentFields::Field("hitPoints", &Boss::hitPoints),
        ComponentFields::Field("rage", &Boss::rage),
        ComponentFields::Field("enraged", &Boss::enraged),
        ComponentFields::Field("title", &Boss::title),
        ComponentFields::Field("patrol", &Boss::patrol),
        ComponentFields::Field("lair", &Boss::lair),
        ComponentFields::Field("tint", &Boss::tint));
    CHECK_MSG(ok, "the component registers");
}

void unregister() {
    ComponentFields::Clear();
    ComponentCodec::ClearRegisteredComponents();
}

// Records what the inspector would be shown, and can edit through it.
struct Recorder final : FieldVisitor {
    std::vector<std::string> seen;   // "name:Type" in the order visited
    void Bool(const char* n, bool& v) override { seen.push_back(std::string(n) + ":Bool"); if (editBool) v = !v; }
    void Int(const char* n, int& v) override { seen.push_back(std::string(n) + ":Int"); if (editInt) v += 7; }
    void Float(const char* n, float&) override { seen.push_back(std::string(n) + ":Float"); }
    void String(const char* n, std::string& v) override { seen.push_back(std::string(n) + ":String"); if (editString) v += "!"; }
    void Vec2(const char* n, glm::vec2&) override { seen.push_back(std::string(n) + ":Vec2"); }
    void Vec3(const char* n, glm::vec3&) override { seen.push_back(std::string(n) + ":Vec3"); }
    void Vec4(const char* n, glm::vec4&) override { seen.push_back(std::string(n) + ":Vec4"); }
    bool editBool{false};
    bool editInt{false};
    bool editString{false};
};

} // namespace

static void testADescribedComponentSurvivesASceneRoundTrip() {
    registerBoss();

    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TagComponent>(entity, "Warden");
    registry.emplace<TransformComponent>(entity);
    auto& boss = registry.emplace<Boss>(entity);
    boss.hitPoints = 640;
    boss.rage = 0.875f;
    boss.enraged = true;
    boss.title = "Warden of the \"Deep\"";   // a quote and a backslash would break a hand-built string
    boss.patrol = glm::vec2(-3.5f, 8.25f);
    boss.lair = glm::vec3(10.0f, -2.5f, 0.125f);
    boss.tint = glm::vec4(0.5f, 0.25f, 0.75f, 1.0f);

    const std::string text = SceneSerializer::SerializeToString(registry);
    CHECK_MSG(text.find("\"Game\"") != std::string::npos && text.find("\"Boss\"") != std::string::npos,
              "the component is written under the scene's Game member");

    entt::registry loaded;
    const auto result = SceneSerializer::DeserializeFromString(loaded, text);
    CHECK_MSG(result.ok, result.message);

    const Boss* back = nullptr;
    for (auto e : loaded.view<Boss>()) back = &loaded.get<Boss>(e);
    CHECK_MSG(back != nullptr, "and read back onto the entity");
    if (back == nullptr) { unregister(); return; }

    CHECK_EQ(back->hitPoints, 640);
    CHECK_NEAR(back->rage, 0.875f);
    CHECK(back->enraged);
    CHECK_MSG(back->title == boss.title, "a string with quotes survives: " + back->title);
    CHECK_NEAR(back->patrol.x, -3.5f);
    CHECK_NEAR(back->patrol.y, 8.25f);
    CHECK(back->lair == boss.lair);
    CHECK(back->tint == boss.tint);

    unregister();
}

static void testAFieldTheFileDoesNotMentionKeepsItsDefault() {
    // Adding a field to a component must not break a scene saved before it existed:
    // the reader starts from a default-constructed component, so an absent key reads
    // as what the type says - not as zero, and not as whatever the last load left.
    registerBoss();

    const std::string older = R"({
      "Version": 2, "Entities": [ { "Tag": "Old", "Game": { "Boss": { "hitPoints": 5 } } } ] })";
    entt::registry loaded;
    CHECK(SceneSerializer::DeserializeFromString(loaded, older).ok);

    const Boss* boss = nullptr;
    for (auto e : loaded.view<Boss>()) boss = &loaded.get<Boss>(e);
    CHECK_MSG(boss != nullptr, "the component loads from a file that names one of its fields");
    if (boss != nullptr) {
        CHECK_EQ(boss->hitPoints, 5);
        CHECK_NEAR(boss->rage, 0.25f);
        CHECK_MSG(boss->title == "the Unnamed", "an absent string is the default, not empty");
        CHECK(boss->lair == glm::vec3(0.0f, 1.0f, 0.0f));
        CHECK(!boss->enraged);
    }
    unregister();
}

static void testABadValueDegradesOneFieldAndNotTheComponent() {
    registerBoss();
    const std::string hand_edited = R"({
      "Version": 2, "Entities": [ { "Tag": "Edited", "Game": { "Boss": {
        "hitPoints": "lots", "rage": 0.5, "lair": [4], "patrol": "north", "enraged": 1 } } } ] })";
    entt::registry loaded;
    CHECK(SceneSerializer::DeserializeFromString(loaded, hand_edited).ok);

    const Boss* boss = nullptr;
    for (auto e : loaded.view<Boss>()) boss = &loaded.get<Boss>(e);
    CHECK(boss != nullptr);
    if (boss != nullptr) {
        CHECK_NEAR(boss->rage, 0.5f);
        CHECK_MSG(boss->hitPoints == 100, "a string where a number belongs leaves the default");
        CHECK_NEAR(boss->lair.x, 4.0f);
        CHECK_MSG(boss->lair.y == 1.0f && boss->lair.z == 0.0f, "a short array changes only the entries it has");
        CHECK_MSG(boss->patrol == glm::vec2(1.0f, 2.0f), "an array field given a string keeps its default");
    }
    unregister();
}

static void testANonFiniteFloatIsWrittenAsANumber() {
    // The scene writer holds the finite-number guard for the whole write, so a
    // described component's floats go through it like every engine component's: a
    // NaN does not make the file unreadable.
    registerBoss();
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity);
    registry.emplace<Boss>(entity).rage = std::numeric_limits<float>::quiet_NaN();

    const std::string text = SceneSerializer::SerializeToString(registry);
    entt::registry loaded;
    const auto nanResult = SceneSerializer::DeserializeFromString(loaded, text);
    CHECK_MSG(nanResult.ok, "the scene still loads: " + nanResult.message);
    unregister();
}

static void testTheInspectorIsOfferedEveryFieldInOrderAndEditsTheLiveComponent() {
    registerBoss();
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<Boss>(entity);

    const auto& all = ComponentFields::All();
    CHECK_EQ(all.size(), size_t{1});
    if (all.size() != 1) { unregister(); return; }
    const auto& boss = all[0];
    CHECK_MSG(boss.key == "Boss", "it is listed under its key");
    CHECK(boss.has(registry, entity));

    Recorder recorder;
    recorder.editBool = recorder.editInt = recorder.editString = true;
    boss.visit(registry, entity, recorder);

    const std::vector<std::string> expected = {"hitPoints:Int", "rage:Float", "enraged:Bool", "title:String",
                                               "patrol:Vec2", "lair:Vec3", "tint:Vec4"};
    CHECK_MSG(recorder.seen == expected, "every field, in the order declared, with its type");

    const Boss& live = registry.get<Boss>(entity);
    CHECK_MSG(live.hitPoints == 107 && live.enraged && live.title == "the Unnamed!",
              "a change made through the visitor is a change to the component itself");
    unregister();
}

static void testAddAndRemoveWorkThroughTheEntries() {
    registerBoss();
    entt::registry registry;
    const auto entity = registry.create();

    const auto& boss = ComponentFields::All()[0];
    CHECK(!boss.has(registry, entity));
    boss.add(registry, entity);
    CHECK_MSG(boss.has(registry, entity), "Add Component attaches a default one");
    CHECK_EQ(registry.get<Boss>(entity).hitPoints, 100);

    registry.get<Boss>(entity).hitPoints = 3;
    boss.add(registry, entity);
    CHECK_MSG(registry.get<Boss>(entity).hitPoints == 3, "adding to an entity that has one changes nothing");

    boss.remove(registry, entity);
    CHECK_MSG(!boss.has(registry, entity), "Remove Component takes it off");

    // And with none on the entity, visiting is a no-op rather than a null access.
    Recorder recorder;
    boss.visit(registry, entity, recorder);
    CHECK(recorder.seen.empty());
    unregister();
}

static void testAKeyTakenIsRefusedAndNothingIsHalfRegistered() {
    registerBoss();
    const bool again = RegisterDescribedComponent<Boss>("Boss", ComponentFields::Field("hitPoints", &Boss::hitPoints));
    CHECK_MSG(!again, "the same key twice is refused");
    CHECK_EQ(ComponentFields::All().size(), size_t{1});
    CHECK_EQ(ComponentCodec::RegisteredComponentCount(), size_t{1});

    // A key the codec already holds (registered by hand) refuses the described one,
    // and leaves the inspector's list as it was.
    ComponentFields::Clear();
    const bool taken = RegisterDescribedComponent<Boss>("Boss", ComponentFields::Field("hitPoints", &Boss::hitPoints));
    CHECK_MSG(!taken, "a key the codec holds is refused");
    CHECK_MSG(ComponentFields::All().empty(), "and nothing is added to the inspector's list");
    unregister();
}

static void runTests() {
    testADescribedComponentSurvivesASceneRoundTrip();
    testAFieldTheFileDoesNotMentionKeepsItsDefault();
    testABadValueDegradesOneFieldAndNotTheComponent();
    testANonFiniteFloatIsWrittenAsANumber();
    testTheInspectorIsOfferedEveryFieldInOrderAndEditsTheLiveComponent();
    testAddAndRemoveWorkThroughTheEntries();
    testAKeyTakenIsRefusedAndNothingIsHalfRegistered();
}

TEST_MAIN("test_componentfields", 40)
