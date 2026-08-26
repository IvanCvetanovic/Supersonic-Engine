#include "WolfBrigadeLayer.hpp"

#include <cmath>

#include "core/Components.hpp"
#include "core/Log.hpp"

using namespace Supersonic;

namespace WolfBrigade {

namespace {

// The lane, in world units.
//
// Godot's is 6000 x 1080 px showing 1920 at a time (world.json, camera_controller.gd).
// Divided by 100 so a unit body - 30x40 px for a soldier - is a sensible size in
// an engine whose default cube is one unit across. The ratio is what matters;
// Phase 1's orthographic camera is where pixels become the authored unit again.
constexpr float kLaneLength = 60.0f;
constexpr float kGroundY = 0.0f;

// A soldier is 30x40 px in units.json. Everything else is scaled from that.
constexpr float kBodyWidth = 0.30f;
constexpr float kBodyHeight = 0.40f;

// The HP bar is a fixed 40x6 px above the body (unit.gd:16).
constexpr float kBarWidth = 0.40f;
constexpr float kBarHeight = 0.06f;

// Quads are thin cubes. There is no quad primitive yet - item 1.3 in the plan -
// and a flattened cube is the same number of draws, which is what is being
// measured.
constexpr float kQuadDepth = 0.02f;

// Everything is on ONE plane now.
//
// Phase 0 nudged each layer 0.01 toward the camera so the depth buffer would
// order them, and said in this comment that it was a shortcut. Item 1.4 removed
// the need for it: RenderableComponent::sortKey orders coplanar surfaces the
// way Godot's child index does, so the z-stagger is gone and the ordering is
// authored rather than smuggled in through geometry.
//
// This matters beyond tidiness. The stagger meant a "flat" 2D scene was
// secretly 3D, so anything that measured depth - picking, culling, a future
// shadow - saw a lane that was several centimetres thick.
constexpr float kQuadPlane = 0.0f;

glm::vec3 unitColour(int index) {
    // raider red, soldier blue, worker amber - close enough to units.json to
    // tell at a glance whether the lane is populated correctly.
    switch (index % 3) {
        case 0: return glm::vec3(0.78f, 0.28f, 0.24f);
        case 1: return glm::vec3(0.30f, 0.52f, 0.85f);
        default: return glm::vec3(0.85f, 0.68f, 0.28f);
    }
}

} // namespace

entt::entity WolfBrigadeLayer::makeQuad(entt::registry& registry, const char* tag,
                                        const glm::vec3& position, const glm::vec3& size,
                                        const glm::vec3& colour, int32_t layer, bool unlit) {
    const auto entity = registry.create();
    registry.emplace<TagComponent>(entity, tag);

    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = position;
    transform.scale = size;

    auto& mesh = registry.emplace<MeshComponent>(entity);
    // Quad, not Cube. A flattened cube is the same number of draws but its
    // corners are rainbow by design, so a flat colour came out as a gradient -
    // which a screenshot caught and no amount of reading would have.
    mesh.primitiveType = "Quad";

    auto& material = registry.emplace<MaterialComponent>(entity);
    material.albedoColor = glm::vec4(colour, 1.0f);

    // UNLIT, which is what a ColorRect is. Phase 0 faked this with an emissive
    // material because the engine had no unlit path; item 1.2 built one, so the
    // fake is gone and the quad now genuinely skips lighting, ambient, IBL,
    // shadows and fog rather than merely surviving them.
    material.unlit = unlit;

    auto& renderable = registry.emplace<RenderableComponent>(entity);
    renderable.sortKey = layer;
    return entity;
}

void WolfBrigadeLayer::OnAttach(entt::registry& registry) {
    // The game owns the world, so it clears what the editor loaded.
    //
    // This is what a standalone game does and it is the honest thing to prove:
    // a layer that only ever ADDS to somebody else's scene has not shown that a
    // game can live here. It also makes the spike independent of which scene
    // happens to be on disk.
    registry.clear();

    // A camera looking down the lane from the side. Wolf Brigade's is a
    // Camera2D with no zoom, panning along x over a 6000-wide world; this is
    // the perspective stand-in, pulled back far enough to see a stretch of it.
    const auto camera = registry.create();
    registry.emplace<TagComponent>(camera, "Lane Camera");
    auto& cameraTransform = registry.emplace<TransformComponent>(camera);
    cameraTransform.position = glm::vec3(0.0f, 1.4f, 12.0f);

    auto& view = registry.emplace<CameraComponent>(camera);
    view.position = cameraTransform.position;
    view.yaw = -90.0f;
    // Level, because a side-scroller looks straight at its lane. Phase 0 tilted
    // 4 degrees down to make a perspective view legible; an orthographic one
    // does not need the help.
    view.pitch = 0.0f;
    view.isPrimary = true;

    // ORTHOGRAPHIC, which is item 1.1 and the reason the lane stopped bending
    // away at its edges. orthoHeight is the world height the viewport spans -
    // Godot authors 1080 px against a 1920x1080 viewport, and this is the same
    // promise in world units.
    view.projection = CameraComponent::Projection::Orthographic;
    view.orthoHeight = 4.0f;

    // A light, because emissive alone leaves the ground unreadable and the
    // ground is what makes the lane look like a lane.
    const auto sun = registry.create();
    registry.emplace<TagComponent>(sun, "Sun");
    registry.emplace<TransformComponent>(sun);
    auto& light = registry.emplace<LightComponent>(sun);
    light.type = 0;
    light.direction = glm::vec3(0.4f, 1.0f, 0.6f);
    light.intensity = m_sunIntensity;

    // The ground, deliberately LIT while everything else is not.
    //
    // It is the control. If the sun changes and the ground does not, the test
    // below is measuring nothing - so the one surface that must react is kept
    // reacting on purpose.
    makeQuad(registry, "Ground", glm::vec3(0.0f, kGroundY - 1.4f, kQuadPlane),
             glm::vec3(kLaneLength, 2.8f, kQuadDepth), glm::vec3(0.13f, 0.15f, 0.19f),
             /*layer=*/-1, /*unlit=*/false);

    // Units along the lane, five drawables each, to the requested count.
    const int unitCount = m_requested / 5;
    m_units.reserve(static_cast<size_t>(unitCount));

    for (int i = 0; i < unitCount; ++i) {
        const float t = unitCount > 1 ? static_cast<float>(i) / static_cast<float>(unitCount - 1)
                                      : 0.5f;
        const float x = (t - 0.5f) * kLaneLength;
        const glm::vec3 colour = unitColour(i);

        Unit unit;
        unit.laneX = x;
        // Alternating directions, so the lane reads as two sides meeting.
        unit.speed = (i % 2 == 0) ? 0.6f : -0.6f;
        unit.health = 0.35f + 0.65f * static_cast<float>((i * 37) % 100) / 100.0f;

        const float feet = kGroundY;
        const float mid = feet + kBodyHeight * 0.5f;

        // Ordered back to front exactly as unit.tscn documents its children:
        // SelectionRing -> Body -> HPBar/Bg -> HPBar/Fill -> Label.
        unit.ring = makeQuad(registry, "SelectionRing", glm::vec3(x, mid, kQuadPlane),
                             glm::vec3(kBodyWidth * 1.35f, kBodyHeight * 1.15f, kQuadDepth),
                             glm::vec3(0.95f, 0.85f, 0.35f), /*layer=*/0);
        unit.body = makeQuad(registry, "Body", glm::vec3(x, mid, kQuadPlane),
                             glm::vec3(kBodyWidth, kBodyHeight, kQuadDepth), colour,
                             /*layer=*/1);
        unit.barBg = makeQuad(registry, "HPBar/Bg",
                              glm::vec3(x, feet + kBodyHeight + 0.10f, kQuadPlane),
                              glm::vec3(kBarWidth, kBarHeight, kQuadDepth),
                              glm::vec3(0.10f, 0.10f, 0.12f), /*layer=*/2);
        unit.barFill = makeQuad(registry, "HPBar/Fill",
                                glm::vec3(x, feet + kBodyHeight + 0.10f, kQuadPlane),
                                glm::vec3(kBarWidth * unit.health, kBarHeight, kQuadDepth),
                                glm::vec3(0.35f, 0.80f, 0.35f), /*layer=*/3);
        // Stands in for the Label. Phase 2 makes it text; this is here so the
        // drawable count is honest.
        unit.label = makeQuad(registry, "Label",
                              glm::vec3(x, feet + kBodyHeight + 0.20f, kQuadPlane),
                              glm::vec3(kBarWidth * 0.7f, kBarHeight * 1.2f, kQuadDepth),
                              glm::vec3(0.75f, 0.75f, 0.80f), /*layer=*/4);

        m_units.push_back(unit);
    }

    SUPERSONIC_LOG_INFO("WolfBrigade")
        << "Lane built: " << m_units.size() << " units, "
        << (m_units.size() * 5 + 1) << " drawables." << std::endl;
}

void WolfBrigadeLayer::OnUpdate(entt::registry& registry, float deltaTime) {
    m_elapsed += deltaTime;

    // Movement along x only, because the game is a strictly 1D lane
    // (unit.gd:313). Nothing here is the real simulation - Phase 3 is - it
    // moves so that a screenshot of frame 40 differs from frame 1, which is
    // what makes the measurement a measurement rather than a static image.
    for (Unit& unit : m_units) {
        unit.laneX += unit.speed * deltaTime;
        const float half = kLaneLength * 0.5f;
        if (unit.laneX > half) unit.laneX -= kLaneLength;
        if (unit.laneX < -half) unit.laneX += kLaneLength;

        const auto move = [&](entt::entity entity) {
            if (entity == entt::null || !registry.valid(entity)) return;
            registry.get<TransformComponent>(entity).position.x = unit.laneX;
        };
        move(unit.ring);
        move(unit.body);
        move(unit.barBg);
        move(unit.barFill);
        move(unit.label);
    }
}

} // namespace WolfBrigade
