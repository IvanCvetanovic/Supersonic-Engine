// Regression tests for the physics step.
//
// The integrator ran on the raw wall-clock frame delta with no clamp, and
// ground collision clamped the transform ORIGIN to y = 0 while reflecting
// velocity unconditionally.
//
// The collision half is newer. BoxColliderComponent and SphereColliderComponent
// were stored, shown in the inspector and serialised, but never tested against
// anything: two bodies could occupy the same space with no complaint, and
// RigidBodyComponent::mass was written and never read by anything.

#include "TestHarness.hpp"
#include "core/PhysicsSystem.hpp"
#include "core/PhysicsSettings.hpp"
#include "core/Components.hpp"
#include "core/TransformSystem.hpp"
#include "core/Heightfield.hpp"
#include "core/HeightfieldCache.hpp"
#include "core/TerrainGenerator.hpp"
#include "core/SceneSerializer.hpp"

#include <filesystem>

#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <string>
#include <cstdio>
#include <vector>

using namespace Supersonic;

static void stepFor(entt::registry& registry, float seconds, float step = 1.0f / 60.0f) {
    for (float t = 0.0f; t < seconds; t += step) {
        PhysicsSystem::Update(registry, step);
    }
}

// The world ground plane is off unless a scene asks for it. Every test below
// that is ABOUT the plane says so explicitly, which is the point: it used to be
// there whether anything wanted it or not, and a test could not tell the
// difference between a body resting on a floor and a body resting on the
// integrator.
static void enableGroundPlane(entt::registry& registry, float height = 0.0f) {
    PhysicsSettings settings;
    settings.hasGroundPlane = true;
    settings.groundPlaneY = height;
    registry.ctx().insert_or_assign<PhysicsSettings>(std::move(settings));
}

static entt::entity makeBox(entt::registry& registry, const glm::vec3& position,
                            float mass = 1.0f, const glm::vec3& scale = glm::vec3(1.0f)) {
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = position;
    transform.scale = scale;
    auto& body = registry.emplace<RigidBodyComponent>(entity);
    body.mass = mass;
    registry.emplace<BoxColliderComponent>(entity);
    return entity;
}

// A collider with no rigid body: an immovable obstacle, which is how level
// geometry takes part without being simulated.
static entt::entity makeStaticBox(entt::registry& registry, const glm::vec3& position,
                                  const glm::vec3& scale = glm::vec3(1.0f)) {
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = position;
    transform.scale = scale;
    registry.emplace<BoxColliderComponent>(entity);
    return entity;
}

static entt::entity makeCapsule(entt::registry& registry, const glm::vec3& position,
                               float radius = 0.4f, float height = 2.0f, float mass = 1.0f) {
    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity, position);
    registry.emplace<RigidBodyComponent>(entity).mass = mass;
    auto& collider = registry.emplace<CapsuleColliderComponent>(entity);
    collider.radius = radius;
    collider.height = height;
    return entity;
}

static entt::entity makeSphere(entt::registry& registry, const glm::vec3& position,
                               float radius = 0.5f, float mass = 1.0f) {
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = position;
    auto& body = registry.emplace<RigidBodyComponent>(entity);
    body.mass = mass;
    auto& collider = registry.emplace<SphereColliderComponent>(entity);
    collider.radius = radius;
    return entity;
}

static PhysicsSystem::Proxy makeProxy(uint32_t id, size_t index,
                                      const glm::vec3& min, const glm::vec3& max,
                                      float inverseMass) {
    PhysicsSystem::Proxy proxy;
    proxy.entity = static_cast<entt::entity>(id);
    proxy.index = index;
    proxy.min = min;
    proxy.max = max;
    proxy.inverseMass = inverseMass;
    return proxy;
}

// ---- Terrain ---------------------------------------------------------------
//
// The README named this gap for a long time: the procedurally generated terrain
// was scenery you fell through. Heightfield's own suite proves the geometry;
// what these prove is that the solver and the queries can reach it - a surface
// that is solid to the narrowphase and invisible to IsGrounded is worse than
// one you fall through, because a character controller then reports that it is
// standing on ground while it drops past a mountain.

static entt::entity makeTerrain(entt::registry& registry,
                                const glm::vec3& position = glm::vec3(0.0f),
                                float scale = 1.0f) {
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = position;
    transform.scale = glm::vec3(scale);
    registry.emplace<HeightfieldColliderComponent>(entity);
    return entity;
}

// The surface the collider describes, asked of the same grid the solver uses.
// Not the analytic sine: the collider is two flat triangles per cell, and
// inside a cell those differ from the smooth surface by a real amount.
static float terrainHeightAt(entt::registry& registry, entt::entity terrain,
                             float worldX, float worldZ) {
    const auto& collider = registry.get<HeightfieldColliderComponent>(terrain);
    const auto& transform = registry.get<TransformComponent>(terrain);
    const Heightfield* field = HeightfieldCache::For(registry).Get(collider);
    if (!field) return 0.0f;

    const float scale = transform.scale.x;
    float height = 0.0f;
    if (!field->HeightAt((worldX - transform.position.x) / scale,
                         (worldZ - transform.position.z) / scale, height)) {
        return 0.0f;
    }
    return transform.position.y + height * scale;
}

// The bottom of a bowl in the standard terrain, which is where a ball can
// actually come to rest.
//
// h = 0.6 * (sin(0.2x) + cos(0.2z)), so both terms are at their minimum when
// 0.2x = -pi/2 and 0.2z = pi. Anywhere else on this surface is a slope, and a
// ball on a slope is still moving when the test looks at it - which is correct
// behaviour and useless as an expected value.
static constexpr float kBasinX = -7.853982f;
static constexpr float kBasinZ = 15.707963f;

static void testASphereIsTheSameSizeWhicheverWayItIsTurned() {
    // Found by the terrain tests, and nothing to do with terrain: a sphere is
    // rotation-invariant and the solver did not agree. Its radius came from the
    // world bounding box, whose half extent for a rotated transform is the sum
    // of the three axes' contributions - 1.41 times the radius at 45 degrees
    // about one axis, up to 1.73 in general.
    //
    // Nothing caught it because until something rolled, nothing rotated: a ball
    // dropped straight onto a box lands with its orientation still exactly
    // identity. Put one on a hill and it rolls, its orientation changes every
    // step, and the ball grows as it goes - hovering a little higher above the
    // ground each step, without limit.
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f), glm::vec3(10.0f, 1.0f, 10.0f));

    const auto upright = makeSphere(registry, glm::vec3(-2.0f, 3.0f, 0.0f), 0.5f);
    const auto turned = makeSphere(registry, glm::vec3(2.0f, 3.0f, 0.0f), 0.5f);
    registry.get<TransformComponent>(turned).rotation = glm::vec3(0.7f, 0.5f, 0.9f);

    stepFor(registry, 3.0f);

    const float restA = registry.get<TransformComponent>(upright).position.y;
    const float restB = registry.get<TransformComponent>(turned).position.y;
    CHECK_MSG(test::nearly(restA, 1.0f, 0.02f), "the upright ball rests on top of the platform");
    CHECK_MSG(test::nearly(restA, restB, 1e-3f), "and turning one must not change its size");

    // The QUERIES have to agree, and they are a separate code path that the
    // check above cannot reach: gatherShapes builds its own shape list, and it
    // read the same bounding box. A ray fired at each ball from the same height
    // has to travel the same distance.
    const auto hitA = PhysicsSystem::Raycast(registry, glm::vec3(-2.0f, 5.0f, 0.0f),
                                             glm::vec3(0.0f, -1.0f, 0.0f), 10.0f);
    const auto hitB = PhysicsSystem::Raycast(registry, glm::vec3(2.0f, 5.0f, 0.0f),
                                             glm::vec3(0.0f, -1.0f, 0.0f), 10.0f);
    CHECK(hitA.hit && hitB.hit);
    CHECK_MSG(hitA.entity == upright && hitB.entity == turned,
              "each ray has to reach its own ball before the platform under it");
    CHECK_MSG(test::nearly(hitA.distance, hitB.distance, 2e-3f),
              "and a query must not see a turned ball as a bigger one");

    // Same for the overlap query, which reads shape.radius directly.
    std::vector<entt::entity> nearUpright;
    std::vector<entt::entity> nearTurned;
    PhysicsSystem::OverlapSphere(registry, glm::vec3(-2.0f, restA + 0.62f, 0.0f), 0.1f, nearUpright);
    PhysicsSystem::OverlapSphere(registry, glm::vec3(2.0f, restB + 0.62f, 0.0f), 0.1f, nearTurned);
    CHECK_MSG(nearUpright.empty(),
              "a probe above the upright ball touches nothing, since 0.62 is past its radius");
    CHECK_MSG(nearTurned.empty(), "and the turned ball is exactly the same size");
}

static void testABallLandsOnTheTerrainInsteadOfFallingThroughIt() {
    entt::registry registry;
    const auto terrain = makeTerrain(registry);

    const float x = kBasinX;
    const float z = kBasinZ;
    const float surface = terrainHeightAt(registry, terrain, x, z);

    const auto ball = makeSphere(registry, glm::vec3(x, surface + 2.0f, z), 0.5f);
    stepFor(registry, 6.0f);

    const auto& transform = registry.get<TransformComponent>(ball);
    CHECK_MSG(transform.position.y > surface - 0.5f,
              "the ball must end up on the terrain, not somewhere under it");

    // Measured where it ACTUALLY IS: even in a bowl it rolls a little on the
    // way down, and the invariant being tested is that it is on the ground,
    // not that it is on one particular square metre of it.
    const float under = terrainHeightAt(registry, terrain, transform.position.x,
                                        transform.position.z);
    // kSlop is the overlap the solver deliberately leaves; anything more than a
    // few times that is resting on the wrong thing.
    CHECK_MSG(test::nearly(transform.position.y, under + 0.5f, 0.05f),
              "and resting exactly one radius above the surface beneath it");
    CHECK_MSG(glm::length(registry.get<RigidBodyComponent>(ball).velocity) < 0.5f,
              "and it has come to rest rather than still sliding through");
}

static void testABallOnASlopeRollsDownhill() {
    // The reason the contact normal has to be the hill's rather than +Y, and
    // the failure the SAT narrowphase was written to fix in the shape that
    // replaced it: a ball held up along +Y rests in the air on a slope and
    // never moves.
    entt::registry registry;
    const auto terrain = makeTerrain(registry);

    // dh/dx = 0.12 * cos(0.2x), which at x = 2.5 is +0.105: the ground climbs
    // towards +x, so downhill is -x. dh/dz there is small enough not to decide
    // anything.
    const float x = 2.5f;
    const float z = -3.5f;
    const float surface = terrainHeightAt(registry, terrain, x, z);

    // Placed ON the surface rather than dropped, so this is about the normal
    // rather than about a bounce.
    const auto ball = makeSphere(registry, glm::vec3(x, surface + 0.5f, z), 0.5f);
    stepFor(registry, 1.0f);

    const auto& transform = registry.get<TransformComponent>(ball);
    CHECK_MSG(transform.position.x < x - 0.1f, "a ball on a slope rolls down it");
    CHECK_MSG(transform.position.y < surface + 0.5f,
              "and downhill is also downwards");
}

static void testWithoutTheColliderItStillFallsThrough() {
    // The control, and the whole of what the README described. Without it the
    // test above could pass because the ball stopped for some other reason -
    // the world ground plane, a sleep threshold, an integrator that ran out of
    // steps - rather than because the terrain is solid.
    entt::registry registry;
    const auto terrain = makeTerrain(registry);
    registry.remove<HeightfieldColliderComponent>(terrain);
    registry.emplace<TagComponent>(terrain, "scenery");

    // Something else with a collider, so the step does not return early for
    // having fewer than two bodies.
    makeStaticBox(registry, glm::vec3(50.0f, 0.0f, 50.0f));

    const auto ball = makeSphere(registry, glm::vec3(2.5f, 6.0f, -3.5f), 0.5f);
    stepFor(registry, 3.0f);

    CHECK_MSG(registry.get<TransformComponent>(ball).position.y < -10.0f,
              "with no collider the terrain is scenery and the ball goes through it");
}

