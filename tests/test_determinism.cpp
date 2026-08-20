// Tests that the simulation does the same thing twice.
//
// It did not. Three runs of one binary over one scene produced three different
// images, because everything that needed a clock read the frame delta - the
// real, variable time the last frame took to draw - and accumulated it.
// ScriptComponent::elapsed did exactly that and drove std::sin off the result.
//
// That is not a small inaccuracy. Nothing can be replayed, no test can assert
// on what a simulation produced, two machines in a network game disagree
// immediately, and a bug a player reports cannot be reproduced by the person
// fixing it. It is also what stopped the shadow-pass cache from being tested
// end to end: a rendered frame could not be compared against anything, because
// the same scene did not render the same way twice.

#include "TestHarness.hpp"
#include "core/StateHash.hpp"
#include "core/SimulationClock.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/PhysicsSettings.hpp"
#include "core/Components.hpp"

#include <string>
#include <vector>

using namespace Supersonic;

namespace {

// A scene with something to get wrong: bodies at different heights that land,
// bounce, interact and settle, plus a static floor to land on.
void buildScene(entt::registry& registry) {
    const auto floorEntity = registry.create();
    registry.emplace<TransformComponent>(floorEntity, glm::vec3(0.0f, 0.0f, 0.0f),
                                         glm::vec3(0.0f), glm::vec3(20.0f, 1.0f, 20.0f));
    registry.emplace<BoxColliderComponent>(floorEntity);

    for (int i = 0; i < 6; ++i) {
        const auto box = registry.create();
        registry.emplace<TransformComponent>(
            box, glm::vec3(static_cast<float>(i) * 0.9f - 2.0f, 2.0f + static_cast<float>(i) * 1.3f,
                           static_cast<float>(i % 3) * 0.4f));
        auto& body = registry.emplace<RigidBodyComponent>(box);
        body.restitution = 0.1f * static_cast<float>(i);
        registry.emplace<BoxColliderComponent>(box);
    }

    const auto ball = registry.create();
    registry.emplace<TransformComponent>(ball, glm::vec3(0.3f, 8.0f, 0.1f));
    registry.emplace<RigidBodyComponent>(ball).restitution = 0.6f;
    registry.emplace<SphereColliderComponent>(ball).radius = 0.5f;
}

uint64_t runFor(int ticks) {
    entt::registry registry;
    buildScene(registry);

    auto& clock = registry.ctx().emplace<SimulationClock>();
    clock.fixedDelta = 1.0f / 60.0f;

    for (int i = 0; i < ticks; ++i) {
        ++clock.tick;
        PhysicsSystem::Update(registry, clock.fixedDelta);
    }
    return StateHash::Compute(registry);
}

} // namespace

static void testTheSameSceneSteppedTwiceAgreesExactly() {
    // The whole claim, and it is bit equality rather than a tolerance. A
    // tolerance would be a decision about how far two runs may drift before it
    // counts, which is the question this test exists to answer - so it cannot
    // also be the test's parameter.
    const uint64_t first = runFor(240);
    const uint64_t second = runFor(240);

    CHECK_MSG(first == second,
              "four seconds of simulation must reproduce exactly: " +
                  std::to_string(first) + " vs " + std::to_string(second));

    // And again from a third run, because two agreeing could still be two
    // copies of the same accident.
    CHECK_MSG(runFor(240) == first, "and a third time");
}

static void testTheHashMovesWhenTheSimulationDoes() {
    // The other half. A hash that never changes agrees with everything and
    // proves nothing - which is exactly what a state oracle would look like if
    // it were reading a field the simulation does not touch.
    const uint64_t early = runFor(1);
    const uint64_t settling = runFor(30);
    const uint64_t late = runFor(240);

    CHECK_MSG(early != settling, "one tick and thirty are different states");
    CHECK_MSG(settling != late, "thirty and two hundred and forty likewise");
}

