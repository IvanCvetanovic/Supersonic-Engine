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
#include "core/InterpolationSystem.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/PhysicsSettings.hpp"
#include "core/Components.hpp"
#include "core/SceneSerializer.hpp"

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

static void testTheCanonicalSceneHashesToTheSameNumberOnEveryBuild() {
    // THE CROSS-TOOLCHAIN GATE, and the only test here that is about a machine
    // rather than about the code.
    //
    // Every other test compares two hashes computed in one process, so a build
    // whose floats behave differently agrees with itself perfectly and passes
    // all of them. That is exactly the shape of failure this suite exists to
    // catch, one level up: reproducibility that holds within a binary and not
    // between two. A recording is only worth sending to somebody if the number
    // means the same thing on their machine.
    //
    // So the number is written down. Four seconds of the fixture scene - six
    // boxes and a ball falling onto a floor, which runs the integrator, the
    // broadphase, the SAT narrowphase and the impulse solver - has one answer,
    // and it is this one.
    //
    // WHEN THIS FAILS, it is a decision and not a chore. Either the simulation
    // changed, in which case every recording on disk has stopped comparing and
    // the constant should move deliberately; or the toolchain changed, in which
    // case a replay does not cross that boundary and that is the thing worth
    // knowing. Updating the number to make the suite green without deciding
    // which of those happened is how this stops meaning anything.
    //
    // Verified across MSVC 2022 (MSVC STL) and GCC 15.2 (libstdc++), both x64
    // Release, on 29 August 2026. Note what that pair does and does not vary:
    // different compiler backends and different standard libraries, but the
    // same UCRT - so `asin` in the Euler conversion and `pow` in the damping
    // curve are the SAME implementations in both. A glibc build would vary
    // those too, and has not been run.
    constexpr uint64_t kCanonical = 8818694387102185031ull;

    const uint64_t actual = runFor(240);
    CHECK_MSG(actual == kCanonical,
              "the canonical scene hashed to " + std::to_string(actual) +
                  ", and this build expects " + std::to_string(kCanonical));
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

static void testTheSameSceneHashesTheSameHoweverManyLoadsPrecededIt() {
    // THE ONE THAT DECIDES WHETHER ANY OF THIS SURVIVES LEAVING THE PROCESS.
    //
    // Every other test here builds one registry and never reloads it, so all
    // of them agree with a hash that depends on how many scenes have been
    // loaded before this one. A real run does not look like that: the editor
    // loads a scene, plays it, stops, loads it again; a packaged game loads a
    // level, the player dies, it loads the same level. If the hash moves across
    // a reload then "this run reproduces" is a claim about one process that has
    // done nothing else, which is not a claim anybody can use.
    //
    // EnTT's entity handle is an index and a VERSION packed into one integer,
    // and destroying an entity bumps the version so a stale handle can be
    // spotted. registry.clear() destroys everything, so the second load gets
    // the same indices back carrying different versions - and a seed taken from
    // the whole handle therefore differs while the state is identical.
    entt::registry registry;

    buildScene(registry);
    const uint64_t first = StateHash::Compute(registry);

    // The same scene again, into the same registry, exactly as a reload does.
    registry.clear();
    buildScene(registry);
    const uint64_t second = StateHash::Compute(registry);

    CHECK_MSG(first == second,
              "a reloaded scene is the same state and must hash the same: " +
                  std::to_string(first) + " vs " + std::to_string(second));

    // And a third, because the version increments each time: two loads could
    // agree by an accident that three would not survive.
    registry.clear();
    buildScene(registry);
    CHECK_MSG(StateHash::Compute(registry) == first, "and again on the third load");
}

static void testASceneLoadedTwiceThroughTheSerializerHashesTheSame() {
    // The test above proves the HASH does not care how many times a registry
    // has been cleared. This proves the thing that actually has to be true for
    // a replay to be worth anything: that the real load path - the one an
    // editor Stop/Play and a packaged game's level reload both go through -
    // puts the world back in a state the oracle calls identical.
    //
    // Those are two different claims and only this one involves
    // SceneSerializer, which clears the registry and then creates one entity
    // per array element in file order. That order is what makes the indices
    // line up; if a load ever started creating before clearing, or resolved
    // parents by creating out of order, the hash would move and this would say
    // so.
    entt::registry authored;
    buildScene(authored);
    const std::string text = SceneSerializer::SerializeToString(authored);

    entt::registry loaded;
    const SerializationResult first = SceneSerializer::DeserializeFromString(loaded, text);
    CHECK_MSG(first.ok, "the scene must load at all: " + first.message);
    const uint64_t firstHash = StateHash::Compute(loaded);

    // The same text again, into the registry that already holds it. This is a
    // reload, not a fresh process.
    const SerializationResult second = SceneSerializer::DeserializeFromString(loaded, text);
    CHECK_MSG(second.ok, "and load again: " + second.message);

    CHECK_MSG(StateHash::Compute(loaded) == firstHash,
              "reloading a scene must land on the state it was already in");

    // And a registry that has never loaded anything agrees with one that has,
    // which is the cross-PROCESS half: a recording made this morning and a
    // replay run this afternoon are two different registries with two different
    // histories, and they have to agree about tick zero or nothing after it
    // means anything.
    entt::registry fresh;
    CHECK_MSG(SceneSerializer::DeserializeFromString(fresh, text).ok, "a fresh registry loads it");
    CHECK_MSG(StateHash::Compute(fresh) == firstHash,
              "a scene's state does not depend on what the registry did before it");
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

static void testLoadingASceneStartsItsClockAtZero() {
    // The context survives registry.clear(), so a scene load used to inherit
    // the tick counter of everything the process had already simulated. It read
    // as harmless - a counter nobody prints - and it is not, because the number
    // it feeds is narrowed to a float BEFORE anything subtracts from it:
    // SecondsF() is tick * fixedDelta as a float, and a script's elapsed time
    // is derived from that. The lattice it lands on gets coarser as the tick
    // count grows, so the same logical tick of the same scene produced a
    // different number depending on what had run beforehand, and a script
    // driving std::sin off it moved somewhere else.
    entt::registry registry;
    buildScene(registry);

    // A process that has been running a while.
    auto& before = registry.ctx().emplace<SimulationClock>();
    before.tick = 500000;
    before.droppedSeconds = 4.25;
    before.alpha = 0.5f;
    before.fixedDelta = 1.0f / 20.0f;

    const std::string text = SceneSerializer::SerializeToString(registry);
    CHECK_MSG(SceneSerializer::DeserializeFromString(registry, text).ok, "the scene loads");

    const auto& after = registry.ctx().get<SimulationClock>();
    CHECK_EQ(after.tick, uint64_t{0});
    CHECK_MSG(after.droppedSeconds == 0.0, "a fresh scene has lost no time yet");
    CHECK_NEAR(after.alpha, 0.0f);

    // And the authored rate still arrives, which is the part that must NOT be
    // reset to a default - the scene said 20 Hz and it means it.
    CHECK_NEAR(after.fixedDelta, 1.0f / 20.0f);
}

static void testASceneWithNoAuthoredRateDoesNotInheritTheLastOnes() {
    // Every scene written before the tick could be authored has no Simulation
    // block, and the rate assignment used to sit inside the check for one - so
    // such a scene silently kept whatever the previous scene had asked for.
    // Loading a 20 Hz level and then a 60 Hz one that predates the block ran the
    // second at 20, which is the file's own stated rule about the context being
    // always assigned rather than merely read, broken three lines below where it
    // is written down.
    entt::registry registry;

    auto& clock = registry.ctx().emplace<SimulationClock>();
    clock.fixedDelta = 1.0f / 20.0f;

    // A scene from before the block existed: entities and nothing else.
    CHECK_MSG(SceneSerializer::DeserializeFromString(registry, R"({"Entities": []})").ok,
              "a scene with no Simulation block still loads");

    CHECK_NEAR(registry.ctx().get<SimulationClock>().fixedDelta, 1.0f / 60.0f);
}

static void testAnEntityWithNoPlaceInTheWorldIsStillState() {
    // A HUD element has no transform, by design, because it lives in screen
    // space - and ScriptEngine deliberately runs scripts on entities with or
    // without one, so that a script can drive it.
    //
    // While the hash opened on view<TransformComponent> such an entity was
    // invisible twice: it contributed nothing, and it was not even counted, so
    // the count that exists to stop things hiding was itself inside the filter
    // that hid them. An entire menu hashed the same as an empty registry, and a
    // replay of one would have reported success without looking at anything.
    entt::registry empty;
    entt::registry withHud;

    const auto hud = withHud.create();
    withHud.emplace<ScriptComponent>(hud);

    CHECK_MSG(StateHash::Compute(empty) != StateHash::Compute(withHud),
              "an entity with no transform is still an entity");

    // And existing is enough on its own. A spawn that has not been given a
    // position yet must still move the number, or a replay that diverged by one
    // spawn says nothing until the spawned thing is placed.
    entt::registry bare;
    bare.create();
    CHECK_MSG(StateHash::Compute(bare) != StateHash::Compute(empty),
              "spawning an entity that carries nothing is still a change");
}

static void testWhereAScriptIsCountsAsState() {
    // THE ONE THAT WOULD HAVE MADE A REPLAY LIE. Script state is written by one
    // tick and read by the next, which is the inclusion rule the header states,
    // and it was not in the hash. A script keeping a cooldown or a state-machine
    // phase could therefore diverge on tick one and be reported as agreeing -
    // until the tick that finally turned the counter into a position, which
    // names a tick thousands after the one that actually went wrong.
    entt::registry a;
    entt::registry b;

    for (entt::registry* r : { &a, &b }) {
        const auto e = r->create();
        r->emplace<TransformComponent>(e, glm::vec3(1.0f, 2.0f, 3.0f));
        r->emplace<ScriptComponent>(e);
    }

    CHECK_MSG(StateHash::Compute(a) == StateHash::Compute(b),
              "two identical scripts agree to begin with");

    // One of them has been somewhere the other has not, and nothing it can be
    // seen to have done has happened yet.
    for (auto e : b.view<ScriptComponent>()) {
        b.get<ScriptComponent>(e).state.emplace_back("cooldown", 0.5f);
    }

    CHECK_MSG(StateHash::Compute(a) != StateHash::Compute(b),
              "a counter a later tick will read is state, even before it moves anything");

    // The name is part of it, not only the number: two scripts holding 0.5
    // under different keys are in different places.
    for (auto e : a.view<ScriptComponent>()) {
        a.get<ScriptComponent>(e).state.emplace_back("charge", 0.5f);
    }
    CHECK_MSG(StateHash::Compute(a) != StateHash::Compute(b),
              "and which counter holds it matters as much as what it holds");
}

static void testTheWorldsGravityIsPartOfTheState() {
    // Everything else the hash walks is on an entity, and the registry's
    // CONTEXT is read by every tick and was hashed by nothing. Most of what
    // lives there is a pointer to a subsystem or a cache and is rightly out;
    // gravity is not. Every integration step reads it, so two runs under
    // different gravity are two different simulations - and the oracle called
    // them identical until something had fallen far enough for a transform to
    // notice, which names a tick long after the one that differed.
    entt::registry earth;
    entt::registry moon;

    for (entt::registry* r : { &earth, &moon }) {
        const auto e = r->create();
        r->emplace<TransformComponent>(e, glm::vec3(0.0f, 5.0f, 0.0f));
        r->emplace<RigidBodyComponent>(e);
        r->ctx().emplace<PhysicsSettings>();
    }
    moon.ctx().get<PhysicsSettings>().gravity = glm::vec3(0.0f, -1.62f, 0.0f);

    CHECK_MSG(StateHash::Compute(earth) != StateHash::Compute(moon),
              "two worlds pulling differently are different worlds, before anything has moved");

    // And the ground plane, which the solver also reads every step.
    entt::registry floored;
    const auto e = floored.create();
    floored.emplace<TransformComponent>(e, glm::vec3(0.0f, 5.0f, 0.0f));
    floored.emplace<RigidBodyComponent>(e);
    auto& settings = floored.ctx().emplace<PhysicsSettings>();
    settings.hasGroundPlane = true;

    CHECK_MSG(StateHash::Compute(floored) != StateHash::Compute(earth),
              "a world with a floor is not the same as one without");
}

// --- a game whose state is not in the registry ------------------------------
//
// The shape this engine recommends for a real game, and the shape the oracle
// could not see. Wolf Brigade's authoritative state is a C++ object graph owned
// by an EngineLayer - a match, its units, a map of resources - and none of it is
// components. `Compute` walks a registry, so it walked one containing none of it
// and returned a number that agreed with itself perfectly.

namespace {

// Stands in for that object graph. Deliberately not components, and reached the
// way a layer's singleton actually is: a pointer in the registry's context.
struct PretendMatch {
    int gold{0};
    float timer{0.0f};
    std::vector<int> unitHealth;
};

void registerMatchContributor() {
    StateHash::RegisterContributor("PretendMatch",
        [](const entt::registry& registry, StateHash::Mixer& out) {
            const auto* slot = registry.ctx().find<PretendMatch*>();
            if (!slot || !*slot) return;
            const PretendMatch& match = **slot;
            out.I64(match.gold);
            out.F32(match.timer);
            for (const int health : match.unitHealth) out.I64(health);
        });
}

} // namespace

static void testAGamesOwnStateIsInvisibleUntilItRegisters() {
    // THE ONE THAT SHOWS WHY THE API EXISTS. Two matches that differ in every
    // number a player would care about, hashing identically, because none of it
    // is in the registry.
    StateHash::ClearContributors();

    entt::registry winning;
    entt::registry losing;
    PretendMatch ahead{900, 12.5f, {100, 100, 100}};
    PretendMatch behind{40, 3.25f, {7, 0, 0}};

    winning.ctx().emplace<PretendMatch*>(&ahead);
    losing.ctx().emplace<PretendMatch*>(&behind);

    CHECK_MSG(StateHash::Compute(winning) == StateHash::Compute(losing),
              "before registering, two completely different matches hash the same - "
              "which is the failure this closes, and it looks exactly like success");

    // And with the game's state declared, they stop agreeing.
    registerMatchContributor();
    CHECK_MSG(StateHash::Compute(winning) != StateHash::Compute(losing),
              "a registered contributor makes the game's own state part of the oracle");

    StateHash::ClearContributors();
}

static void testAnUnregisteredEngineHashesExactlyAsItAlwaysDid() {
    // The compatibility claim, and it is not cosmetic: a recording made before
    // this existed has to keep comparing against the engine that made it, or
    // every replay on disk quietly stops meaning anything.
    StateHash::ClearContributors();

    entt::registry registry;
    buildScene(registry);
    const uint64_t before = StateHash::Compute(registry);

    // Registering something that contributes NOTHING still counts as a
    // contributor, so this also pins that the guard is on the list being empty
    // rather than on the bytes being zero.
    CHECK_MSG(StateHash::RegisterContributor("Nothing",
                  [](const entt::registry&, StateHash::Mixer&) {}),
              "an empty contributor registers");
    CHECK_MSG(StateHash::Compute(registry) != before,
              "and declaring one changes the number, because the count is part of it");

    StateHash::ClearContributors();
    CHECK_MSG(StateHash::Compute(registry) == before,
              "with none registered the hash is exactly what it was before any of this");
}

static void testTheOrderContributorsRegisteredInCannotMatter() {
    // The same argument the entity walk makes about EnTT's iteration order, one
    // level up. Two contributors registered in the other order is not a
    // divergence, and a hash that folded them in sequence would call it one -
    // which would make moving a registration between two translation units a
    // failing replay.
    entt::registry registry;
    PretendMatch match{5, 1.0f, {3}};
    registry.ctx().emplace<PretendMatch*>(&match);

    auto goldOnly = [](const entt::registry& r, StateHash::Mixer& out) {
        const auto* slot = r.ctx().find<PretendMatch*>();
        if (slot && *slot) out.I64((*slot)->gold);
    };
    auto timerOnly = [](const entt::registry& r, StateHash::Mixer& out) {
        const auto* slot = r.ctx().find<PretendMatch*>();
        if (slot && *slot) out.F32((*slot)->timer);
    };

    StateHash::ClearContributors();
    StateHash::RegisterContributor("Gold", goldOnly);
    StateHash::RegisterContributor("Timer", timerOnly);
    const uint64_t forward = StateHash::Compute(registry);

    StateHash::ClearContributors();
    StateHash::RegisterContributor("Timer", timerOnly);
    StateHash::RegisterContributor("Gold", goldOnly);
    const uint64_t backward = StateHash::Compute(registry);

    CHECK_MSG(forward == backward,
              "registration order is not state: " + std::to_string(forward) + " vs " +
                  std::to_string(backward));

    // But WHICH contributor holds a value still matters, or seeding by name
    // would be pointless - two contributors swapping what they read is a
    // different world, exactly as two entities swapping positions is.
    StateHash::ClearContributors();
    StateHash::RegisterContributor("Gold", timerOnly);
    StateHash::RegisterContributor("Timer", goldOnly);
    CHECK_MSG(StateHash::Compute(registry) != forward,
              "and the name a value is filed under is part of the state");

    StateHash::ClearContributors();
}

static void testAContributorNameIsClaimedOnce() {
    StateHash::ClearContributors();
    const auto noop = [](const entt::registry&, StateHash::Mixer&) {};

    CHECK(StateHash::RegisterContributor("Match", noop));
    CHECK_MSG(!StateHash::RegisterContributor("Match", noop),
              "a second registration under one name is refused rather than shadowing");
    CHECK_EQ(StateHash::ContributorCount(), size_t{1});

    CHECK_MSG(!StateHash::RegisterContributor("", noop), "an unnamed contributor has no seed");
    CHECK_MSG(!StateHash::RegisterContributor("Empty", nullptr), "and nothing to call");
    CHECK_EQ(StateHash::ContributorCount(), size_t{1});

    StateHash::ClearContributors();
    CHECK_EQ(StateHash::ContributorCount(), size_t{0});
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

// --- drawing between two ticks --------------------------------------------
//
// A simulation thinking twenty times a second and drawn a hundred and
// forty-four times a second draws the same twenty positions seven times each,
// and looks exactly like that. Interpolation hides the tick rate without
// changing it - and the way to get it wrong is to let the drawn value leak back
// into the simulation, which would make the world depend on the frame rate and
// undo the entire reason the tick is fixed.

static void tickOnce(entt::registry& registry, const glm::vec3& moveTo) {
    InterpolationSystem::BeginTick(registry);
    for (auto entity : registry.view<TransformComponent>()) {
        registry.get<TransformComponent>(entity).position = moveTo;
    }
    InterpolationSystem::EndTick(registry);
}

static void testAFrameIsDrawnBetweenTheLastTwoTicks() {
    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = glm::vec3(0.0f);
    registry.emplace<InterpolatedTransformComponent>(entity);

    tickOnce(registry, glm::vec3(0.0f));    // settles previous == current
    tickOnce(registry, glm::vec3(10.0f, 0.0f, 0.0f));

    InterpolationSystem::Apply(registry, 0.0f);
    CHECK_NEAR(registry.get<TransformComponent>(entity).position.x, 0.0f);

    InterpolationSystem::Apply(registry, 0.5f);
    CHECK_NEAR(registry.get<TransformComponent>(entity).position.x, 5.0f);

    InterpolationSystem::Apply(registry, 1.0f);
    CHECK_NEAR(registry.get<TransformComponent>(entity).position.x, 10.0f);
}

static void testTheDrawnValueNeverReachesTheSimulation() {
    // THE ONE THAT MATTERS. If a tick reads the interpolated transform rather
    // than the simulated one, the world's state depends on the frame rate - the
    // exact bug the fixed tick exists to prevent, reintroduced by the feature
    // meant to hide it. And it would be invisible: the motion would look right.
    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = glm::vec3(0.0f);
    registry.emplace<InterpolatedTransformComponent>(entity);

    tickOnce(registry, glm::vec3(0.0f));
    tickOnce(registry, glm::vec3(10.0f, 0.0f, 0.0f));

    // A frame is drawn part way between the two ticks...
    InterpolationSystem::Apply(registry, 0.5f);
    CHECK_NEAR(registry.get<TransformComponent>(entity).position.x, 5.0f);

    // ...and the next tick must start from TEN, not from five. BeginTick puts
    // the authoritative value back before anything reads it.
    InterpolationSystem::BeginTick(registry);
    CHECK_MSG(std::fabs(registry.get<TransformComponent>(entity).position.x - 10.0f) < 1e-4f,
              "the tick starts from the simulated position, not the drawn one: got "
              + std::to_string(registry.get<TransformComponent>(entity).position.x));

    // And a run of ticks lands where the simulation says regardless of how many
    // frames were drawn between them.
    for (auto e : registry.view<TransformComponent>()) {
        registry.get<TransformComponent>(e).position = glm::vec3(20.0f, 0.0f, 0.0f);
    }
    InterpolationSystem::EndTick(registry);
    InterpolationSystem::Apply(registry, 1.0f);
    CHECK_NEAR(registry.get<TransformComponent>(entity).position.x, 20.0f);
}

static void testAnEntitysFirstTickDoesNotStreakInFromNowhere() {
    // Before an entity has run a tick there is no previous state. Interpolating
    // out of an uninitialised one drags it in from wherever that memory
    // pointed, which for an entity spawned far from the origin is a visible
    // streak across the map on the frame it appears.
    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = glm::vec3(500.0f, 0.0f, -300.0f);
    registry.emplace<InterpolatedTransformComponent>(entity);

    // Drawn before it has ever ticked: left exactly where it was put.
    InterpolationSystem::Apply(registry, 0.5f);
    CHECK_NEAR(registry.get<TransformComponent>(entity).position.x, 500.0f);
    CHECK_NEAR(registry.get<TransformComponent>(entity).position.z, -300.0f);

    // And after its first tick it still sits still rather than sliding in.
    tickOnce(registry, glm::vec3(500.0f, 0.0f, -300.0f));
    InterpolationSystem::Apply(registry, 0.5f);
    CHECK_NEAR(registry.get<TransformComponent>(entity).position.x, 500.0f);
}

static void testAnEntityWithoutTheComponentIsUntouched() {
    // Opt-in. Scenery, UI, and anything a script moves per frame must draw
    // where they are rather than one tick in the past.
    entt::registry registry;
    const entt::entity plain = registry.create();
    registry.emplace<TransformComponent>(plain).position = glm::vec3(7.0f, 8.0f, 9.0f);

    InterpolationSystem::BeginTick(registry);
    InterpolationSystem::EndTick(registry);
    InterpolationSystem::Apply(registry, 0.5f);

    CHECK_NEAR(registry.get<TransformComponent>(plain).position.x, 7.0f);
    CHECK_NEAR(registry.get<TransformComponent>(plain).position.y, 8.0f);
    CHECK_NEAR(registry.get<TransformComponent>(plain).position.z, 9.0f);
}

static void testARotationTakesTheShortWayRound() {
    // Euler angles wrap. A turret at 179 degrees turning to -179 has moved two
    // degrees; a straight lerp takes it three hundred and fifty-eight the other
    // way, which is the turret spinning a full circle for one frame, once per
    // lap.
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    registry.emplace<InterpolatedTransformComponent>(entity);

    const float nearPi = 3.12413936f;    // +179 degrees
    const float nearNegPi = -3.12413936f;

    transform.rotation = glm::vec3(0.0f, nearPi, 0.0f);
    tickOnce(registry, glm::vec3(0.0f));
    for (auto e : registry.view<TransformComponent>()) {
        registry.get<TransformComponent>(e).rotation = glm::vec3(0.0f, nearNegPi, 0.0f);
    }
    InterpolationSystem::EndTick(registry);

    InterpolationSystem::Apply(registry, 0.5f);
    const float y = registry.get<TransformComponent>(entity).rotation.y;

    // Half way across a two-degree gap is one degree past 179, which wraps to
    // just under -180. What it must NOT be is anywhere near zero - that is the
    // long way round.
    CHECK_MSG(std::fabs(y) > 3.0f,
              "the short way round keeps it near the wrap, not through zero: got "
              + std::to_string(y));
}

static void runTests() {
    testAFrameIsDrawnBetweenTheLastTwoTicks();
    testTheDrawnValueNeverReachesTheSimulation();
    testAnEntitysFirstTickDoesNotStreakInFromNowhere();
    testAnEntityWithoutTheComponentIsUntouched();
    testARotationTakesTheShortWayRound();
    testOneSecondIsTheSameNumberOfTicksAtAnyFrameRate();
    testAnInexactFrameRateCostsAtMostOneTick();
    testATickRateIsIndependentOfTheFrameRate();
    testTimeTheLoopCannotRunIsCountedRatherThanVanished();
    testTheOverstepFractionIsTheRemainder();
    testTheDefaultRateReproducesTheOldBehaviour();
    testTheSameSceneSteppedTwiceAgreesExactly();
    testTheCanonicalSceneHashesToTheSameNumberOnEveryBuild();
    testTheHashMovesWhenTheSimulationDoes();
    testTheHashDoesNotDependOnEntityCreationOrder();
    testTheSameSceneHashesTheSameHoweverManyLoadsPrecededIt();
    testASceneLoadedTwiceThroughTheSerializerHashesTheSame();
    testAValueOnTheWrongEntityIsADifferentState();
    testLoadingASceneStartsItsClockAtZero();
    testASceneWithNoAuthoredRateDoesNotInheritTheLastOnes();
    testAnEntityWithNoPlaceInTheWorldIsStillState();
    testWhereAScriptIsCountsAsState();
    testTheWorldsGravityIsPartOfTheState();
    testAGamesOwnStateIsInvisibleUntilItRegisters();
    testAnUnregisteredEngineHashesExactlyAsItAlwaysDid();
    testTheOrderContributorsRegisteredInCannotMatter();
    testAContributorNameIsClaimedOnce();
    testSleepStateIsPartOfTheState();
    testTheClockIsDerivedRatherThanAccumulated();
}

TEST_MAIN("test_determinism", 40)