static void testTerrainThatHasBeenMovedAndScaledStillHoldsThings() {
    // The frame conversion, which is the part of the wiring that has nothing to
    // do with the geometry: the shape goes into the grid's space, the contact
    // comes back out of it, and a missed scale is a surface at the right shape
    // and the wrong size.
    entt::registry registry;
    const auto terrain = makeTerrain(registry, glm::vec3(10.0f, 3.0f, -4.0f), 2.0f);

    // The same bowl, in the terrain's own coordinates, taken out to where the
    // transform has put it.
    const float x = 10.0f + 2.0f * kBasinX;
    const float z = -4.0f + 2.0f * kBasinZ;
    const float surface = terrainHeightAt(registry, terrain, x, z);
    CHECK_MSG(surface > 3.0f - 2.5f && surface < 3.0f,
              "the moved terrain has to be where the transform put it");

    const auto ball = makeSphere(registry, glm::vec3(x, surface + 2.0f, z), 0.5f);
    stepFor(registry, 6.0f);

    const auto& resting = registry.get<TransformComponent>(ball);
    const float under = terrainHeightAt(registry, terrain, resting.position.x, resting.position.z);
    CHECK_MSG(test::nearly(resting.position.y, under + 0.5f, 0.06f),
              "a terrain moved and scaled still holds a ball one radius above its surface");
    CHECK_MSG(resting.position.y > 0.5f,
              "and it is up where the transform put the terrain, not down at the origin");
}

static void testABuriedBodyIsPushedBackOutOfTheGround() {
    // Placed inside the hill, which is what a spawn point, a gizmo drag or a
    // script does sooner or later. With a one-sided surface there is no way out
    // and the body stays there forever.
    entt::registry registry;
    const auto terrain = makeTerrain(registry);

    const float x = -1.5f;
    const float z = 4.5f;
    const float surface = terrainHeightAt(registry, terrain, x, z);

    const auto ball = makeSphere(registry, glm::vec3(x, surface - 1.0f, z), 0.5f);
    registry.get<RigidBodyComponent>(ball).useGravity = false;
    stepFor(registry, 2.0f);

    CHECK_MSG(registry.get<TransformComponent>(ball).position.y > surface,
              "a body under the terrain has to come back up through the top of it");
}

static void testTheGroundCheckSeesTerrain() {
    entt::registry registry;
    const auto terrain = makeTerrain(registry);

    const float x = 5.5f;
    const float z = 5.5f;
    const float surface = terrainHeightAt(registry, terrain, x, z);

    CHECK_MSG(PhysicsSystem::IsGrounded(registry, glm::vec3(x, surface + 0.05f, z), 0.15f),
              "standing on the terrain is standing on the ground");
    CHECK_MSG(!PhysicsSystem::IsGrounded(registry, glm::vec3(x, surface + 5.0f, z), 0.15f),
              "and five units above it is not");
}

static void testARayStopsAtTheSurfaceAndNotAtTheBoundingBox() {
    // THE query that tells a real heightfield from one answered with its
    // bounds. The terrain's box reaches its highest hill everywhere, so a
    // bounding-box answer stops the ray at that height above every valley -
    // which is a character standing on thin air over the whole map.
    entt::registry registry;
    const auto terrain = makeTerrain(registry);

    // A low point: sin(0.2x) and cos(0.2z) both near their minimum.
    const float x = -7.5f;
    const float z = 15.5f;
    const float surface = terrainHeightAt(registry, terrain, x, z);

    const float from = 20.0f;
    const auto hit = PhysicsSystem::Raycast(registry, glm::vec3(x, from, z),
                                            glm::vec3(0.0f, -1.0f, 0.0f), 100.0f);
    CHECK(hit.hit);
    CHECK_MSG(hit.entity == terrain, "and it must be the terrain it hit");
    CHECK_MSG(test::nearly(hit.distance, from - surface, 0.01f),
              "the ray stops at the surface");
    CHECK_MSG(test::nearly(hit.point.y, surface, 0.01f), "at the surface's height");

    // The bounding box would have stopped it at the tallest hill in the field,
    // which is a different answer - and this is the check that says so rather
    // than assuming it.
    const float tallest = TerrainGenerator::kPrimitiveHeightScale * 2.0f;
    CHECK_MSG(tallest - surface > 0.5f, "the valley has to be well below the peaks");
    CHECK_MSG(hit.distance > from - tallest + 0.5f, "and the ray must reach past them");

    // Nothing below the ray at all, off the side of the grid.
    const auto miss = PhysicsSystem::Raycast(registry, glm::vec3(500.0f, from, z),
                                             glm::vec3(0.0f, -1.0f, 0.0f), 100.0f);
    CHECK(!miss.hit);
}

static void testOverlapSphereSeesTerrain() {
    entt::registry registry;
    const auto terrain = makeTerrain(registry);
    const float surface = terrainHeightAt(registry, terrain, 1.5f, 1.5f);

    std::vector<entt::entity> touching;
    PhysicsSystem::OverlapSphere(registry, glm::vec3(1.5f, surface + 0.2f, 1.5f), 0.5f, touching);
    CHECK_MSG(touching.size() == 1 && touching[0] == terrain,
              "a sphere resting on the ground overlaps it");

    touching.clear();
    PhysicsSystem::OverlapSphere(registry, glm::vec3(1.5f, surface + 20.0f, 1.5f), 0.5f, touching);
    CHECK_MSG(touching.empty(), "and one twenty units up does not");
}

static void testTerrainNeverMovesEvenWithARigidBodyOnIt() {
    // A RigidBodyComponent on terrain would otherwise make the ground fall, and
    // nothing about that reads as a mistake in an inspector.
    entt::registry registry;
    const auto terrain = makeTerrain(registry);
    registry.emplace<RigidBodyComponent>(terrain).mass = 1.0f;

    makeSphere(registry, glm::vec3(0.5f, 4.0f, 0.5f), 0.5f);
    const glm::vec3 before = registry.get<TransformComponent>(terrain).position;
    stepFor(registry, 1.0f);
    const glm::vec3 after = registry.get<TransformComponent>(terrain).position;

    // The rigid body still integrates - it is a body with gravity - but nothing
    // the collider does may push it, and nothing may rest ON a surface that is
    // being pushed around by what is resting on it.
    CHECK_MSG(after.x == before.x && after.z == before.z,
              "terrain must not be shoved sideways by whatever lands on it");
}

static void testSweepAndPruneFindsOverlappingPairs() {
    std::vector<PhysicsSystem::Proxy> proxies;
    proxies.push_back(makeProxy(1, 0, glm::vec3(0.0f), glm::vec3(1.0f), 1.0f));
    proxies.push_back(makeProxy(2, 1, glm::vec3(0.5f), glm::vec3(1.5f), 1.0f));
    proxies.push_back(makeProxy(3, 2, glm::vec3(50.0f), glm::vec3(51.0f), 1.0f));

    std::vector<std::pair<size_t, size_t>> pairs;
    PhysicsSystem::SweepAndPrune(proxies, pairs);
    CHECK_EQ(pairs.size(), size_t{1});
}

static void testSweepAndPruneSkipsPairsSeparatedOffAxis() {
    // Overlapping on the sweep axis but disjoint on Y. Testing X alone is not
    // enough, and that is the classic sort-and-sweep mistake.
    std::vector<PhysicsSystem::Proxy> proxies;
    proxies.push_back(makeProxy(1, 0, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), 1.0f));
    proxies.push_back(makeProxy(2, 1, glm::vec3(0.5f, 9.0f, 0.0f), glm::vec3(1.5f, 10.0f, 1.0f), 1.0f));

    std::vector<std::pair<size_t, size_t>> pairs;
    PhysicsSystem::SweepAndPrune(proxies, pairs);
    CHECK_MSG(pairs.empty(), "boxes apart on Y must not be reported as candidates");
}

static void testSweepAndPruneIgnoresTwoStatics() {
    std::vector<PhysicsSystem::Proxy> proxies;
    proxies.push_back(makeProxy(1, 0, glm::vec3(0.0f), glm::vec3(1.0f), 0.0f));
    proxies.push_back(makeProxy(2, 1, glm::vec3(0.5f), glm::vec3(1.5f), 0.0f));

    std::vector<std::pair<size_t, size_t>> pairs;
    PhysicsSystem::SweepAndPrune(proxies, pairs);
    CHECK_MSG(pairs.empty(), "two immovable colliders have nothing to resolve");
}

static void testBoxesSeparateInsteadOfOverlapping() {
    entt::registry registry;
    const auto a = makeBox(registry, glm::vec3(0.0f, 5.0f, 0.0f));
    const auto b = makeBox(registry, glm::vec3(0.5f, 5.0f, 0.0f));

    stepFor(registry, 0.5f);

    const float separation = std::fabs(registry.get<TransformComponent>(a).position.x -
                                       registry.get<TransformComponent>(b).position.x);
    CHECK_MSG(separation > 0.5f, "overlapping boxes must be pushed apart, not left interpenetrating");
}

static void testBodyRestsOnStaticPlatformNotThroughIt() {
    entt::registry registry;
    // Platform top surface sits at y = 1.15.
    makeStaticBox(registry, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(4.0f, 0.3f, 4.0f));
    const auto falling = makeBox(registry, glm::vec3(0.0f, 5.0f, 0.0f));

    stepFor(registry, 4.0f);

    const float y = registry.get<TransformComponent>(falling).position.y;
    CHECK_MSG(y > 1.0f, "a body must land on the platform, not fall through to the ground plane");
    CHECK_MSG(y < 2.2f, "and must settle on it rather than being flung upward");
}

static void testStaticColliderDoesNotMove() {
    entt::registry registry;
    const auto platform = makeStaticBox(registry, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(4.0f, 0.3f, 4.0f));
    makeBox(registry, glm::vec3(0.0f, 3.0f, 0.0f), 50.0f);

    stepFor(registry, 2.0f);

    const auto& transform = registry.get<TransformComponent>(platform);
    CHECK_NEAR(transform.position.y, 1.0f);
    CHECK_NEAR(transform.position.x, 0.0f);
}

static void testHeavierBodyMovesLessThanLighterOne() {
    // mass was previously stored and never read, so both bodies would have been
    // displaced identically whatever their masses.
    entt::registry registry;
    const auto light = makeBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), 1.0f);
    const auto heavy = makeBox(registry, glm::vec3(0.6f, 5.0f, 0.0f), 100.0f);

    const float lightStart = registry.get<TransformComponent>(light).position.x;
    const float heavyStart = registry.get<TransformComponent>(heavy).position.x;

    PhysicsSystem::Update(registry, 1.0f / 60.0f);

    const float lightMoved = std::fabs(registry.get<TransformComponent>(light).position.x - lightStart);
    const float heavyMoved = std::fabs(registry.get<TransformComponent>(heavy).position.x - heavyStart);

    CHECK_MSG(lightMoved > heavyMoved, "the lighter body must absorb more of the separation");
    CHECK_MSG(heavyMoved < lightMoved * 0.2f, "and a 100:1 mass ratio must show clearly");
}

static void testSpheresSeparate() {
    entt::registry registry;
    const auto a = makeSphere(registry, glm::vec3(0.0f, 5.0f, 0.0f), 0.5f);
    const auto b = makeSphere(registry, glm::vec3(0.4f, 5.0f, 0.0f), 0.5f);

    stepFor(registry, 0.5f);

    const glm::vec3 delta = registry.get<TransformComponent>(b).position -
                            registry.get<TransformComponent>(a).position;
    CHECK_MSG(glm::length(delta) > 0.5f, "overlapping spheres must be pushed apart");
}