static void testTheHashDoesNotDependOnEntityCreationOrder() {
    // EnTT iterates in an order that comes from how components were added and
    // removed, not from the state. Two registries holding identical values can
    // walk them differently, and a hash that folded them in sequence would call
    // that a divergence - a determinism test that fails on scenes that agree.
    entt::registry forward;
    entt::registry shuffled;

    std::vector<entt::entity> made;
    for (int i = 0; i < 5; ++i) {
        const auto e = forward.create();
        forward.emplace<TransformComponent>(e, glm::vec3(static_cast<float>(i), 1.0f, 0.0f));
        made.push_back(e);
    }

    // The same entities and values, but the components attached back to front.
    std::vector<entt::entity> mirror;
    for (int i = 0; i < 5; ++i) mirror.push_back(shuffled.create());
    for (int i = 4; i >= 0; --i) {
        shuffled.emplace<TransformComponent>(mirror[static_cast<size_t>(i)],
                                             glm::vec3(static_cast<float>(i), 1.0f, 0.0f));
    }

    CHECK_MSG(StateHash::Compute(forward) == StateHash::Compute(shuffled),
              "the same state must hash the same however it was built");
}

static void testAValueOnTheWrongEntityIsADifferentState() {
    // Two crates swapping positions is not the same world, even though the set
    // of positions is unchanged. A hash that summed per-entity contributions
    // without seeding them by entity would be blind to it.
    entt::registry a;
    entt::registry b;

    const auto a0 = a.create();
    const auto a1 = a.create();
    a.emplace<TransformComponent>(a0, glm::vec3(1.0f, 0.0f, 0.0f));
    a.emplace<TransformComponent>(a1, glm::vec3(2.0f, 0.0f, 0.0f));

    const auto b0 = b.create();
    const auto b1 = b.create();
    b.emplace<TransformComponent>(b0, glm::vec3(2.0f, 0.0f, 0.0f));
    b.emplace<TransformComponent>(b1, glm::vec3(1.0f, 0.0f, 0.0f));

    CHECK_MSG(StateHash::Compute(a) != StateHash::Compute(b),
              "swapping two entities' positions is a different state");
}

static void testSleepStateIsPartOfTheState() {
    // A body asleep on one machine and awake on another has not diverged yet
    // and will on the next thing that touches it. Leaving it out of the hash
    // would let a desync go unreported until the frame it became visible.
    entt::registry awake;
    entt::registry asleep;

    for (entt::registry* r : { &awake, &asleep }) {
        const auto e = r->create();
        r->emplace<TransformComponent>(e, glm::vec3(0.0f, 1.0f, 0.0f));
        r->emplace<RigidBodyComponent>(e);
    }
    for (auto e : asleep.view<RigidBodyComponent>()) {
        asleep.get<RigidBodyComponent>(e).isSleeping = true;
    }

    CHECK_MSG(StateHash::Compute(awake) != StateHash::Compute(asleep),
              "sleeping is state, not an optimisation detail");
}

static void testTheClockIsDerivedRatherThanAccumulated() {
    // Summing a float sixty times a second drifts, and drifts differently
    // depending on where the sum started - which would put the reproducibility
    // straight back where it was found. Multiplying gives the same answer for
    // the same tick however that tick was reached.
    SimulationClock clock;
    clock.fixedDelta = 1.0f / 60.0f;

    float accumulated = 0.0f;
    for (int i = 0; i < 100000; ++i) {
        ++clock.tick;
        accumulated += clock.fixedDelta;
    }

    // Against the SAME step, not against the mathematical 1/60. A float cannot
    // hold 1/60 exactly, so the honest question is not "does the clock equal
    // 100000/60" - it does not and should not - but "is it exactly the step
    // taken that many times", which is what a second machine would also get.
    const double derived = clock.Seconds();
    const double exact = 100000.0 * static_cast<double>(clock.fixedDelta);

    CHECK_MSG(std::fabs(derived - exact) < 1e-9,
              "the derived clock must be the step, multiplied: " + std::to_string(derived));

    // The accumulated one has visibly drifted by here, which is the point.
    const double drift = std::fabs(static_cast<double>(accumulated) - exact);
    CHECK_MSG(drift > 1e-3,
              "and the accumulated one has drifted, which is why it is not used: drift = " +
                  std::to_string(drift));
}

static void runTests() {
    testTheSameSceneSteppedTwiceAgreesExactly();
    testTheHashMovesWhenTheSimulationDoes();
    testTheHashDoesNotDependOnEntityCreationOrder();
    testAValueOnTheWrongEntityIsADifferentState();
    testSleepStateIsPartOfTheState();
    testTheClockIsDerivedRatherThanAccumulated();
}

TEST_MAIN("test_determinism", 9)
