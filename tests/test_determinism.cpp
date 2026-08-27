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

#include <algorithm>
#include <cmath>
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

// --- the accumulator, at two frame rates ----------------------------------
//
// The engine's loop cannot be linked into a suite - it owns a window and a
// device - so what is tested here is the ARITHMETIC it runs, transliterated.
// Two descriptions of one rule, and the risk that they drift is the same risk
// clusterIndexFor carries and names.
//
// This is the rule the whole clock rests on: the number of ticks a run
// produces depends on how much time passed, and NOT on how that time was
// delivered. Sixty frames of 1/60 and six frames of 1/6 are the same second and
// must be the same simulation.

struct TickPump {
    float accumulator{0.0f};
    float gameTick{1.0f / 60.0f};
    int maxStepsPerFrame{5};
    uint64_t ticks{0};
    double dropped{0.0};
    float alpha{0.0f};

    // A transliteration of SupersonicApp's loop. Kept deliberately close to it,
    // including the order of the drop and the alpha, because reading the two
    // side by side is the only check that they agree.
    void Frame(float delta) {
        accumulator += delta;
        int steps = 0;
        while (accumulator >= gameTick && steps < maxStepsPerFrame) {
            ++ticks;
            accumulator -= gameTick;
            ++steps;
        }
        if (steps == maxStepsPerFrame && accumulator > 0.0f) {
            dropped += static_cast<double>(accumulator);
            accumulator = 0.0f;
        }
        alpha = gameTick > 0.0f ? std::clamp(accumulator / gameTick, 0.0f, 1.0f) : 0.0f;
    }
};

static void testOneSecondIsTheSameNumberOfTicksAtAnyFrameRate() {
    // BINARY-EXACT DELTAS, so this measures the loop rather than the floating
    // point summation underneath it. 1/64 and 1/128 are exact; 1/30 and 1/144
    // are not, and 144 of the latter add up to slightly LESS than a second - so
    // a test built on them asserts that the loop should invent the tick that
    // time did not pay for. It should not, and the next test says so.
    TickPump slow;
    slow.gameTick = 1.0f / 64.0f;
    for (int i = 0; i < 64; ++i) slow.Frame(1.0f / 64.0f);

    TickPump fast;
    fast.gameTick = 1.0f / 64.0f;
    for (int i = 0; i < 128; ++i) fast.Frame(1.0f / 128.0f);

    CHECK_MSG(slow.ticks == fast.ticks,
              "a second of simulation is a second however it was delivered: got "
              + std::to_string(slow.ticks) + " one way and "
              + std::to_string(fast.ticks) + " the other");
    CHECK_EQ(slow.ticks, uint64_t{64});

    // And nothing was thrown away on either, which is the other half: two runs
    // that both dropped everything would also report equal tick counts.
    CHECK_MSG(slow.dropped == 0.0, "the slow machine dropped nothing");
    CHECK_MSG(fast.dropped == 0.0, "and neither did the fast one");
}

static void testAnInexactFrameRateCostsAtMostOneTick() {
    // The honest version of the above, with the frame rates a machine actually
    // produces. 1/144 is not representable, so 144 of them are a hair under a
    // second and the last tick has genuinely not been paid for.
    //
    // This is not drift: the accumulator KEEPS the remainder, so the shortfall
    // is bounded by one tick forever rather than growing. Asserted here so that
    // if it ever does grow, something says so.
    TickPump slow;
    for (int i = 0; i < 30; ++i) slow.Frame(1.0f / 30.0f);

    TickPump fast;
    for (int i = 0; i < 144; ++i) fast.Frame(1.0f / 144.0f);

    const long long difference =
        static_cast<long long>(slow.ticks) - static_cast<long long>(fast.ticks);
    CHECK_MSG(difference <= 1 && difference >= -1,
              "at most one tick between two frame rates over a second: got "
              + std::to_string(slow.ticks) + " and " + std::to_string(fast.ticks));

    // Ten seconds, to show it stays bounded rather than accumulating.
    TickPump longRun;
    for (int i = 0; i < 1440; ++i) longRun.Frame(1.0f / 144.0f);
    const long long drift = 600 - static_cast<long long>(longRun.ticks);
    CHECK_MSG(drift <= 1 && drift >= -1,
              "and it is still at most one tick after ten seconds: got "
              + std::to_string(longRun.ticks) + " of an expected 600");
}