static void testSphereRestsOnBox() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(4.0f, 0.4f, 4.0f));
    const auto ball = makeSphere(registry, glm::vec3(0.0f, 5.0f, 0.0f), 0.5f);

    stepFor(registry, 4.0f);

    const float y = registry.get<TransformComponent>(ball).position.y;
    CHECK_MSG(y > 1.1f, "a sphere must rest on top of a box, not sink into it");
    CHECK_MSG(y < 2.2f, "and not be ejected");
}

static void testTriggerReportsButDoesNotResolve() {
    entt::registry registry;
    const auto trigger = registry.create();
    auto& triggerTransform = registry.emplace<TransformComponent>(trigger);
    triggerTransform.position = glm::vec3(0.0f, 5.0f, 0.0f);
    auto& triggerCollider = registry.emplace<BoxColliderComponent>(trigger);
    triggerCollider.isTrigger = true;

    const auto body = makeBox(registry, glm::vec3(0.1f, 5.0f, 0.0f));
    const float startX = registry.get<TransformComponent>(body).position.x;

    std::vector<PhysicsSystem::Contact> contacts;
    PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);

    CHECK_MSG(!contacts.empty(), "an overlap with a trigger must still be reported");
    bool sawTrigger = false;
    for (const auto& contact : contacts) {
        if (contact.isTrigger) sawTrigger = true;
    }
    CHECK_MSG(sawTrigger, "and must be flagged as a trigger contact");
    CHECK_NEAR(registry.get<TransformComponent>(body).position.x, startX);
}

static void testContactsAreClearedEachStep() {
    entt::registry registry;
    makeBox(registry, glm::vec3(0.0f, 5.0f, 0.0f));
    makeBox(registry, glm::vec3(0.4f, 5.0f, 0.0f));

    std::vector<PhysicsSystem::Contact> contacts;
    PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);
    const size_t first = contacts.size();
    CHECK_MSG(first > 0, "the overlap must be reported");

    PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);
    CHECK_MSG(contacts.size() <= first, "the list must be cleared per step, not appended to");
}

static void testKinematicBodyIsNotPushed() {
    entt::registry registry;
    const auto kinematic = makeBox(registry, glm::vec3(0.0f, 5.0f, 0.0f));
    registry.get<RigidBodyComponent>(kinematic).isKinematic = true;
    makeBox(registry, glm::vec3(0.5f, 5.0f, 0.0f));

    stepFor(registry, 0.5f);

    const auto& transform = registry.get<TransformComponent>(kinematic);
    CHECK_NEAR(transform.position.x, 0.0f);
    CHECK_NEAR(transform.position.y, 5.0f);
}

static void testDistantBodiesNeverContact() {
    entt::registry registry;
    makeBox(registry, glm::vec3(-40.0f, 5.0f, 0.0f));
    makeBox(registry, glm::vec3(40.0f, 5.0f, 0.0f));

    std::vector<PhysicsSystem::Contact> contacts;
    PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);
    CHECK_MSG(contacts.empty(), "bodies 80 units apart must not generate a contact");
}

static void testParentedBodyFallsInWorldSpace() {
    // The parent is turned 90 degrees about Y. Velocity is world space but
    // position is relative to the parent, so applying the fall to the local
    // position without converting sends the child sideways instead of down.
    entt::registry registry;

    const auto parent = registry.create();
    auto& parentTransform = registry.emplace<TransformComponent>(parent);
    parentTransform.position = glm::vec3(5.0f, 10.0f, 0.0f);
    parentTransform.rotation = glm::vec3(0.0f, glm::radians(90.0f), 0.0f);

    const auto child = registry.create();
    registry.emplace<TransformComponent>(child);
    registry.emplace<HierarchyComponent>(child, parent);
    registry.emplace<RigidBodyComponent>(child);
    registry.emplace<BoxColliderComponent>(child);

    TransformSystem::UpdateWorldTransforms(registry);
    const glm::vec3 startWorld = glm::vec3(registry.get<WorldTransformComponent>(child).matrix[3]);

    for (int i = 0; i < 30; ++i) {
        PhysicsSystem::Update(registry, 1.0f / 60.0f);
        TransformSystem::UpdateWorldTransforms(registry);
    }

    const glm::vec3 endWorld = glm::vec3(registry.get<WorldTransformComponent>(child).matrix[3]);

    CHECK_MSG(endWorld.y < startWorld.y - 0.05f, "a parented body must fall in world space");
    CHECK_NEAR(endWorld.x, startWorld.x);
    CHECK_NEAR(endWorld.z, startWorld.z);
}

static void testGravityAccelerates() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 10.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(entity);

    PhysicsSystem::Update(registry, 1.0f / 60.0f);

    const auto& body = registry.get<RigidBodyComponent>(entity);
    CHECK_MSG(body.velocity.y < 0.0f, "gravity must produce downward velocity");
    CHECK_MSG(transform.position.y < 10.0f, "the body must have fallen");
}

static void testKinematicBodiesDoNotMove() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 10.0f, 0.0f);
    auto& body = registry.emplace<RigidBodyComponent>(entity);
    body.isKinematic = true;

    stepFor(registry, 1.0f);
    CHECK_NEAR(transform.position.y, 10.0f);
}

static void testRestsOnColliderBottomNotOrigin() {
    // A unit cube scaled to 0.7 used to come to rest with its CENTRE at y = 0,
    // which buries it half-way through the floor it is supposedly resting on.
    entt::registry registry;
    enableGroundPlane(registry);
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 4.0f, 0.0f);
    transform.scale = glm::vec3(0.7f);
    registry.emplace<RigidBodyComponent>(entity);
    registry.emplace<BoxColliderComponent>(entity);

    stepFor(registry, 6.0f);

    const float expectedRestY = 0.5f * 0.7f; // half height above the plane
    CHECK_NEAR(transform.position.y, expectedRestY);
    CHECK_MSG(transform.position.y > 0.0f, "the body must rest ON the plane, not inside it");
}

static void testComesToRest() {
    entt::registry registry;
    enableGroundPlane(registry);
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 5.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(entity);
    registry.emplace<BoxColliderComponent>(entity);

    stepFor(registry, 10.0f);

    const auto& body = registry.get<RigidBodyComponent>(entity);
    CHECK_NEAR(body.velocity.y, 0.0f);
}

static void testRisingBodyIsNotReflected() {
    // Velocity used to be inverted on every frame the body was below the plane,
    // including frames where it was already moving upward, which re-launched
    // resting bodies.
    entt::registry registry;
    enableGroundPlane(registry);
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 0.1f, 0.0f);
    auto& body = registry.emplace<RigidBodyComponent>(entity);
    body.useGravity = false;
    body.velocity = glm::vec3(0.0f, 5.0f, 0.0f);
    registry.emplace<BoxColliderComponent>(entity);

    PhysicsSystem::Update(registry, 1.0f / 60.0f);

    CHECK_MSG(registry.get<RigidBodyComponent>(entity).velocity.y > 0.0f,
              "an upward-moving body must not have its velocity inverted");
}

static void testColliderSizeAffectsRestHeight() {
    entt::registry registry;
    enableGroundPlane(registry);
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 6.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(entity);
    auto& box = registry.emplace<BoxColliderComponent>(entity);
    box.size = glm::vec3(1.0f, 4.0f, 1.0f);

    stepFor(registry, 8.0f);
    CHECK_NEAR(transform.position.y, 2.0f);
}

static void testZeroAndNegativeDeltaAreIgnored() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 10.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(entity);

    PhysicsSystem::Update(registry, 0.0f);
    PhysicsSystem::Update(registry, -1.0f);

    CHECK_NEAR(transform.position.y, 10.0f);
}

static void testFixedStepKeepsABodyOnTheGround() {
    // Supersonic::Run feeds this a fixed 1/60 step. Handing the integrator a raw
    // two-second delta (a title-bar drag on Win32) moved a body 39 units in one
    // frame, straight through the floor.
    //
    // This was called testFixedStepDoesNotTunnel, which it was not: it creates
    // ONE entity, so Update returns before the narrowphase runs at all, and then
    // asserts y >= 0 - which the world ground clamp guarantees at any speed
    // whatsoever. It tested the integrator and the clamp, which is worth
    // testing, under a name that claimed something it never checked. The real
    // tunnelling test is below.
    entt::registry registry;
    enableGroundPlane(registry);
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 2.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(entity);
    registry.emplace<BoxColliderComponent>(entity);

    stepFor(registry, 2.0f);

    CHECK_MSG(transform.position.y >= 0.0f, "a body must never end up below the ground plane");
    CHECK_MSG(transform.position.y < 3.0f, "and must not be launched into the air");
}

static void testAFastProjectileDoesNotPassThroughAThinWall() {
    // The arithmetic that made this necessary: with discrete overlap testing at
    // a 1/60 step, two bodies are only ever seen touching if they overlap on
    // some frame, which needs v <= 120 * (halfA + halfB). A 0.1-radius
    // projectile against a 0.2-thick wall is 24 m/s - slower than a thrown
    // baseball. Above that the projectile is on one side at frame N and the
    // other at frame N+1, and nothing in between ever happened.
    entt::registry registry;

    // A thin static wall in the x = 0 plane, well above the ground plane so the
    // world clamp cannot be what stops anything.
    const auto wall = registry.create();
    auto& wallTransform = registry.emplace<TransformComponent>(wall);
    wallTransform.position = glm::vec3(0.0f, 10.0f, 0.0f);
    wallTransform.scale = glm::vec3(0.2f, 8.0f, 8.0f);
    registry.emplace<BoxColliderComponent>(wall);

    // A small, fast projectile aimed straight at it. Gravity is irrelevant over
    // the few frames this takes; what matters is the 40 m/s, comfortably past
    // the 24 m/s the discrete test could cope with.
    const auto bullet = registry.create();
    auto& bulletTransform = registry.emplace<TransformComponent>(bullet);
    bulletTransform.position = glm::vec3(-2.0f, 10.0f, 0.0f);
    auto& bulletBody = registry.emplace<RigidBodyComponent>(bullet);
    bulletBody.velocity = glm::vec3(40.0f, 0.0f, 0.0f);
    bulletBody.useGravity = false;
    bulletBody.restitution = 0.0f;
    auto& bulletCollider = registry.emplace<SphereColliderComponent>(bullet);
    bulletCollider.radius = 0.1f;

    // Long enough to cross the wall several times over if nothing stops it.
    for (int i = 0; i < 30; ++i) {
        PhysicsSystem::Update(registry, 1.0f / 60.0f);
    }

    const float x = registry.get<TransformComponent>(bullet).position.x;
    CHECK_MSG(x < 0.0f,
              "a 40 m/s projectile must not end up on the far side of a thin wall: x = " +
                  std::to_string(x));

    // And it must actually have been stopped rather than never having moved -
    // a test that passes because the body sat still is not testing anything.
    //
    // Not "it moved forward": the wall carries no RigidBodyComponent, so it
    // takes the default restitution, and the projectile bounces off it at
    // around 8 m/s. The first version of this test asserted x > -2 and failed
    // at -6.27, which was the physics being right and the assertion being
    // wrong. What matters is that the velocity changed - the projectile met
    // something - and that it is on the near side.
    const glm::vec3 finalVelocity = registry.get<RigidBodyComponent>(bullet).velocity;
    CHECK_MSG(finalVelocity.x < 39.0f,
              "the projectile must have been stopped or deflected, not sailed through: vx = " +
                  std::to_string(finalVelocity.x));
}


// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

