// Tests for serialising a game's own components.
//
// ComponentCodec named eighteen engine components and could not be opened to
// anything else, which is a hard limit on what can be built on this engine: a
// mid-match save and a rollback snapshot are made of a game's own component
// types, and neither could be written by the thing that writes every other
// component. The options were to fork the codec or to keep a second serializer
// beside it, and two writers over the same data always drift - which is the
// argument this file was created to make in the first place.

#include "TestHarness.hpp"
#include "core/ComponentCodec.hpp"
#include "core/SceneSerializer.hpp"
#include "core/Components.hpp"
#include "core/Json.hpp"

#include <sstream>
#include <string>

using namespace Supersonic;

namespace {

// The shape of a real game component: not a transform, not anything the engine
// has a name for.
struct Health {
    int current{0};
    int maximum{0};
};

struct Team {
    std::string name;
};

// Registers both and cleans up after itself, so one test cannot leave the
// registry populated for the next. Registration is global by design - it maps a
// type to its format - which makes leaking it across tests a real hazard.
struct ScopedRegistration {
    ScopedRegistration() {
        ComponentCodec::ClearRegisteredComponents();

        ComponentCodec::RegisterComponent(
            "Health",
            [](const entt::registry& registry, entt::entity entity, std::ostream& out) {
                const auto* health = registry.try_get<Health>(entity);
                if (!health) return false;
                out << "{ \"Current\": " << health->current
                    << ", \"Maximum\": " << health->maximum << " }";
                return true;
            },
            [](entt::registry& registry, entt::entity entity, const Json::Value& value) {
                auto& health = registry.emplace_or_replace<Health>(entity);
                health.current = static_cast<int>(value["Current"].AsNumber(0.0));
                health.maximum = static_cast<int>(value["Maximum"].AsNumber(0.0));
            });

        ComponentCodec::RegisterComponent(
            "Team",
            [](const entt::registry& registry, entt::entity entity, std::ostream& out) {
                const auto* team = registry.try_get<Team>(entity);
                if (!team) return false;
                out << "\"" << Json::Escape(team->name) << "\"";
                return true;
            },
            [](entt::registry& registry, entt::entity entity, const Json::Value& value) {
                registry.emplace_or_replace<Team>(entity, value.AsString(""));
            });
    }
    ~ScopedRegistration() { ComponentCodec::ClearRegisteredComponents(); }
};

std::string writeEntity(entt::registry& registry, entt::entity entity) {
    std::ostringstream out;
    out << "{\n";
    ComponentCodec::Write(registry, entity, out, "  ");
    out << "}\n";
    return out.str();
}

} // namespace

static void testAGameComponentSurvivesARoundTrip() {
    ScopedRegistration registration;

    entt::registry source;
    const auto entity = source.create();
    source.emplace<TagComponent>(entity, "Grunt");
    source.emplace<TransformComponent>(entity, glm::vec3(3.0f, 0.0f, -2.0f));
    source.emplace<Health>(entity, Health{17, 40});
    source.emplace<Team>(entity, Team{"Wolves"});

    const std::string text = writeEntity(source, entity);

    Json::Value root;
    std::string error;
    CHECK_MSG(Json::Parse(text, root, error),
              "a game component must not produce a file that fails to parse: " + error);

    entt::registry loaded;
    const auto clone = loaded.create();
    ComponentCodec::Read(loaded, clone, root);

    const auto* health = loaded.try_get<Health>(clone);
    CHECK_MSG(health != nullptr, "the game component must come back");
    if (health) {
        CHECK_EQ(health->current, 17);
        CHECK_EQ(health->maximum, 40);
    }

    const auto* team = loaded.try_get<Team>(clone);
    CHECK_MSG(team != nullptr, "and so must the second one");
    if (team) CHECK(team->name == "Wolves");

    // The engine's own components must be untouched by any of this.
    CHECK_MSG(loaded.all_of<TagComponent>(clone), "the engine's components still work");
    CHECK_NEAR(loaded.get<TransformComponent>(clone).position.x, 3.0f);
}

static void testGameComponentsLiveInTheirOwnMember() {
    // Not tidiness. It makes a collision between a game's component name and an
    // engine one impossible - now, and for every component the engine ever
    // adds. A game naming something "Transform" is unremarkable and must not be
    // a scene-corrupting mistake.
    ScopedRegistration registration;
    ComponentCodec::ClearRegisteredComponents();
    ComponentCodec::RegisterComponent(
        "Transform",
        [](const entt::registry&, entt::entity, std::ostream& out) {
            out << "\"a game's own Transform\"";
            return true;
        },
        [](entt::registry&, entt::entity, const Json::Value&) {});

    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity, glm::vec3(9.0f, 0.0f, 0.0f));

    const std::string text = writeEntity(registry, entity);

    Json::Value root;
    std::string error;
    CHECK_MSG(Json::Parse(text, root, error), "still valid JSON: " + error);

    // The engine's Transform is an object with a Position; the game's is a
    // string, and it is inside "Game".
    CHECK_MSG(root["Transform"].IsObject(), "the engine's Transform is untouched");
    CHECK_NEAR(root["Transform"]["Position"].AsArray()[0].AsFloat(0.0f), 9.0f);
    CHECK_MSG(root["Game"]["Transform"].AsString("") == "a game's own Transform",
              "and the game's lives beside it without collision");
}

