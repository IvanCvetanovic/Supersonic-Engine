// HUSK's layer: the seam between the ported simulation and the picture.
//
// The sim is held to the Rust game's hashes by test_husk_scenarios. What this
// suite holds is the layer's side of the bargain:
//
//  - it runs the game at HUSK's rate, 20 Hz, on the engine's clock;
//  - it steps exactly the sim and nothing else - the layer's World hashes, tick
//    for tick, exactly as a bare World built and stepped beside it. That covers
//    the event draining, which is the one place a view could reach into a sim;
//  - its drawables stand where the sim says;
//  - a click read on the tick becomes the order HUSK's own client would send.
//
// No window and no Vulkan: a layer is handed a registry and called.

#include "TestHarness.hpp"

#include "HuskLayer.hpp"

#include "core/Components.hpp"
#include "core/Input.hpp"
#include "core/SimulationClock.hpp"
#include "core/ViewportInfo.hpp"

#include "sim/Mission.hpp"
#include "sim/World.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <string>

using namespace Supersonic;
using husk::HuskLayer;

namespace {

constexpr float kTick = husk::kSimDt;

// The viewport a frame would publish: the whole 1280x720 window, pointer over
// the game.
void publishViewport(entt::registry& registry) {
    ViewportInfo viewport;
    viewport.rect.min = glm::vec2(0.0f, 0.0f);
    viewport.rect.max = glm::vec2(1280.0f, 720.0f);
    viewport.pointerOverGame = true;
    registry.ctx().insert_or_assign(viewport);
}

entt::entity primaryCamera(entt::registry& registry) {
    for (auto [entity, camera] : registry.view<CameraComponent>().each()) {
        if (camera.isPrimary) return entity;
    }
    return entt::null;
}

// Where a world point lands on the 1280x720 viewport, by the same matrices
// the renderer and the layer's picking use.
glm::vec2 project(entt::registry& registry, const glm::vec3& world) {
    const auto& camera = registry.get<CameraComponent>(primaryCamera(registry));
    const glm::vec4 clip = camera.getProjectionMatrix() * camera.getViewMatrix() * glm::vec4(world, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return glm::vec2((ndc.x + 1.0f) * 0.5f * 1280.0f, (ndc.y + 1.0f) * 0.5f * 720.0f);
}

// One tick with the given edges and pointer, fed through Input's replay path -
// which is also the path a recorded session takes, so this is the real route.
void tickWith(HuskLayer& layer, entt::registry& registry, const glm::vec2& pointer,
              std::vector<std::string> pressed, std::vector<std::string> released) {
    Input::TickInput input;
    input.mousePosition = pointer;
    input.pressed = std::move(pressed);
    input.released = std::move(released);
    Input::BeginReplayedTick(input);
    layer.OnFixedUpdate(registry, kTick);
    Input::EndReplayedTick();
}

const husk::Entity* firstPlayerUnit(const husk::World& w) {
    for (const auto& [id, e] : w.entities) {
        if (e.isUnit() && e.team == 0 && e.indexed) return &e;
    }
    return nullptr;
}

} // namespace

static void testAttachingRunsTheGameAtItsOwnRate() {
    entt::registry registry;
    HuskLayer layer;
    layer.OnAttach(registry);

    CHECK_MSG(layer.LoadError().empty(), "the default mission loads: " + layer.LoadError());
    CHECK(layer.SimWorld() != nullptr);
    const auto* clock = registry.ctx().find<SimulationClock>();
    CHECK_MSG(clock != nullptr && clock->fixedDelta == husk::kSimDt,
              "the engine ticks at HUSK's 20 Hz, not the engine default");
    CHECK_MSG(layer.SimWorld()->mission.has_value(), "the mission is installed");
    CHECK_MSG(primaryCamera(registry) != entt::null, "there is a camera to see it through");

    layer.OnDetach(registry);
    CHECK(layer.SimWorld() == nullptr);
}

// The claim the whole layer rests on. If the view touched the sim - drained a
// list the sim reads, pushed an order nobody gave, stepped twice - the two
// hashes part on the tick it happened.
static void testTheLayerStepsExactlyTheSim() {
    entt::registry registry;
    HuskLayer layer("first_light", 7);
    layer.OnAttach(registry);
    CHECK_MSG(layer.LoadError().empty(), layer.LoadError());

    husk::World reference(husk::sharedCatalogs(), 7);
    CHECK(!husk::loadMission(reference, "first_light").has_value());

    int agreed = 0;
    for (int t = 0; t < 600; ++t) {
        layer.OnFixedUpdate(registry, kTick);
        husk::step(reference);
        // Drained here as the layer drains them, so the comparison is of the
        // sims and not of two event backlogs.
        if (layer.SimWorld()->hash != reference.hash) break;
        ++agreed;
    }
    CHECK_MSG(agreed == 600, "the layer's world agreed with a bare one for " + std::to_string(agreed) +
                                 " of 600 ticks");
    CHECK_EQ(layer.SimWorld()->tick, reference.tick);
}

static void testDrawablesStandWhereTheSimSays() {
    entt::registry registry;
    HuskLayer layer(HuskLayer::kSandbox, 7);
    layer.OnAttach(registry);
    for (int t = 0; t < 40; ++t) layer.OnFixedUpdate(registry, kTick);

    const husk::World& w = *layer.SimWorld();
    size_t drawn = 0;
    size_t misplaced = 0;
    for (const auto& [id, e] : w.entities) {
        const entt::entity d = layer.DrawableFor(id);
        if (d == entt::null) continue;
        ++drawn;
        const auto& interp = registry.get<InterpolatedTransformComponent>(d);
        const auto& transform = registry.get<TransformComponent>(d);
        // Sim .x is world X and sim .y is world Z; the layer writes the
        // authoritative transform, and interpolation starts from it.
        if (std::fabs(transform.position.x - e.pos.cur.x) > 1e-4f ||
            std::fabs(transform.position.z - e.pos.cur.y) > 1e-4f) {
            ++misplaced;
        }
        (void)interp;
    }
    CHECK_MSG(drawn == w.entities.size(), "every sim entity has a drawable (" + std::to_string(drawn) + " of " +
                                               std::to_string(w.entities.size()) + ")");
    CHECK_MSG(misplaced == 0, std::to_string(misplaced) + " drawable(s) are not where their entity is");

    // And one that dies takes its drawable with it.
    const uint32_t victim = w.entities.begin()->first;
    layer.SimWorld()->despawnNow(victim);
    layer.OnFixedUpdate(registry, kTick);
    CHECK_MSG(layer.DrawableFor(victim) == entt::null, "a despawned entity's drawable is gone");
}

// Left-click a unit, right-click the ground beside it: the unit is ordered to
// walk there, which is HUSK's plain right-click (input.rs, the `_` arm).
static void testAClickOnTheTickOrdersTheUnit() {
    entt::registry registry;
    HuskLayer layer(HuskLayer::kSandbox, 7);
    layer.OnAttach(registry);
    publishViewport(registry);
    layer.OnUpdate(registry, 1.0f / 60.0f); // places the camera for this viewport
    layer.OnFixedUpdate(registry, kTick);

    const husk::Entity* unit = firstPlayerUnit(*layer.SimWorld());
    CHECK_MSG(unit != nullptr, "the sandbox has a player unit");
    if (!unit) return;
    const uint32_t id = unit->id;

    const entt::entity drawable = layer.DrawableFor(id);
    CHECK(drawable != entt::null);
    if (drawable == entt::null) return;
    const glm::vec2 onUnit = project(registry, registry.get<TransformComponent>(drawable).position);
    CHECK_MSG(onUnit.x > 0.0f && onUnit.x < 1280.0f && onUnit.y > 0.0f && onUnit.y < 720.0f,
              "the camera starts over the player's units");

    tickWith(layer, registry, onUnit, {HuskLayer::kSelect}, {});
    tickWith(layer, registry, onUnit, {}, {HuskLayer::kSelect});
    CHECK_MSG(layer.Selection().size() == 1 && layer.Selection()[0] == id, "the click selected the unit");

    // Open ground: east of the unit, clear of everything by three units, so
    // the click cannot land on a unit or a source and become a different
    // order. The sandbox map is flat, so height zero is the ground.
    const husk::World& w = *layer.SimWorld();
    husk::Vec2 goal = unit->pos.cur;
    bool found = false;
    for (float dx = 4.0f; dx <= 24.0f && !found; dx += 1.0f) {
        for (float dz : {0.0f, 4.0f, -4.0f}) {
            const husk::Vec2 candidate(unit->pos.cur.x + dx, unit->pos.cur.y + dz);
            bool clear = true;
            for (const auto& [otherId, other] : w.entities) {
                if (other.pos.cur.distance(candidate) < 3.0f) clear = false;
            }
            if (clear) {
                goal = candidate;
                found = true;
                break;
            }
        }
    }
    CHECK_MSG(found, "open ground near the unit");
    const glm::vec2 onGround = project(registry, glm::vec3(goal.x, 0.0f, goal.y));
    husk::Vec2 aimed;
    CHECK(layer.ScreenToGround(registry, onGround, aimed));
    CHECK_MSG(aimed.distance(goal) < 0.05f, "the pointer meets the ground where it was aimed");
    tickWith(layer, registry, onGround, {HuskLayer::kCommand}, {});

    const husk::Entity* after = layer.SimWorld()->get(id);
    CHECK(after != nullptr);
    if (!after) return;
    CHECK_MSG(!after->orders.empty() && after->orders.front().kind == husk::OrderKind::Point,
              "the unit has a move order");
    if (!after->orders.empty()) {
        // Within a cell, not exactly: drainOrders adds the slot's formation
        // offset and snaps a move onto the nav grid (Orders.cpp), as the game
        // does with the same click.
        CHECK_MSG(after->orders.front().target.distance(aimed) <= 1.0f,
                  "the order goes where the pointer met the ground");
    }
}

static void testAMissionThatDoesNotLoadIsReportedNotThrown() {
    entt::registry registry;
    HuskLayer layer("no_such_mission", 7);
    layer.OnAttach(registry);
    CHECK_MSG(!layer.LoadError().empty(), "the failure is kept for the HUD");
    CHECK(layer.SimWorld() != nullptr);
    layer.OnFixedUpdate(registry, kTick);
    CHECK_EQ(layer.SimWorld()->tick, static_cast<uint64_t>(1));
}

void runTests() {
    testAttachingRunsTheGameAtItsOwnRate();
    testTheLayerStepsExactlyTheSim();
    testDrawablesStandWhereTheSimSays();
    testAClickOnTheTickOrdersTheUnit();
    testAMissionThatDoesNotLoadIsReportedNotThrown();
}

TEST_MAIN("test_husk_layer", 26)