static void testRaycastHitsTheNearestCollider() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -5.0f));
    makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -10.0f));

    const auto hit = PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 0.0f, 0.0f),
                                            glm::vec3(0.0f, 0.0f, -1.0f), 100.0f);
    CHECK_MSG(hit.hit, "the ray must find something");
    // Near face of the closer box: centre -5, half extent 0.5.
    CHECK_NEAR(hit.distance, 4.5f);
    CHECK_MSG(hit.normal.z > 0.9f, "the normal must face back along the ray");
}

static void testRaycastMissesAndRespectsRange() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -5.0f));

    CHECK_MSG(!PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 20.0f, 0.0f),
                                      glm::vec3(0.0f, 0.0f, -1.0f), 100.0f).hit,
              "a ray that passes overhead must miss");

    CHECK_MSG(!PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 0.0f, 0.0f),
                                      glm::vec3(0.0f, 0.0f, -1.0f), 2.0f).hit,
              "and a range shorter than the distance must not reach it");

    CHECK_MSG(!PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 0.0f, 0.0f),
                                      glm::vec3(0.0f, 0.0f, 1.0f), 100.0f).hit,
              "nor may a ray pointing the other way");
}

static void testRaycastIgnoresTheCasterAndTriggers() {
    entt::registry registry;
    const auto self = makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -2.0f));

    const auto trigger = registry.create();
    auto& triggerTransform = registry.emplace<TransformComponent>(trigger);
    triggerTransform.position = glm::vec3(0.0f, 0.0f, -6.0f);
    registry.emplace<BoxColliderComponent>(trigger).isTrigger = true;

    makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -9.0f));

    // Ignoring the caster is what stops a character's own collider from
    // absorbing every shot it fires.
    const auto hit = PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 0.0f, 0.0f),
                                            glm::vec3(0.0f, 0.0f, -1.0f), 100.0f, self);
    CHECK_MSG(hit.hit, "something must still be hit");
    CHECK_NEAR(hit.distance, 8.5f);   // the solid box, not the trigger at 5.5

    const auto withTriggers = PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 0.0f, 0.0f),
                                                     glm::vec3(0.0f, 0.0f, -1.0f), 100.0f,
                                                     self, /*includeTriggers=*/true);
    CHECK_NEAR(withTriggers.distance, 5.5f);
}

static void testRaycastHitsSpheres() {
    entt::registry registry;
    const auto ball = registry.create();
    auto& transform = registry.emplace<TransformComponent>(ball);
    transform.position = glm::vec3(0.0f, 0.0f, -4.0f);
    registry.emplace<SphereColliderComponent>(ball).radius = 1.0f;

    const auto hit = PhysicsSystem::Raycast(registry, glm::vec3(0.0f, 0.0f, 0.0f),
                                            glm::vec3(0.0f, 0.0f, -1.0f), 100.0f);
    CHECK_MSG(hit.hit, "a sphere collider must be hittable");
    CHECK_NEAR(hit.distance, 3.0f);
    CHECK_MSG(hit.entity == ball, "and report which entity was hit");
}

static void testRaycastRejectsDegenerateInput() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -5.0f));

    // A zero direction would normalise to NaN and report an impossible hit.
    CHECK_MSG(!PhysicsSystem::Raycast(registry, glm::vec3(0.0f), glm::vec3(0.0f), 100.0f).hit,
              "a zero-length direction must simply miss");
    CHECK_MSG(!PhysicsSystem::Raycast(registry, glm::vec3(0.0f),
                                      glm::vec3(0.0f, 0.0f, -1.0f), -1.0f).hit,
              "and a negative range must too");
}

static void testRaycastDirectionNeedNotBeNormalised() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 0.0f, -5.0f));

    const auto hit = PhysicsSystem::Raycast(registry, glm::vec3(0.0f),
                                            glm::vec3(0.0f, 0.0f, -7.3f), 100.0f);
    CHECK_MSG(hit.hit, "an unnormalised direction must work");
    CHECK_NEAR(hit.distance, 4.5f);
}

static void testOverlapSphereFindsWhatItTouches() {
    entt::registry registry;
    const auto near = makeStaticBox(registry, glm::vec3(1.0f, 0.0f, 0.0f));
    makeStaticBox(registry, glm::vec3(30.0f, 0.0f, 0.0f));

    std::vector<entt::entity> found;
    PhysicsSystem::OverlapSphere(registry, glm::vec3(0.0f), 2.0f, found);

    CHECK_EQ(found.size(), size_t{1});
    if (!found.empty()) CHECK_MSG(found[0] == near, "the near box, not the far one");

    // Appends rather than clears, so several queries can accumulate.
    PhysicsSystem::OverlapSphere(registry, glm::vec3(0.0f), 2.0f, found);
    CHECK_EQ(found.size(), size_t{2});
}

static void testGroundCheck() {
    entt::registry registry;
    // A platform whose top surface is at y = 1.15.
    makeStaticBox(registry, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(4.0f, 0.3f, 4.0f));

    CHECK_MSG(PhysicsSystem::IsGrounded(registry, glm::vec3(0.0f, 1.2f, 0.0f), 0.2f),
              "standing just above the platform is grounded");
    CHECK_MSG(!PhysicsSystem::IsGrounded(registry, glm::vec3(0.0f, 6.0f, 0.0f), 0.2f),
              "six units up is not");

    // The world ground plane counts even though no entity represents it - but
    // only when the scene has one. This check lives two hundred lines from the
    // clamp it has to agree with, which is exactly the kind of pair that drifts.
    entt::registry empty;
    CHECK_MSG(!PhysicsSystem::IsGrounded(empty, glm::vec3(0.0f, 0.05f, 0.0f), 0.2f),
              "an empty scene has no ground to stand on");

    enableGroundPlane(empty);
    CHECK_MSG(PhysicsSystem::IsGrounded(empty, glm::vec3(0.0f, 0.05f, 0.0f), 0.2f),
              "the world plane counts as ground once the scene asks for one");
    CHECK_MSG(!PhysicsSystem::IsGrounded(empty, glm::vec3(0.0f, 6.0f, 0.0f), 0.2f),
              "and only near it");
}

// --- materials --------------------------------------------------------------
//
// Bounce and grip used to be two constants for the whole world, so a rubber
// ball and a wooden crate behaved identically. That is not a tuning problem;
// it is the absence of the setting.

static void testRestitutionTakesTheBouncierSurface() {
    // A superball dropped on concrete bounces. Taking the smaller - or the
    // average - would mean any dead surface killed every ball that touched it.
    CHECK_NEAR(PhysicsSystem::CombineRestitution(0.9f, 0.0f), 0.9f);
    CHECK_NEAR(PhysicsSystem::CombineRestitution(0.2f, 0.5f), 0.5f);
}

static void testRestitutionCannotGainEnergy() {
    // Above 1 a body leaves every impact faster than it arrived, and the scene
    // shakes itself apart within seconds.
    CHECK_MSG(PhysicsSystem::CombineRestitution(5.0f, 5.0f) < 1.0f,
              "restitution must stay below 1 or bouncing adds energy");
    CHECK_MSG(PhysicsSystem::CombineRestitution(-3.0f, 0.0f) >= 0.0f,
              "and negative restitution must not suck bodies together");
}

static void testFrictionIsZeroIfEitherSurfaceIsIce() {
    // The geometric mean has the property that matters: ice against anything
    // is still slippery. An average would let a rough floor grip a puck.
    CHECK_NEAR(PhysicsSystem::CombineFriction(0.0f, 1.0f), 0.0f);
    CHECK_NEAR(PhysicsSystem::CombineFriction(0.4f, 0.4f), 0.4f);
}

static void testABouncierBallReboundsHigher() {
    // The property has to reach the solver, not merely be stored.
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f));

    const auto dead = makeSphere(registry, glm::vec3(-4.0f, 3.0f, 0.0f));
    registry.get<RigidBodyComponent>(dead).restitution = 0.0f;

    const auto bouncy = makeSphere(registry, glm::vec3(4.0f, 3.0f, 0.0f));
    registry.get<RigidBodyComponent>(bouncy).restitution = 0.9f;

    stepFor(registry, 2.0f);

    const float deadHeight = registry.get<TransformComponent>(dead).position.y;
    const float bouncyHeight = registry.get<TransformComponent>(bouncy).position.y;

    CHECK_MSG(bouncyHeight > deadHeight + 0.05f,
              "the bouncier ball must still be higher: dead " + std::to_string(deadHeight) +
                  " vs bouncy " + std::to_string(bouncyHeight));
}

static void testDampingSlowsABodyWithNothingTouchingIt() {
    entt::registry registry;

    const auto drifting = registry.create();
    registry.emplace<TransformComponent>(drifting, glm::vec3(0.0f, 50.0f, 0.0f));
    auto& body = registry.emplace<RigidBodyComponent>(drifting);
    body.useGravity = false;
    body.velocity = glm::vec3(10.0f, 0.0f, 0.0f);
    body.linearDamping = 0.9f;

    stepFor(registry, 1.0f);

    const float speed = glm::length(registry.get<RigidBodyComponent>(drifting).velocity);
    CHECK_MSG(speed < 5.0f, "damping must bleed off speed: got " + std::to_string(speed));
    CHECK_MSG(speed > 0.0f, "but not stop the body dead in one second");
}

// --- rotation ---------------------------------------------------------------
//
// Nothing rotated at all: a crate dropped on its corner landed flat, a ball
// never rolled, and a hit off the centre of mass pushed a body without turning
// it.

static void testAngularVelocityTurnsTheTransform() {
    entt::registry registry;

    const auto spinner = registry.create();
    registry.emplace<TransformComponent>(spinner, glm::vec3(0.0f, 50.0f, 0.0f));
    auto& body = registry.emplace<RigidBodyComponent>(spinner);
    body.useGravity = false;
    body.angularDamping = 0.0f;
    // A quarter turn per second about Y.
    body.angularVelocity = glm::vec3(0.0f, glm::half_pi<float>(), 0.0f);

    stepFor(registry, 1.0f);

    // Compared as a rotation rather than as three numbers: the Euler triple
    // for a given orientation is not unique, so checking the angles directly
    // fails on a representation change that means nothing.
    const glm::quat actual(registry.get<TransformComponent>(spinner).rotation);
    const glm::quat expected =
        glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));

    const float alignment = std::abs(glm::dot(glm::normalize(actual), glm::normalize(expected)));
    CHECK_MSG(alignment > 0.99f,
              "a quarter turn per second must be a quarter turn after a second: "
              "alignment " + std::to_string(alignment));
}

static void testFrozenRotationNeverTurns() {
    // A character or a camera boom wants to be shoved around without tipping.
    entt::registry registry;

    const auto upright = registry.create();
    registry.emplace<TransformComponent>(upright, glm::vec3(0.0f, 50.0f, 0.0f));
    auto& body = registry.emplace<RigidBodyComponent>(upright);
    body.useGravity = false;
    body.freezeRotation = true;
    body.angularVelocity = glm::vec3(3.0f, 3.0f, 3.0f);

    stepFor(registry, 1.0f);

    const glm::vec3 rotation = registry.get<TransformComponent>(upright).rotation;
    CHECK_MSG(glm::length(rotation) < 1e-4f, "a frozen body must not turn at all");
    CHECK_MSG(glm::length(registry.get<RigidBodyComponent>(upright).angularVelocity) < 1e-6f,
              "and its spin must be cleared, not merely ignored");
}

static void testAngularDampingSlowsSpin() {
    entt::registry registry;

    const auto spinner = registry.create();
    registry.emplace<TransformComponent>(spinner, glm::vec3(0.0f, 50.0f, 0.0f));
    auto& body = registry.emplace<RigidBodyComponent>(spinner);
    body.useGravity = false;
    body.angularVelocity = glm::vec3(0.0f, 8.0f, 0.0f);
    body.angularDamping = 0.9f;

    stepFor(registry, 1.0f);

    const float speed = registry.get<RigidBodyComponent>(spinner).angularVelocity.y;
    CHECK_MSG(speed < 4.0f, "spin must decay: got " + std::to_string(speed));
    CHECK_MSG(speed > 0.0f, "but not reverse");
}