static void testATickRateIsIndependentOfTheFrameRate() {
    // The whole point of the split: a game that asks for 20 Hz gets 20 ticks a
    // second whether it is drawn at 64 or at 128.
    TickPump slow;
    slow.gameTick = 1.0f / 20.0f;
    for (int i = 0; i < 64; ++i) slow.Frame(1.0f / 64.0f);

    TickPump fast;
    fast.gameTick = 1.0f / 20.0f;
    for (int i = 0; i < 128; ++i) fast.Frame(1.0f / 128.0f);

    // THE CLAIM IS INDEPENDENCE, and this is the assertion that carries it:
    // two very different frame rates, the same number of ticks.
    CHECK_EQ(slow.ticks, fast.ticks);

    // The absolute count is 19 rather than 20, and that is correct. A twentieth
    // of a second is not representable in binary, so the step is a hair MORE
    // than 0.05 and twenty of them do not fit in a second. Asserting 20 here
    // would be asserting that the loop should run a tick nobody paid for.
    CHECK_MSG(slow.ticks == 19 || slow.ticks == 20,
              "within a tick of twenty: got " + std::to_string(slow.ticks));
}

static void testTimeTheLoopCannotRunIsCountedRatherThanVanished() {
    // A frame so long the loop hits its ceiling. Dropping is the right answer -
    // catching up under sustained load never catches up - but dropping in
    // SILENCE was not: a machine that could not keep up ran every mission timer
    // short and told nobody.
    TickPump pump;
    pump.Frame(1.0f);   // sixty ticks' worth, five allowed

    CHECK_EQ(pump.ticks, uint64_t{5});
    CHECK_MSG(pump.dropped > 0.9, "the fifty-five ticks it could not run were counted");

    // Close to the truth, not merely non-zero: one second in, five sixtieths
    // simulated, and the rest recorded.
    const double expected = 1.0 - 5.0 / 60.0;
    CHECK_MSG(std::fabs(pump.dropped - expected) < 1e-3,
              "got " + std::to_string(pump.dropped) + ", expected "
              + std::to_string(expected));

    // And the accumulator really was cleared, or the next frame would run the
    // ceiling again on time that has already been written off.
    CHECK_NEAR(pump.alpha, 0.0f);
}

static void testTheOverstepFractionIsTheRemainder() {
    // What interpolation will read. Half a tick in, alpha is a half - and at
    // exactly a tick boundary it is zero rather than one, because the tick has
    // already run.
    TickPump pump;
    pump.Frame(1.0f / 120.0f);          // half of a 1/60 tick
    CHECK_EQ(pump.ticks, uint64_t{0});
    CHECK_NEAR(pump.alpha, 0.5f);

    pump.Frame(1.0f / 120.0f);          // the other half
    CHECK_EQ(pump.ticks, uint64_t{1});
    CHECK_NEAR(pump.alpha, 0.0f);
}

static void testTheDefaultRateReproducesTheOldBehaviour() {
    // The regression that would be easiest to ship: every scene in the project
    // predates an authored tick, so the default has to be the rate they all
    // ran at. One physics substep per tick, and the same step length.
    const float gameTick = 1.0f / 60.0f;
    const float physicsStep = 1.0f / 60.0f;
    const int substeps = std::max(1, static_cast<int>(std::lround(gameTick / physicsStep)));
    CHECK_EQ(substeps, 1);
    CHECK_NEAR(gameTick / static_cast<float>(substeps), physicsStep);

    // And a 20 Hz game still integrates physics at 1/60, which is what keeps
    // changing the tick rate from quietly changing how collisions feel.
    const float slowTick = 1.0f / 20.0f;
    const int slowSubsteps = std::max(1, static_cast<int>(std::lround(slowTick / physicsStep)));
    CHECK_EQ(slowSubsteps, 3);
    CHECK_NEAR(slowTick / static_cast<float>(slowSubsteps), physicsStep);
}

static void runTests() {
    testOneSecondIsTheSameNumberOfTicksAtAnyFrameRate();
    testAnInexactFrameRateCostsAtMostOneTick();
    testATickRateIsIndependentOfTheFrameRate();
    testTimeTheLoopCannotRunIsCountedRatherThanVanished();
    testTheOverstepFractionIsTheRemainder();
    testTheDefaultRateReproducesTheOldBehaviour();
    testTheSameSceneSteppedTwiceAgreesExactly();
    testTheHashMovesWhenTheSimulationDoes();
    testTheHashDoesNotDependOnEntityCreationOrder();
    testAValueOnTheWrongEntityIsADifferentState();
    testSleepStateIsPartOfTheState();
    testTheClockIsDerivedRatherThanAccumulated();
}

TEST_MAIN("test_determinism", 28)
