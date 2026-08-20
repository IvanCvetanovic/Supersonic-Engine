// Tests for the seam a game lives in.
//
// The engine was an application with an editor fused into it: the registry was
// private and the frame was a closed sequence of calls to engine systems, so
// there was nowhere for anyone else's code to run. A game had two options -
// edit SupersonicApp.cpp, or express its logic through the script plugin's C
// ABI, which is deliberately POD-only and is not where a simulation's own data
// structures belong.
//
// The stack itself is small. What is worth testing is the four rules it makes:
// layers run in push order, they come apart in the reverse of it, a layer may
// push another from inside its own tick, and the registry it is handed is the
// scene rather than a copy of it.

#include "TestHarness.hpp"
#include "core/LayerStack.hpp"
#include "core/Components.hpp"

#include <string>
#include <vector>

using namespace Supersonic;

namespace {

// A layer that writes down everything that happens to it, into a log the test
// owns. Shared rather than static so two stacks in one suite cannot see each
// other's history.
struct Recorder : EngineLayer {
    Recorder(std::string name, std::vector<std::string>* log)
        : m_name(std::move(name)), m_log(log) {}

    const char* Name() const override { return m_name.c_str(); }

    void OnAttach(entt::registry&) override { m_log->push_back(m_name + ":attach"); }
    void OnDetach(entt::registry&) override { m_log->push_back(m_name + ":detach"); }
    void OnFixedUpdate(entt::registry&, float dt) override {
        m_log->push_back(m_name + ":fixed");
        lastFixedDelta = dt;
        ++fixedTicks;
    }
    void OnUpdate(entt::registry&, float dt) override {
        m_log->push_back(m_name + ":update");
        lastDelta = dt;
    }

    float lastFixedDelta{-1.0f};
    float lastDelta{-1.0f};
    int fixedTicks{0};

private:
    std::string m_name;
    std::vector<std::string>* m_log;
};

} // namespace

static void testLayersRunInPushOrder() {
    // Push order and nothing else - no priorities, no sorting. A game that
    // needs its systems sequenced pushes them in sequence, which is a thing it
    // knows and the engine never could.
    entt::registry registry;
    std::vector<std::string> log;
    LayerStack stack;

    stack.Push(std::make_unique<Recorder>("first", &log), registry);
    stack.Push(std::make_unique<Recorder>("second", &log), registry);
    stack.Push(std::make_unique<Recorder>("third", &log), registry);

    CHECK_EQ(stack.Size(), size_t{3});
    CHECK(log.size() == 3 && log[0] == "first:attach" && log[2] == "third:attach");

    log.clear();
    stack.FixedUpdate(registry, 1.0f / 60.0f);
    CHECK_MSG(log.size() == 3, "every layer must tick: got " + std::to_string(log.size()));
    CHECK(log[0] == "first:fixed");
    CHECK(log[1] == "second:fixed");
    CHECK_MSG(log[2] == "third:fixed", "and in the order they were pushed");

    log.clear();
    stack.Update(registry, 0.016f);
    CHECK(log[0] == "first:update" && log[2] == "third:update");
}

static void testLayersDetachInReverse() {
    // The only safe order when layers were pushed in dependency order: the last
    // one pushed may be built on the ones before it, so it has to come apart
    // first. Detaching forwards tears out the foundation while something is
    // still standing on it.
    entt::registry registry;
    std::vector<std::string> log;

    {
        LayerStack stack;
        stack.Push(std::make_unique<Recorder>("engine", &log), registry);
        stack.Push(std::make_unique<Recorder>("game", &log), registry);
        stack.Push(std::make_unique<Recorder>("ui", &log), registry);
        log.clear();
        stack.Clear(&registry);

        CHECK_MSG(log.size() == 3, "every layer must be detached: got " +
                                       std::to_string(log.size()));
        CHECK(log[0] == "ui:detach");
        CHECK(log[1] == "game:detach");
        CHECK_MSG(log[2] == "engine:detach", "the first pushed is the last detached");
        CHECK_MSG(stack.Empty(), "and the stack is empty afterwards");
    }
}

static void testTheFixedStepDeltaIsTheFixedStep() {
    // The whole reason OnFixedUpdate is separate from OnUpdate. A simulation
    // driven from a variable delta cannot be replayed and cannot agree with
    // itself across two machines; this callback must always be handed the same
    // number.
    entt::registry registry;
    std::vector<std::string> log;
    LayerStack stack;

    auto owned = std::make_unique<Recorder>("sim", &log);
    Recorder* sim = owned.get();
    stack.Push(std::move(owned), registry);

    stack.FixedUpdate(registry, 1.0f / 60.0f);
    stack.FixedUpdate(registry, 1.0f / 60.0f);
    stack.Update(registry, 0.0331f);

    CHECK_EQ(sim->fixedTicks, 2);
    CHECK_NEAR(sim->lastFixedDelta, 1.0f / 60.0f);
    CHECK_MSG(std::fabs(sim->lastDelta - 0.0331f) < 1e-6f,
              "the per-frame callback gets the real elapsed time, not the step");
}