static void testAnOffCentreImpactCreatesSpin() {
    // The whole point. A box landing with only one corner over an obstacle
    // must start turning; before this it stayed perfectly level.
    entt::registry registry;

    // A small static block, offset so the falling box lands on its edge.
    const auto block = registry.create();
    registry.emplace<TransformComponent>(block, glm::vec3(0.9f, 0.0f, 0.0f));
    registry.emplace<BoxColliderComponent>(block);

    const auto falling = makeBox(registry, glm::vec3(0.0f, 1.6f, 0.0f));
    registry.get<RigidBodyComponent>(falling).angularDamping = 0.0f;

    stepFor(registry, 0.6f);

    const glm::vec3 spin = registry.get<RigidBodyComponent>(falling).angularVelocity;
    CHECK_MSG(glm::length(spin) > 0.05f,
              "landing on one corner must impart spin: got " + std::to_string(glm::length(spin)));

    // And about the right axis: the contact is offset along x, so the box
    // tips about z. A spin about the wrong axis would look like the box
    // spinning on the spot instead of toppling.
    CHECK_MSG(std::abs(spin.z) > std::abs(spin.y),
              "a contact offset along x must tip the box about z, not spin it about y");
}

static void testACentredImpactCreatesNoSpin() {
    // The control for the test above. A box landing squarely must not start
    // turning, or every stack in the scene would slowly come apart.
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f));

    const auto falling = makeBox(registry, glm::vec3(0.0f, 2.0f, 0.0f));
    registry.get<RigidBodyComponent>(falling).angularDamping = 0.0f;

    stepFor(registry, 1.5f);

    const float spin = glm::length(registry.get<RigidBodyComponent>(falling).angularVelocity);
    CHECK_MSG(spin < 0.05f,
              "a square landing must not impart spin: got " + std::to_string(spin));
}

static void testSpinDoesNotAppearFromNothing() {
    // A body resting undisturbed must stay still. Angular terms that leak
    // energy show up here first, as a stack that slowly rotates itself apart.
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f));

    // On a block well above the world ground plane, so it is genuinely
    // resting on a collider rather than on the integrator's floor clamp.
    const auto block = registry.create();
    registry.emplace<TransformComponent>(block, glm::vec3(0.0f, 1.0f, 0.0f),
                                         glm::vec3(0.0f), glm::vec3(6.0f, 1.0f, 6.0f));
    registry.emplace<BoxColliderComponent>(block);

    const auto resting = makeBox(registry, glm::vec3(0.0f, 2.05f, 0.0f));
    auto& body = registry.get<RigidBodyComponent>(resting);
    body.angularDamping = 0.0f;
    body.velocity = glm::vec3(0.0f);

    // Not allowed to sleep, deliberately. A sleeping body cannot spin up, so
    // leaving this one eligible would let the check below pass by freezing
    // rather than by being stable - which is the test measuring the feature
    // that was added after it instead of the one it was written for.
    body.allowSleep = false;

    std::vector<PhysicsSystem::Contact> contacts;
    bool touched = false;
    for (int i = 0; i < 180; ++i) {
        PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);
        if (!contacts.empty()) touched = true;
    }
    // Without this the test could pass by never touching the floor at all,
    // which is the one way a stability test silently stops covering anything.
    CHECK_MSG(touched, "the body must actually be resting ON something");

    const float spin = glm::length(registry.get<RigidBodyComponent>(resting).angularVelocity);
    CHECK_MSG(spin < 0.05f, "a resting body must not spin up: got " + std::to_string(spin));
}

static void testALongBoxIsHarderToTipAboutItsLongAxis() {
    // Inertia has to depend on the shape, not just the mass. A crate three
    // times longer in x resists turning about z far more than a cube of the
    // same mass, and a solver using mass alone would spin both identically.
    //
    // The comparison is controlled: same mass, same drop, and blocks placed so
    // the contact lands the same distance from each centre of mass, because
    // torque is force times arm and an uncontrolled arm would explain any
    // difference by itself.
    //
    // Everything sits well above y = 0: below that the integrator's world
    // ground plane catches a body before it ever reaches a collider, which is
    // how the first version of this test managed to make no contact at all.
    entt::registry registry;
    const float arm = 0.35f;

    const auto cube = makeBox(registry, glm::vec3(-20.0f, 2.5f, 0.0f), 1.0f, glm::vec3(1.0f));
    registry.get<RigidBodyComponent>(cube).angularDamping = 0.0f;
    const auto cubeBlock = registry.create();
    registry.emplace<TransformComponent>(cubeBlock, glm::vec3(-20.0f + arm, 1.0f, 0.0f));
    registry.emplace<BoxColliderComponent>(cubeBlock);

    const auto longBox = makeBox(registry, glm::vec3(20.0f, 2.5f, 0.0f), 1.0f,
                                 glm::vec3(3.0f, 1.0f, 1.0f));
    registry.get<RigidBodyComponent>(longBox).angularDamping = 0.0f;
    const auto longBlock = registry.create();
    registry.emplace<TransformComponent>(longBlock, glm::vec3(20.0f + arm, 1.0f, 0.0f));
    registry.emplace<BoxColliderComponent>(longBlock);

    // Measured a few steps after first contact rather than after a fixed forty
    // five, and this matters now that the narrowphase is oriented.
    //
    // The contact normal used to snap to a world axis whatever the body was
    // doing, so a tipping box was still pushed straight up and the two bodies
    // stayed comparable. With SAT the normal follows the ROTATED face, which is
    // correct and introduces a feedback loop: the more a box tips, the more its
    // normal tilts, and the more it tips. Left running for forty five steps the
    // two boxes diverge for reasons that have nothing to do with inertia, which
    // is the thing under test.
    //
    // Sampling just after impact keeps the normal near vertical for both, so
    // what is compared is the angular response to a known torque.
    std::vector<PhysicsSystem::Contact> contacts;
    bool touched = false;
    int settled = 0;
    for (int i = 0; i < 60 && settled < 4; ++i) {
        PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);
        if (!contacts.empty()) touched = true;
        if (touched) ++settled;
    }
    CHECK_MSG(touched, "the boxes must actually land on their blocks");

    const float cubeSpin = std::abs(registry.get<RigidBodyComponent>(cube).angularVelocity.z);
    const float longSpin = std::abs(registry.get<RigidBodyComponent>(longBox).angularVelocity.z);

    CHECK_MSG(cubeSpin > 1e-3f, "the cube must tip: got " + std::to_string(cubeSpin));
    CHECK_MSG(longSpin > 1e-4f, "and so must the long box: got " + std::to_string(longSpin));
    // 1.15, down from 1.5 when this ran against the AABB narrowphase.
    //
    // The claim under test is directional and physical: a 3 x 1 x 1 box has
    // five times the moment of inertia about z that a unit cube has, so the
    // same torque spins it less. That still holds.
    //
    // The MARGIN is not physical. It depends on where the contact lands, and
    // that moved: the old code applied its impulse at the midpoint of the
    // overlapping AABB region, and SAT applies it at the centroid of the
    // clipped face manifold, which is a different point. Treating 1.5 as
    // meaningful would be reading a calibration constant as a law.
    //
    // Expect to revisit it once the solver applies all four manifold points
    // with accumulated impulses, because that changes the torque arm again -
    // and at that point the honest form of this test is a direct assertion
    // about the inertia tensor rather than an inference from a landing.
    CHECK_MSG(cubeSpin > longSpin * 1.15f,
              "the longer box resists turning about z: cube " + std::to_string(cubeSpin) +
                  " vs long " + std::to_string(longSpin));
}

static void testLayerMasksSuppressAPair() {
    // Without layers everything collides with everything, which is a design
    // ceiling rather than a tuning problem: a bullet cannot ignore the thing
    // that fired it and a camera boom cannot pass through the player.
    entt::registry registry;

    const auto a = registry.create();
    registry.emplace<TransformComponent>(a).position = glm::vec3(0.0f, 5.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(a);
    auto& boxA = registry.emplace<BoxColliderComponent>(a);
    boxA.layer = 1u << 0;
    boxA.collidesWith = 1u << 1;   // talks only to layer 1

    const auto b = registry.create();
    registry.emplace<TransformComponent>(b).position = glm::vec3(0.2f, 5.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(b);
    auto& boxB = registry.emplace<BoxColliderComponent>(b);
    boxB.layer = 1u << 2;          // NOT layer 1
    boxB.collidesWith = 0xFFFFFFFFu;

    // Deeply overlapping, so anything that tests them will report a contact.
    std::vector<PhysicsSystem::Contact> contacts;
    PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);

    CHECK_MSG(contacts.empty(),
              "a pair whose masks do not agree must never reach the narrowphase");

    // And the same pair DOES collide once both sides agree, so the test is
    // measuring the mask rather than a geometry mistake.
    boxA.collidesWith = 0xFFFFFFFFu;
    contacts.clear();
    PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);
    CHECK_MSG(!contacts.empty(), "with agreeing masks the same pair must collide");
}

static void testFilteringNeedsBothSidesToAgree() {
    // One-way filtering would let A push B while B ignored A, which the solver
    // resolves as a one-sided impulse - an object shoved by something it is not
    // touching.
    entt::registry registry;

    const auto a = registry.create();
    registry.emplace<TransformComponent>(a).position = glm::vec3(0.0f, 5.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(a);
    auto& boxA = registry.emplace<BoxColliderComponent>(a);
    boxA.layer = 1u << 0;
    boxA.collidesWith = 0xFFFFFFFFu;   // A is willing

    const auto b = registry.create();
    registry.emplace<TransformComponent>(b).position = glm::vec3(0.2f, 5.0f, 0.0f);
    registry.emplace<RigidBodyComponent>(b);
    auto& boxB = registry.emplace<BoxColliderComponent>(b);
    boxB.layer = 1u << 1;
    boxB.collidesWith = 1u << 5;       // B is not

    std::vector<PhysicsSystem::Contact> contacts;
    PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);
    CHECK_MSG(contacts.empty(), "one unwilling side must suppress the pair");
}

static void testColliderCenterMovesTheCollider() {
    // Without an offset a collider is nailed to the entity origin, so a
    // character whose mesh pivots at the feet cannot have a body at its chest.
    entt::registry registry;

    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = glm::vec3(0.0f, 10.0f, 0.0f);
    auto& box = registry.emplace<BoxColliderComponent>(entity);
    box.size = glm::vec3(1.0f);

    // A ray four units to the side misses a collider at the origin.
    const glm::vec3 origin(4.0f, 10.0f, 0.0f);
    const glm::vec3 towards(-1.0f, 0.0f, 0.0f);

    const auto missed = PhysicsSystem::Raycast(registry, origin, towards, 2.0f);
    CHECK_MSG(!missed.hit, "the ray must fall short of a collider at the origin");

    // Offsetting the collider towards the ray brings it into reach, without the
    // entity itself having moved.
    box.center = glm::vec3(3.0f, 0.0f, 0.0f);
    const auto found = PhysicsSystem::Raycast(registry, origin, towards, 2.0f);
    CHECK_MSG(found.hit, "the offset must move the collider the query sees");
    CHECK_NEAR(registry.get<TransformComponent>(entity).position.x, 0.0f);
}

static void testRaycastRespectsItsLayerMask() {
    entt::registry registry;

    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = glm::vec3(0.0f, 10.0f, 0.0f);
    auto& box = registry.emplace<BoxColliderComponent>(entity);
    box.layer = 1u << 3;

    const glm::vec3 origin(4.0f, 10.0f, 0.0f);
    const glm::vec3 towards(-1.0f, 0.0f, 0.0f);

    const auto anyLayer = PhysicsSystem::Raycast(registry, origin, towards, 10.0f);
    CHECK_MSG(anyLayer.hit, "the default mask must hit everything, as before");

    const auto wrongLayer = PhysicsSystem::Raycast(registry, origin, towards, 10.0f,
                                                   entt::null, false, 1u << 1);
    CHECK_MSG(!wrongLayer.hit, "a ray must not hit a layer it did not ask for");

    const auto rightLayer = PhysicsSystem::Raycast(registry, origin, towards, 10.0f,
                                                   entt::null, false, 1u << 3);
    CHECK_MSG(rightLayer.hit, "and must hit the layer it did ask for");
}