static void testAnEntityWithoutTheComponentWritesNothingForIt() {
    // The writer returns false and the codec must emit no key at all - not an
    // empty object, and certainly not a dangling "Health": with nothing after
    // it, which would be a file that cannot be read back.
    ScopedRegistration registration;

    entt::registry registry;
    const auto bare = registry.create();
    registry.emplace<TransformComponent>(bare);

    const std::string text = writeEntity(registry, bare);

    Json::Value root;
    std::string error;
    CHECK_MSG(Json::Parse(text, root, error), "must still parse: " + error);
    CHECK_MSG(!root.Has("Game"),
              "an entity with none of a game's components gets no Game member");
}

static void testTheEngineStillWritesValidJsonWithNothingRegistered() {
    // The path every existing scene takes. The extension writes before
    // HasRenderable precisely so the engine's last member keeps its
    // no-trailing-comma contract, and that contract is what lets the caller
    // close the object.
    ComponentCodec::ClearRegisteredComponents();
    CHECK_EQ(ComponentCodec::RegisteredComponentCount(), size_t{0});

    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TagComponent>(entity, "Plain");
    registry.emplace<TransformComponent>(entity);

    Json::Value root;
    std::string error;
    CHECK_MSG(Json::Parse(writeEntity(registry, entity), root, error),
              "an unextended entity must be unchanged: " + error);
    CHECK(root["Tag"].AsString("") == "Plain");
}

static void testRegistrationRejectsWhatWouldFailLater() {
    ComponentCodec::ClearRegisteredComponents();

    const auto writer = [](const entt::registry&, entt::entity, std::ostream&) { return false; };
    const auto reader = [](entt::registry&, entt::entity, const Json::Value&) {};

    CHECK_MSG(!ComponentCodec::RegisterComponent("", writer, reader),
              "a nameless component has no key to be written under");
    CHECK_MSG(!ComponentCodec::RegisterComponent("NoWriter", nullptr, reader),
              "a missing writer would crash at save time");
    CHECK_MSG(!ComponentCodec::RegisterComponent("NoReader", writer, nullptr),
              "a missing reader would crash at load time, against a file already written");

    CHECK(ComponentCodec::RegisterComponent("Once", writer, reader));
    CHECK_MSG(!ComponentCodec::RegisterComponent("Once", writer, reader),
              "registering the same key twice would write it twice");
    CHECK_EQ(ComponentCodec::RegisteredComponentCount(), size_t{1});

    ComponentCodec::ClearRegisteredComponents();
}

static void testAWholeSceneCarriesGameComponents() {
    // Through SceneSerializer rather than the codec directly, because that is
    // the path a save actually takes.
    ScopedRegistration registration;

    const std::string path = "test_gamecomponents_tmp.scene";
    {
        entt::registry registry;
        for (int i = 0; i < 3; ++i) {
            const auto e = registry.create();
            registry.emplace<TagComponent>(e, "Unit " + std::to_string(i));
            registry.emplace<TransformComponent>(e);
            registry.emplace<Health>(e, Health{i * 10, 100});
        }
        CHECK_MSG(SceneSerializer::Serialize(registry, path).ok, "the scene must save");
    }

    entt::registry loaded;
    const auto result = SceneSerializer::Deserialize(loaded, path);
    std::remove(path.c_str());

    CHECK_MSG(result.ok, "and load: " + result.message);

    int found = 0;
    int total = 0;
    for (auto entity : loaded.view<Health>()) {
        ++found;
        total += loaded.get<Health>(entity).current;
    }
    CHECK_MSG(found == 3, "every unit's health must survive the scene: got " +
                              std::to_string(found));
    CHECK_MSG(total == 30, "with its values: got " + std::to_string(total));
}

static void runTests() {
    testAGameComponentSurvivesARoundTrip();
    testGameComponentsLiveInTheirOwnMember();
    testAnEntityWithoutTheComponentWritesNothingForIt();
    testTheEngineStillWritesValidJsonWithNothingRegistered();
    testRegistrationRejectsWhatWouldFailLater();
    testAWholeSceneCarriesGameComponents();
}

TEST_MAIN("test_codecextension", 22)