namespace {

// Pushes a second layer from inside its own tick - a game starting a match, a
// level loading the systems it needs. That reallocates the vector being walked,
// which is why the walk is indexed rather than iterated.
struct Spawner : EngineLayer {
    Spawner(LayerStack* stack, std::vector<std::string>* log) : m_stack(stack), m_log(log) {}
    const char* Name() const override { return "spawner"; }

    void OnFixedUpdate(entt::registry& registry, float) override {
        m_log->push_back("spawner:fixed");
        if (!m_spawned) {
            m_spawned = true;
            m_stack->Push(std::make_unique<Recorder>("spawned", m_log), registry);
        }
    }

private:
    LayerStack* m_stack;
    std::vector<std::string>* m_log;
    bool m_spawned{false};
};

} // namespace

static void testALayerMayPushAnotherFromInsideItsTick() {
    // Iterating the vector here is a dangling iterator the moment the push
    // reallocates - a crash, or worse, a tick against freed memory.
    entt::registry registry;
    std::vector<std::string> log;
    LayerStack stack;

    stack.Push(std::make_unique<Spawner>(&stack, &log), registry);
    log.clear();

    stack.FixedUpdate(registry, 1.0f / 60.0f);

    CHECK_MSG(stack.Size() == 2, "the pushed layer must be in the stack: got " +
                                     std::to_string(stack.Size()));
    // Attached during the pass, and ticked at the end of the same one rather
    // than being skipped until the next frame.
    CHECK_MSG(log.size() == 3, "expected spawner tick, attach, spawned tick: got " +
                                   std::to_string(log.size()));
    CHECK(log[0] == "spawner:fixed");
    CHECK(log[1] == "spawned:attach");
    CHECK(log[2] == "spawned:fixed");

    // And the next pass is ordinary.
    log.clear();
    stack.FixedUpdate(registry, 1.0f / 60.0f);
    CHECK(log.size() == 2 && log[0] == "spawner:fixed" && log[1] == "spawned:fixed");

    stack.Clear(&registry);
}

namespace {

// Builds an entity in OnAttach and moves it every tick, which is the shape of
// every game that will ever use this.
struct WorldBuilder : EngineLayer {
    const char* Name() const override { return "world"; }

    void OnAttach(entt::registry& registry) override {
        entity = registry.create();
        registry.emplace<TagComponent>(entity, "Built by a layer");
        registry.emplace<TransformComponent>(entity);
    }

    void OnFixedUpdate(entt::registry& registry, float dt) override {
        registry.get<TransformComponent>(entity).position.x += dt;
    }

    entt::entity entity{entt::null};
};

} // namespace

static void testALayerOperatesOnTheRealScene() {
    // Not a copy, not a filtered view. The registry a layer is handed is the
    // one the engine's own systems read, or a game could build a world nothing
    // would ever draw.
    entt::registry registry;
    LayerStack stack;

    auto owned = std::make_unique<WorldBuilder>();
    WorldBuilder* builder = owned.get();
    stack.Push(std::move(owned), registry);

    CHECK_MSG(registry.valid(builder->entity), "the layer's entity must exist in the scene");
    CHECK_MSG(registry.all_of<TagComponent>(builder->entity),
              "with the components it gave it");

    for (int i = 0; i < 10; ++i) stack.FixedUpdate(registry, 0.1f);

    const float x = registry.get<TransformComponent>(builder->entity).position.x;
    CHECK_MSG(std::fabs(x - 1.0f) < 1e-4f,
              "ten ticks of 0.1 must have moved it one unit: got " + std::to_string(x));

    stack.Clear(&registry);
}

static void testANullLayerIsIgnoredRatherThanStored() {
    // A stack holding a null entry crashes on the next tick, a long way from
    // the push that did it.
    entt::registry registry;
    LayerStack stack;

    stack.Push(nullptr, registry);
    CHECK_MSG(stack.Empty(), "a null push must not grow the stack");

    stack.FixedUpdate(registry, 1.0f / 60.0f);
    stack.Update(registry, 0.016f);
    CHECK(true); // reaching here without a crash is the assertion
}

static void runTests() {
    testLayersRunInPushOrder();
    testLayersDetachInReverse();
    testTheFixedStepDeltaIsTheFixedStep();
    testALayerMayPushAnotherFromInsideItsTick();
    testALayerOperatesOnTheRealScene();
    testANullLayerIsIgnoredRatherThanStored();
}

TEST_MAIN("test_layerstack", 24)