static void testAStackOfBoxesSettlesInsteadOfSinking() {
    // Nothing tested stacking, and until the solver iterated, nothing could
    // have passed. A single pass resolves each contact as though it were the
    // only one in the world: the middle box is pushed out of the box below it
    // and straight into the box above, every step, so a stack sinks into itself
    // and shivers rather than coming to rest.
    entt::registry registry;

    // A wide static floor well above the world ground plane, so it is this
    // floor holding the stack up rather than the unconditional y = 0 clamp.
    makeStaticBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(10.0f, 1.0f, 10.0f));

    // Three unit boxes, each resting on the one below. Floor top is at y = 5.5.
    std::vector<entt::entity> stack;
    for (int i = 0; i < 3; ++i) {
        stack.push_back(makeBox(registry, glm::vec3(0.0f, 6.0f + static_cast<float>(i), 0.0f)));
    }

    stepFor(registry, 3.0f);

    // Every box must still be above the floor surface. A sinking stack ends
    // with the bottom box inside the floor.
    for (size_t i = 0; i < stack.size(); ++i) {
        const float y = registry.get<TransformComponent>(stack[i]).position.y;
        CHECK_MSG(y > 5.4f, "box " + std::to_string(i) +
                                " sank into the floor: y = " + std::to_string(y));
    }

    // And they must still be in order, each above the last, rather than having
    // interpenetrated into a heap.
    for (size_t i = 1; i < stack.size(); ++i) {
        const float below = registry.get<TransformComponent>(stack[i - 1]).position.y;
        const float above = registry.get<TransformComponent>(stack[i]).position.y;
        CHECK_MSG(above > below + 0.8f,
                  "box " + std::to_string(i) + " must still be stacked on the one below: " +
                      std::to_string(below) + " then " + std::to_string(above));
    }

    // Settled, not vibrating. This is what the accumulated impulse buys: with
    // per-pass clamping the contacts converge instead of fighting.
    for (size_t i = 0; i < stack.size(); ++i) {
        const glm::vec3 velocity = registry.get<RigidBodyComponent>(stack[i]).velocity;
        CHECK_MSG(glm::length(velocity) < 0.5f,
                  "box " + std::to_string(i) + " must have settled: |v| = " +
                      std::to_string(glm::length(velocity)));
    }
}


// --- sleeping ---------------------------------------------------------------
//
// Every settled crate in a level was costing a full integrate-and-solve per step
// to compute the same answer it computed last step. A body that has been still
// for half a second now stops being simulated and stands in as immovable for
// whatever is still awake, until something touches it or moves it.

static void testASettledBodyFallsAsleep() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));
    const auto crate = makeBox(registry, glm::vec3(0.0f, 6.2f, 0.0f));

    stepFor(registry, 3.0f);

    const auto& body = registry.get<RigidBodyComponent>(crate);
    CHECK_MSG(body.isSleeping, "a body resting on a floor must fall asleep");

    // The payoff is that it then stops moving AT ALL, not merely slowly. A
    // sleeping body is not integrated, so this is exact rather than a tolerance.
    const glm::vec3 before = registry.get<TransformComponent>(crate).position;
    stepFor(registry, 2.0f);
    const glm::vec3 after = registry.get<TransformComponent>(crate).position;

    CHECK_MSG(before == after,
              "a sleeping body must not move by even a float: y went " +
                  std::to_string(before.y) + " -> " + std::to_string(after.y));
}

static void testSleepChangesWhereThingsEndUpByNothing() {
    // The check that makes "sleeping costs less and changes nothing" a claim
    // rather than an assumption. The same scene twice, once allowed to sleep and
    // once not: if sleeping freezes a body at a different equilibrium, or fires
    // somewhere it should not have, the two runs disagree and nothing else in
    // this file would notice.
    const auto build = [](entt::registry& registry, bool allowSleep) {
        makeStaticBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));
        std::vector<entt::entity> bodies;
        bodies.push_back(makeBox(registry, glm::vec3(0.0f, 6.2f, 0.0f)));
        bodies.push_back(makeBox(registry, glm::vec3(0.0f, 7.4f, 0.0f)));
        bodies.push_back(makeSphere(registry, glm::vec3(3.0f, 6.5f, 0.0f)));
        for (auto entity : bodies) {
            registry.get<RigidBodyComponent>(entity).allowSleep = allowSleep;
        }
        return bodies;
    };

    entt::registry sleeping;
    entt::registry awake;
    const auto sleepingBodies = build(sleeping, true);
    const auto awakeBodies = build(awake, false);

    stepFor(sleeping, 3.0f);
    stepFor(awake, 3.0f);

    // Measured at exactly zero for all three bodies: a resting body reaches a
    // true fixed point, where the distance it sinks under gravity in one step
    // and the distance the positional correction pushes it back cancel to the
    // same float. So a sleeping body is not frozen NEAR where an awake one
    // hovers, it is frozen exactly there. The tolerance is left at 1e-4 rather
    // than requiring bit equality only because a compiler that contracts the
    // arithmetic differently could land on a two-step cycle instead of a fixed
    // point; it is still fifty times tighter than the solver's resting slop.
    for (size_t i = 0; i < sleepingBodies.size(); ++i) {
        const glm::vec3 slept = sleeping.get<TransformComponent>(sleepingBodies[i]).position;
        const glm::vec3 ran = awake.get<TransformComponent>(awakeBodies[i]).position;
        const float drift = glm::length(slept - ran);
        CHECK_MSG(drift < 1e-4f,
                  "body " + std::to_string(i) + " ended up somewhere else when it slept: " +
                      std::to_string(drift));
    }

    // And the run that was allowed to sleep must actually have slept, or the
    // comparison above proves nothing at all.
    bool anyAsleep = false;
    for (auto entity : sleepingBodies) {
        if (sleeping.get<RigidBodyComponent>(entity).isSleeping) anyAsleep = true;
    }
    CHECK_MSG(anyAsleep, "the sleeping run must have put something to sleep");
}

static void testSomethingLandingOnASleepingBodyWakesIt() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));
    const auto crate = makeBox(registry, glm::vec3(0.0f, 6.2f, 0.0f));

    stepFor(registry, 3.0f);
    CHECK_MSG(registry.get<RigidBodyComponent>(crate).isSleeping,
              "the crate must be asleep before anything is dropped on it");

    const float restingY = registry.get<TransformComponent>(crate).position.y;

    // Dropped from high enough to be moving well above the sleep threshold.
    const auto dropped = makeBox(registry, glm::vec3(0.0f, 9.0f, 0.0f));
    stepFor(registry, 1.5f);

    CHECK_MSG(!registry.get<RigidBodyComponent>(crate).isSleeping ||
                  registry.get<TransformComponent>(dropped).position.y > restingY + 0.8f,
              "a sleeping body must wake up and carry what lands on it");

    // The real question: did the impact transfer at all. A wake that arrives one
    // step late leaves the pair already separated by the positional correction,
    // and the crate never receives the impulse - it would sit at exactly the
    // height it went to sleep at while the dropped box rests on top of it.
    const float loadedY = registry.get<TransformComponent>(crate).position.y;
    CHECK_MSG(loadedY < restingY - 1e-4f,
              "the impact must actually reach the woken body: y stayed at " +
                  std::to_string(loadedY));
}

static void testWritingVelocityWakesASleepingBody() {
    // The path every script takes. SupersonicScriptWorld::setVelocity and
    // addForce write RigidBodyComponent::velocity directly, so a sleeping body
    // that ignored that write would make both of them silently do nothing.
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));
    const auto crate = makeBox(registry, glm::vec3(0.0f, 6.2f, 0.0f));

    stepFor(registry, 3.0f);
    CHECK_MSG(registry.get<RigidBodyComponent>(crate).isSleeping, "must be asleep first");

    const float startX = registry.get<TransformComponent>(crate).position.x;
    registry.get<RigidBodyComponent>(crate).velocity = glm::vec3(4.0f, 0.0f, 0.0f);
    stepFor(registry, 0.5f);

    CHECK_MSG(!registry.get<RigidBodyComponent>(crate).isSleeping,
              "writing a velocity must wake the body");
    const float movedX = registry.get<TransformComponent>(crate).position.x;
    CHECK_MSG(movedX > startX + 0.5f,
              "and it must actually move: x = " + std::to_string(movedX));
}

static void testMovingASleepingBodyWakesIt() {
    // An editor gizmo, a script setting a position, the time-travel debugger
    // scrubbing back. Without this the crate hangs wherever it was put.
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));
    const auto crate = makeBox(registry, glm::vec3(0.0f, 6.2f, 0.0f));

    stepFor(registry, 3.0f);
    CHECK_MSG(registry.get<RigidBodyComponent>(crate).isSleeping, "must be asleep first");

    registry.get<TransformComponent>(crate).position.y += 4.0f;
    stepFor(registry, 0.2f);

    CHECK_MSG(!registry.get<RigidBodyComponent>(crate).isSleeping,
              "a sleeping body that is moved must wake up");
    CHECK_MSG(registry.get<RigidBodyComponent>(crate).velocity.y < -0.5f,
              "and start falling again rather than hanging in the air");
}

static void testABodyOnAKinematicPlatformNeverSleeps() {
    // A kinematic body is moved by code the solver cannot see: there is no
    // velocity to read and no way to know it is about to slide out from under
    // whatever is standing on it. So nothing resting on one is allowed to sleep,
    // or a lift arrives at the top floor with its cargo left behind in the air.
    entt::registry registry;

    const auto platform = registry.create();
    registry.emplace<TransformComponent>(platform, glm::vec3(0.0f, 5.0f, 0.0f),
                                         glm::vec3(0.0f), glm::vec3(20.0f, 1.0f, 20.0f));
    registry.emplace<BoxColliderComponent>(platform);
    registry.emplace<RigidBodyComponent>(platform).isKinematic = true;

    const auto cargo = makeBox(registry, glm::vec3(0.0f, 6.2f, 0.0f));

    stepFor(registry, 4.0f);

    CHECK_MSG(!registry.get<RigidBodyComponent>(cargo).isSleeping,
              "a body resting on a kinematic platform must stay awake");

    // And the same body on an identical STATIC platform must sleep, or the check
    // above passes for the wrong reason - because nothing ever sleeps.
    entt::registry control;
    makeStaticBox(control, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));
    const auto resting = makeBox(control, glm::vec3(0.0f, 6.2f, 0.0f));
    stepFor(control, 4.0f);
    CHECK_MSG(control.get<RigidBodyComponent>(resting).isSleeping,
              "the same body on a static floor must sleep");
}

static void testASleepingBodyStillReportsItsContacts() {
    // Sleeping removes the RESPONSE, not the report. A trigger volume must not
    // forget about something that fell asleep inside it, and anything diffing
    // the contact list for enter/stay/exit would otherwise see every settled
    // body exit the moment it went quiet.
    entt::registry registry;
    const auto floorEntity =
        makeStaticBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));
    const auto crate = makeBox(registry, glm::vec3(0.0f, 6.2f, 0.0f));

    stepFor(registry, 3.0f);
    CHECK_MSG(registry.get<RigidBodyComponent>(crate).isSleeping, "must be asleep first");

    std::vector<PhysicsSystem::Contact> contacts;
    PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);

    bool reported = false;
    for (const auto& contact : contacts) {
        if ((contact.a == crate && contact.b == floorEntity) ||
            (contact.a == floorEntity && contact.b == crate)) {
            reported = true;
        }
    }
    CHECK_MSG(reported, "a sleeping body must still report the contact holding it up");
}

static void testSleepCanBeTurnedOff() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));
    const auto crate = makeBox(registry, glm::vec3(0.0f, 6.2f, 0.0f));
    registry.get<RigidBodyComponent>(crate).allowSleep = false;

    stepFor(registry, 4.0f);

    CHECK_MSG(!registry.get<RigidBodyComponent>(crate).isSleeping,
              "allowSleep = false must keep a body simulated");
}


static void testInertiaFollowsTheBoxAndNotItsBoundingBox() {
    // Physics has to be rotation-invariant: turning a whole scene about the
    // gravity axis must not change what happens in it. That is the cleanest
    // statement of what a mass tensor is for, and it is exactly what a tensor
    // taken from the world AABB gets wrong.
    //
    // A 5 x 1 x 1 plank turned 45 degrees about Y has a bounding box 4.24
    // across, and the moment of inertia of that box about the axis the impact
    // turns it around is 1.58 against the plank's own 2.17. The same object,
    // described as a third easier to turn, decided purely by which way it
    // happens to be facing. That was correct while box-box collided AS its
    // bounding box; it stopped being correct the moment SAT started colliding
    // the box itself, and nothing noticed because every other test in this file
    // uses square bodies, where the two descriptions are identical.
    //
    // Struck by a falling sphere rather than dropped onto a block, and with
    // gravity off on both. A drop compares two impacts that do not happen at
    // the same instant - the two planks reach their blocks a fraction of a step
    // apart and the difference in when swamps the difference in inertia. Here
    // the striker is launched from a fixed distance at a fixed speed, so the
    // impact is the same event in both runs and the only thing that differs is
    // the tensor.
    const float yawAngle = glm::quarter_pi<float>();

    // Out near the end, where the angular term dominates the effective mass.
    // Close to the centre the impulse is mostly linear and the tensor barely
    // shows up at all - which is how the first version of this test managed to
    // measure a one percent difference for a thirty percent error.
    const float arm = 2.0f;

    const auto run = [&](float yaw) {
        entt::registry registry;
        const float cosine = std::cos(yaw);
        const float sine = std::sin(yaw);

        // glm's rotation about +Y takes local +x to (cos, 0, -sin).
        const auto turn = [&](const glm::vec3& v) {
            return glm::vec3(v.x * cosine + v.z * sine, v.y, -v.x * sine + v.z * cosine);
        };

        const auto plank = registry.create();
        registry.emplace<TransformComponent>(plank, glm::vec3(0.0f, 5.0f, 0.0f),
                                             glm::vec3(0.0f, yaw, 0.0f),
                                             glm::vec3(5.0f, 1.0f, 1.0f));
        auto& plankBody = registry.emplace<RigidBodyComponent>(plank);
        plankBody.angularDamping = 0.0f;
        plankBody.useGravity = false;
        registry.emplace<BoxColliderComponent>(plank);

        const auto striker = registry.create();
        registry.emplace<TransformComponent>(striker, turn(glm::vec3(arm, 0.0f, 0.0f)) +
                                                          glm::vec3(0.0f, 5.9f, 0.0f));
        auto& strikerBody = registry.emplace<RigidBodyComponent>(striker);
        strikerBody.useGravity = false;
        strikerBody.angularDamping = 0.0f;
        strikerBody.velocity = glm::vec3(0.0f, -6.0f, 0.0f);
        registry.emplace<SphereColliderComponent>(striker).radius = 0.25f;

        // Stopped two steps after the impulse arrives. With no gravity and no
        // angular damping the spin is then constant, and stopping keeps the
        // spinning end from swinging back up into the striker and adding a
        // second impact that is not part of the experiment.
        int after = -1;
        for (int i = 0; i < 120 && after < 2; ++i) {
            PhysicsSystem::Update(registry, 1.0f / 60.0f);
            const float spin = glm::length(registry.get<RigidBodyComponent>(plank).angularVelocity);
            if (after >= 0) ++after;
            else if (spin > 1e-6f) after = 0;
        }
        CHECK_MSG(after >= 0, "the striker must actually hit the plank");
        return glm::length(registry.get<RigidBodyComponent>(plank).angularVelocity);
    };

    const float square = run(0.0f);
    const float turned = run(yawAngle);

    CHECK_MSG(square > 1e-3f, "the square plank must turn: got " + std::to_string(square));
    CHECK_MSG(turned > 1e-3f, "the turned plank must turn: got " + std::to_string(turned));

    // Measured at exactly 1.000000: the two spins come out bit-identical, which
    // is what rotation invariance means when the tensor is right. The tolerance
    // is left at a thousandth rather than requiring bit equality only because a
    // compiler that contracts the arithmetic differently would not reproduce
    // that exactly. Against 1.16 when the tensor is taken from the bounding box,
    // which is the number this test exists to catch.
    const float ratio = turned / square;
    CHECK_MSG(ratio > 0.999f && ratio < 1.001f,
              "turning the scene must not change the spin: square " +
                  std::to_string(square) + " vs turned " + std::to_string(turned) +
                  " (ratio " + std::to_string(ratio) + ")");
}


// --- world settings ---------------------------------------------------------
//
// Gravity and the ground plane were file-static constants. The plane in
// particular existed whether or not a scene had a floor, applied the body's own
// restitution and no friction at all, and could not be turned off - so a pit was
// not possible, a level built below the origin was unreachable, and there was no
// entity to select to find out why.

static void testThereIsNoGroundPlaneUnlessTheSceneAsksForOne() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity, glm::vec3(0.0f, 2.0f, 0.0f));
    registry.emplace<RigidBodyComponent>(entity);
    registry.emplace<BoxColliderComponent>(entity);

    stepFor(registry, 2.0f);

    CHECK_MSG(transform.position.y < -10.0f,
              "a body over nothing must fall through where the plane used to be: y = " +
                  std::to_string(transform.position.y));
}

static void testTheGroundPlaneCanSitSomewhereOtherThanZero() {
    entt::registry registry;
    enableGroundPlane(registry, -8.0f);

    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity, glm::vec3(0.0f, 2.0f, 0.0f));
    registry.emplace<RigidBodyComponent>(entity);
    registry.emplace<BoxColliderComponent>(entity);

    stepFor(registry, 6.0f);

    // Measured against the bottom of the collider, so a unit cube rests with its
    // origin half a unit above the plane.
    CHECK_NEAR(transform.position.y, -7.5f);
}

static void testGravityIsAPropertyOfTheScene() {
    // Sideways, and stronger than Earth's. A constant in the physics source
    // meant a scene on the moon, underwater, or in a corridor with gravity
    // pointing along a wall was an edit to the engine.
    entt::registry registry;
    PhysicsSettings settings;
    settings.gravity = glm::vec3(20.0f, 0.0f, 0.0f);
    registry.ctx().insert_or_assign<PhysicsSettings>(std::move(settings));

    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity, glm::vec3(0.0f, 5.0f, 0.0f));
    registry.emplace<RigidBodyComponent>(entity);

    stepFor(registry, 1.0f);

    CHECK_MSG(transform.position.x > 5.0f,
              "the body must accelerate along the scene's gravity: x = " +
                  std::to_string(transform.position.x));
    CHECK_NEAR(transform.position.y, 5.0f);
}

static void testABodyCanOptOutOfTheScenesGravity() {
    // The per-body switch has to still win, or turning gravity into a scene
    // setting would quietly break every floating body in every scene.
    entt::registry registry;
    PhysicsSettings settings;
    settings.gravity = glm::vec3(0.0f, -30.0f, 0.0f);
    registry.ctx().insert_or_assign<PhysicsSettings>(std::move(settings));

    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity, glm::vec3(0.0f, 5.0f, 0.0f));
    registry.emplace<RigidBodyComponent>(entity).useGravity = false;

    stepFor(registry, 1.0f);

    CHECK_NEAR(transform.position.y, 5.0f);
}


static void testTheShippedSceneHasAFloorOfItsOwn() {
    // The unconditional ground plane used to mean a scene could leave out its
    // floor and still work. It cannot any more, and the scene that shipped
    // relying on it is the one most likely to regress unnoticed: it is an asset
    // rather than code, so nothing else in this suite ever looks at it.
    const std::string path = "assets/scenes/MainScene.scene";
    if (!std::filesystem::exists(path)) {
        CHECK_MSG(false, "the shipped scene must be reachable from the test working "
                         "directory (" + std::filesystem::current_path().string() + ")");
        return;
    }

    entt::registry registry;
    const auto result = SceneSerializer::Deserialize(registry, path);
    CHECK_MSG(result.ok, "the shipped scene must load: " + result.message);
    if (!result.ok) return;

    int bodies = 0;
    for (auto entity : registry.view<RigidBodyComponent>()) {
        (void)entity;
        ++bodies;
    }
    // Otherwise the loop below is a check over nothing, which is how this kind
    // of test quietly stops covering anything.
    CHECK_MSG(bodies > 0, "the shipped scene must contain something that falls");

    stepFor(registry, 5.0f);
    TransformSystem::UpdateWorldTransforms(registry);

    // Five seconds of free fall is a hundred and twenty units. Anything still
    // within a few units of the origin landed on something.
    for (auto entity : registry.view<RigidBodyComponent, TransformComponent>()) {
        const auto* world = registry.try_get<WorldTransformComponent>(entity);
        const float y = world ? world->matrix[3].y
                              : registry.get<TransformComponent>(entity).position.y;
        std::string name = "entity";
        if (const auto* tag = registry.try_get<TagComponent>(entity)) name = tag->tag;
        CHECK_MSG(y > -5.0f, name + " fell out of the shipped scene: y = " + std::to_string(y));
    }
}


// --- capsules ---------------------------------------------------------------
//
// The shape a character wants, and the reason it is worth a third collider: a
// box catches on every seam it walks over and a sphere rolls off everything.

static void testACapsuleRestsOnItsOwnBottom() {
    entt::registry registry;
    // Floor top at y = 5.5.
    makeStaticBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));

    // Radius 0.4, total height 2.0, so its lowest point is one unit below the
    // origin and it must come to rest at 6.5 - not at 5.9, which is where it
    // would sit if the height were being read as the straight section, and not
    // at 5.9 either if the caps were being ignored.
    const auto capsule = makeCapsule(registry, glm::vec3(0.0f, 8.0f, 0.0f));

    stepFor(registry, 3.0f);

    // Within the solver's resting slop of 6.5, not exactly on it: a body resting
    // on a COLLIDER settles kSlop deep, which is what stops the correction
    // firing forever against floating-point noise. The world ground plane is a
    // hard clamp and lands exactly, which is why the older rest-height tests
    // above can use CHECK_NEAR and this one cannot.
    const float y = registry.get<TransformComponent>(capsule).position.y;
    CHECK_MSG(std::fabs(y - 6.5f) < 0.02f,
              "a capsule must rest one unit above the floor: y = " + std::to_string(y));
}

static void testACapsuleShorterThanItsDiameterIsASphere() {
    // Not an error to be rejected: it falls out of the geometry as a segment of
    // no length, which is exactly a sphere, and must rest at the radius.
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));

    const auto squat = makeCapsule(registry, glm::vec3(0.0f, 8.0f, 0.0f), 0.5f, 0.2f);

    stepFor(registry, 3.0f);

    const float y = registry.get<TransformComponent>(squat).position.y;
    CHECK_MSG(std::fabs(y - 6.0f) < 0.02f,
              "a squat capsule must rest at its radius: y = " + std::to_string(y));
}

static void testACapsuleRidesAStepThatStopsABox() {
    // The claim the shape exists to make. Both are driven into a low step at a
    // fixed speed, the way a character controller drives one, and both are
    // frozen against rotation so neither can simply topple over it.
    //
    // A box meets the step's face square on: the contact normal is horizontal
    // and the push is entirely backwards, so it stops dead. A capsule meets the
    // step's top EDGE with its bottom cap, and the normal from a corner points
    // up as well as back, so the same horizontal push lifts it.
    const float speed = 3.0f;
    const float stepHeight = 0.3f;

    const auto run = [&](bool capsule) {
        entt::registry registry;
        // A wide floor with its top at y = 5.
        makeStaticBox(registry, glm::vec3(0.0f, 4.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f));
        // The step: everything from x = 0 onwards, raised by stepHeight. Its
        // near face is therefore a wall at x = 0 and its top is at 5 + 0.3.
        makeStaticBox(registry, glm::vec3(10.0f, 5.0f + stepHeight * 0.5f, 0.0f),
                      glm::vec3(20.0f, stepHeight, 8.0f));

        const entt::entity mover =
            capsule ? makeCapsule(registry, glm::vec3(-1.0f, 6.0f, 0.0f), 0.4f, 2.0f)
                    : makeBox(registry, glm::vec3(-1.0f, 6.0f, 0.0f), 1.0f,
                              glm::vec3(0.8f, 2.0f, 0.8f));
        auto& body = registry.get<RigidBodyComponent>(mover);
        body.freezeRotation = true;
        // Nothing to grip with, so what happens at the step is the normal and
        // nothing else.
        body.friction = 0.0f;

        // Driven, not launched: the horizontal speed is reasserted every step,
        // which is what a character controller does and what makes this a test
        // of the contact rather than of momentum.
        for (int i = 0; i < 180; ++i) {
            registry.get<RigidBodyComponent>(mover).velocity.x = speed;
            PhysicsSystem::Update(registry, 1.0f / 60.0f);
        }
        return registry.get<TransformComponent>(mover).position.x;
    };

    const float boxX = run(false);
    const float capsuleX = run(true);

    // The step is 0.3 tall against a capsule radius of 0.4, so the contact lands
    // on the lower hemisphere and the normal has somewhere up to point. Raise it
    // past the radius and the contact moves onto the straight section, where the
    // normal is horizontal and the capsule is stopped like the box - which is
    // the correct behaviour, not a limitation.
    CHECK_MSG(boxX < 0.0f,
              "the box must be stopped by the step's face: x = " + std::to_string(boxX));
    CHECK_MSG(capsuleX > boxX + 0.5f,
              "a capsule must get over a step a box is stopped by: box " +
                  std::to_string(boxX) + " vs capsule " + std::to_string(capsuleX));
}

static void testACapsuleDoesNotSpinUpStandingStill() {
    // The stability check the box has. A capsule resting on a floor is held by a
    // single contact under its cap, so anything that leaks angular energy shows
    // up here as a character slowly lying down.
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));

    const auto capsule = makeCapsule(registry, glm::vec3(0.0f, 6.6f, 0.0f));
    auto& body = registry.get<RigidBodyComponent>(capsule);
    body.angularDamping = 0.0f;
    body.allowSleep = false; // or it passes by freezing rather than by being stable

    std::vector<PhysicsSystem::Contact> contacts;
    bool touched = false;
    for (int i = 0; i < 240; ++i) {
        PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);
        if (!contacts.empty()) touched = true;
    }
    CHECK_MSG(touched, "the capsule must actually be resting on the floor");

    const float spin = glm::length(registry.get<RigidBodyComponent>(capsule).angularVelocity);
    CHECK_MSG(spin < 0.05f, "a standing capsule must not spin up: got " + std::to_string(spin));
}

static void testTwoCapsulesPushEachOtherApart() {
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));

    const auto left = makeCapsule(registry, glm::vec3(-0.2f, 6.5f, 0.0f));
    const auto right = makeCapsule(registry, glm::vec3(0.2f, 6.5f, 0.0f));
    registry.get<RigidBodyComponent>(left).freezeRotation = true;
    registry.get<RigidBodyComponent>(right).freezeRotation = true;

    stepFor(registry, 2.0f);

    const float gap = registry.get<TransformComponent>(right).position.x -
                      registry.get<TransformComponent>(left).position.x;
    CHECK_MSG(gap > 0.7f,
              "two overlapping capsules must separate to about their diameter: gap = " +
                  std::to_string(gap));
}

static void testAnEntityWithTwoCollidersPicksOneShape() {
    // Box wins, then capsule, then sphere. Stated in the solver and repeated in
    // the queries, and if the two disagree an entity is gathered twice and
    // collides with itself - which reads as a body launching itself across the
    // level for no reason.
    entt::registry registry;

    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity, glm::vec3(0.0f, 5.0f, 0.0f));
    registry.emplace<RigidBodyComponent>(entity);
    registry.emplace<BoxColliderComponent>(entity);
    registry.emplace<CapsuleColliderComponent>(entity);
    registry.emplace<SphereColliderComponent>(entity);

    std::vector<PhysicsSystem::Contact> contacts;
    PhysicsSystem::Update(registry, 1.0f / 60.0f, &contacts);

    for (const auto& contact : contacts) {
        CHECK_MSG(!(contact.a == entity && contact.b == entity),
                  "an entity must never collide with itself");
    }

    // And a query must report it once, not three times.
    std::vector<entt::entity> hits;
    PhysicsSystem::OverlapSphere(registry, glm::vec3(0.0f, 5.0f, 0.0f), 2.0f, hits);
    int found = 0;
    for (auto hit : hits) {
        if (hit == entity) ++found;
    }
    CHECK_MSG(found == 1, "a query must report the entity exactly once, got " +
                              std::to_string(found));
}


static void testANudgedCapsuleOnItsSideSettlesInsteadOfRocking() {
    // Held by one contact under its middle, a capsule lying along a floor is
    // free to rock end over end about that point with nothing anywhere else to
    // resist. Measured before the manifold gained its second point: still
    // swinging at about a radian per second ten seconds after the nudge, with
    // its rotation oscillating between 1.44 and 1.73 either side of flat.
    //
    // This is the same failure the box manifold exists to prevent, arriving
    // again with a new shape - which is the argument for testing every shape
    // against it rather than trusting that the fix generalised.
    entt::registry registry;
    makeStaticBox(registry, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));

    const auto entity = registry.create();
    // Turned onto its side, tilted a quarter radian, and given a shove.
    registry.emplace<TransformComponent>(entity, glm::vec3(0.0f, 6.1f, 0.0f),
                                         glm::vec3(0.0f, 0.0f, glm::half_pi<float>() + 0.25f),
                                         glm::vec3(1.0f));
    auto& body = registry.emplace<RigidBodyComponent>(entity);
    body.angularDamping = 0.0f;   // nothing bleeds the rocking off artificially
    body.allowSleep = false;      // or it passes by freezing rather than settling
    body.angularVelocity = glm::vec3(0.0f, 0.0f, -0.8f);
    auto& collider = registry.emplace<CapsuleColliderComponent>(entity);
    collider.radius = 0.4f;
    collider.height = 2.0f;

    stepFor(registry, 6.0f);

    const float spin = glm::length(registry.get<RigidBodyComponent>(entity).angularVelocity);
    CHECK_MSG(spin < 0.1f,
              "a nudged capsule on its side must come to rest: |w| = " + std::to_string(spin));

    // And come to rest FLAT, not stopped at whatever angle it happened to reach.
    const float roll = registry.get<TransformComponent>(entity).rotation.z;
    CHECK_MSG(std::fabs(roll - glm::half_pi<float>()) < 0.15f,
              "and lie flat: rotation.z = " + std::to_string(roll));
}

static void runTests() {
    testANudgedCapsuleOnItsSideSettlesInsteadOfRocking();
    testACapsuleRestsOnItsOwnBottom();
    testACapsuleShorterThanItsDiameterIsASphere();
    testACapsuleRidesAStepThatStopsABox();
    testACapsuleDoesNotSpinUpStandingStill();
    testTwoCapsulesPushEachOtherApart();
    testAnEntityWithTwoCollidersPicksOneShape();
    testTheShippedSceneHasAFloorOfItsOwn();
    testThereIsNoGroundPlaneUnlessTheSceneAsksForOne();
    testTheGroundPlaneCanSitSomewhereOtherThanZero();
    testGravityIsAPropertyOfTheScene();
    testABodyCanOptOutOfTheScenesGravity();
    testInertiaFollowsTheBoxAndNotItsBoundingBox();
    testASettledBodyFallsAsleep();
    testSleepChangesWhereThingsEndUpByNothing();
    testSomethingLandingOnASleepingBodyWakesIt();
    testWritingVelocityWakesASleepingBody();
    testMovingASleepingBodyWakesIt();
    testABodyOnAKinematicPlatformNeverSleeps();
    testASleepingBodyStillReportsItsContacts();
    testSleepCanBeTurnedOff();
    testAStackOfBoxesSettlesInsteadOfSinking();
    testLayerMasksSuppressAPair();
    testFilteringNeedsBothSidesToAgree();
    testColliderCenterMovesTheCollider();
    testRaycastRespectsItsLayerMask();
    testSweepAndPruneFindsOverlappingPairs();
    testSweepAndPruneSkipsPairsSeparatedOffAxis();
    testSweepAndPruneIgnoresTwoStatics();
    testBoxesSeparateInsteadOfOverlapping();
    testBodyRestsOnStaticPlatformNotThroughIt();
    testStaticColliderDoesNotMove();
    testHeavierBodyMovesLessThanLighterOne();
    testSpheresSeparate();
    testSphereRestsOnBox();
    testTriggerReportsButDoesNotResolve();
    testContactsAreClearedEachStep();
    testKinematicBodyIsNotPushed();
    testDistantBodiesNeverContact();
    testParentedBodyFallsInWorldSpace();
    testRaycastHitsTheNearestCollider();
    testRaycastMissesAndRespectsRange();
    testRaycastIgnoresTheCasterAndTriggers();
    testRaycastHitsSpheres();
    testRaycastRejectsDegenerateInput();
    testRaycastDirectionNeedNotBeNormalised();
    testOverlapSphereFindsWhatItTouches();
    testGroundCheck();
    testGravityAccelerates();
    testKinematicBodiesDoNotMove();
    testRestsOnColliderBottomNotOrigin();
    testComesToRest();
    testRisingBodyIsNotReflected();
    testColliderSizeAffectsRestHeight();
    testZeroAndNegativeDeltaAreIgnored();
    testFixedStepKeepsABodyOnTheGround();
    testAFastProjectileDoesNotPassThroughAThinWall();

    testASphereIsTheSameSizeWhicheverWayItIsTurned();
    testABallLandsOnTheTerrainInsteadOfFallingThroughIt();
    testABallOnASlopeRollsDownhill();
    testWithoutTheColliderItStillFallsThrough();
    testTerrainThatHasBeenMovedAndScaledStillHoldsThings();
    testABuriedBodyIsPushedBackOutOfTheGround();
    testTheGroundCheckSeesTerrain();
    testARayStopsAtTheSurfaceAndNotAtTheBoundingBox();
    testOverlapSphereSeesTerrain();
    testTerrainNeverMovesEvenWithARigidBodyOnIt();

    testRestitutionTakesTheBouncierSurface();
    testRestitutionCannotGainEnergy();
    testFrictionIsZeroIfEitherSurfaceIsIce();
    testABouncierBallReboundsHigher();
    testDampingSlowsABodyWithNothingTouchingIt();

    testAngularVelocityTurnsTheTransform();
    testFrozenRotationNeverTurns();
    testAngularDampingSlowsSpin();
    testAnOffCentreImpactCreatesSpin();
    testACentredImpactCreatesNoSpin();
    testSpinDoesNotAppearFromNothing();
    testALongBoxIsHarderToTipAboutItsLongAxis();
}

TEST_MAIN("test_physics", 160)
