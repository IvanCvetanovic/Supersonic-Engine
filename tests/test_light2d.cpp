// Regression tests for 2D point lights: the gather that packs Light2DComponents
// for scene binding 12, and the light loop of shader.frag's shadeSprite2D through
// its CPU transliteration, Light2D::Contribution.
//
// Every failure here draws something plausible. A normal read along the wrong
// axis lights a wall from the far side of its torch, and the room still looks
// lit. A light packed at its transform's z rather than its height is either
// blinding or absent, depending on the sprite's draw slot. A light over the cap
// that is dropped without a count is a darker corner nobody can account for.
//
// Pure arithmetic and a registry, no Vulkan and no files, so this suite needs no
// working directory and cannot skip. What the SHADER declares (the binding, the
// cap, the mask's width) is held to these constants by test_materials, which
// already reads shader.frag.

#include "TestHarness.hpp"
#include "core/Components.hpp"
#include "core/Light2D.hpp"

#include <cmath>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

using namespace Supersonic;

namespace {

constexpr float kHalfPi = 1.5707963267948966f;

// The normal map's texels, as the bytes a 2D artist paints them, divided by 255.
// A flat map is (128, 128, 255); image-right is (255, 128, 128). 0.5 rather than
// 128/255 keeps the decoded axes exact, which is what the checks below compare.
const glm::vec3 kFlat{0.5f, 0.5f, 1.0f};
const glm::vec3 kImageRight{1.0f, 0.5f, 0.5f};
const glm::vec3 kImageUp{0.5f, 1.0f, 0.5f};   // green up in the map's own convention

const glm::vec3 kWhite{1.0f};

GpuLight2D lightAt(const glm::vec3& position, float range, uint32_t layers = 1u) {
    GpuLight2D light;
    light.position = position;
    light.range = range;
    light.color = kWhite;
    light.layers = layers;
    return light;
}

// A sprite's model matrix as the renderer builds it, from the transform it
// draws with. Through getModelMatrix rather than glm::rotate, so a rotation or a
// mirror here means what it means to a quad on screen.
glm::mat4 spriteModel(float rotationZ, float scaleX = 1.0f) {
    TransformComponent transform;
    transform.rotation = glm::vec3(0.0f, 0.0f, rotationZ);
    transform.scale = glm::vec3(scaleX, 1.0f, 1.0f);
    return transform.getModelMatrix();
}

// The red channel of one light's add on a white texel with a white tint: with
// every colour at 1 it is exactly clamp(attenuation * facing).
float addOf(const GpuLight2D& light, const glm::vec3& surface, const glm::vec3& normal,
            uint8_t mask = 1) {
    return Light2D::Contribution(light, mask, surface, normal, kWhite).r;
}

} // namespace

// --- the gather -------------------------------------------------------------

static void testTheGatherPacksWhatTheShaderReads() {
    entt::registry registry;
    const auto entity = registry.create();
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(2.5f, -1.25f, -1.5f);   // a draw slot's z, not a height

    auto& light = registry.emplace<Light2DComponent>(entity);
    light.color = glm::vec3(1.0f, 0.5f, 0.1f);
    light.intensity = 3.0f;
    light.range = 6.0f;
    light.height = 0.12f;
    light.layers = 0b10;

    std::vector<GpuLight2D> out;
    uint32_t dropped = 99;
    CHECK_EQ(Light2D::GatherLights2D(registry, out, kMaxLights2D, &dropped), 1u);
    CHECK_EQ(dropped, 0u);
    CHECK_EQ(out.size(), size_t(1));
    if (out.empty()) return;

    // x and y are the entity's; z is the HEIGHT, and the transform's -1.5 is
    // nowhere in the record.
    CHECK_NEAR(out[0].position.x, 2.5f);
    CHECK_NEAR(out[0].position.y, -1.25f);
    CHECK_NEAR(out[0].position.z, 0.12f);
    CHECK_NEAR(out[0].range, 6.0f);
    // colour x intensity, folded here so the shader multiplies once. Above 1 is
    // kept: the shader clamps each light's add, not its colour.
    CHECK_NEAR(out[0].color.r, 3.0f);
    CHECK_NEAR(out[0].color.g, 1.5f);
    CHECK_NEAR(out[0].color.b, 0.3f);
    CHECK_EQ(out[0].layers, 2u);
}

static void testAParentedLightIsWhereItsParentPutIt() {
    // The bug LightComponent once had: reading the LOCAL transform lit the world
    // origin while the character carrying the lamp walked away from it.
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = glm::vec3(0.5f, 0.5f, 0.0f);
    auto& world = registry.emplace<WorldTransformComponent>(entity);
    world.matrix = glm::mat4(1.0f);
    world.matrix[3] = glm::vec4(10.0f, 20.0f, 3.0f, 1.0f);
    registry.emplace<Light2DComponent>(entity).height = 0.25f;

    std::vector<GpuLight2D> out;
    CHECK_EQ(Light2D::GatherLights2D(registry, out, kMaxLights2D), 1u);
    if (out.empty()) return;
    CHECK_NEAR(out[0].position.x, 10.0f);
    CHECK_NEAR(out[0].position.y, 20.0f);
    CHECK_NEAR(out[0].position.z, 0.25f);   // still the height, not the world z
}

static void testAZeroColourOrADisabledLightIsNotPacked() {
    entt::registry registry;
    const auto black = registry.create();
    registry.emplace<TransformComponent>(black);
    registry.emplace<Light2DComponent>(black).color = glm::vec3(0.0f);

    const auto dark = registry.create();
    registry.emplace<TransformComponent>(dark);
    registry.emplace<Light2DComponent>(dark).intensity = 0.0f;   // folds to black

    const auto off = registry.create();
    registry.emplace<TransformComponent>(off);
    registry.emplace<Light2DComponent>(off).enabled = false;

    // One channel is not zero colour: a pure red lamp is a lamp.
    const auto red = registry.create();
    registry.emplace<TransformComponent>(red);
    registry.emplace<Light2DComponent>(red).color = glm::vec3(1.0f, 0.0f, 0.0f);

    std::vector<GpuLight2D> out;
    out.resize(5);   // stale entries must not survive the gather
    uint32_t dropped = 99;
    CHECK_EQ(Light2D::GatherLights2D(registry, out, kMaxLights2D, &dropped), 1u);
    CHECK_EQ(out.size(), size_t(1));
    // Left out is not dropped: nothing was lost, so nothing is to be reported.
    CHECK_EQ(dropped, 0u);
    if (!out.empty()) CHECK_NEAR(out[0].color.r, 1.0f);
}

static void testTheCapDropsAndCounts() {
    entt::registry registry;
    for (int i = 0; i < 70; ++i) {
        const auto entity = registry.create();
        registry.emplace<TransformComponent>(entity).position.x = static_cast<float>(i);
        registry.emplace<Light2DComponent>(entity);
    }
    // A black one among them is left out before the cap is counted, so it can
    // neither take a slot nor be reported as lost.
    const auto black = registry.create();
    registry.emplace<TransformComponent>(black);
    registry.emplace<Light2DComponent>(black).color = glm::vec3(0.0f);

    std::vector<GpuLight2D> out;
    uint32_t dropped = 0;
    CHECK_EQ(Light2D::GatherLights2D(registry, out, kMaxLights2D, &dropped), kMaxLights2D);
    CHECK_EQ(out.size(), size_t(kMaxLights2D));
    CHECK_EQ(dropped, 6u);

    CHECK_EQ(Light2D::GatherLights2D(registry, out, 3, &dropped), 3u);
    CHECK_EQ(dropped, 67u);

    // No lights at all: nothing written and nothing dropped, and the count of a
    // previous call does not linger.
    entt::registry empty;
    dropped = 42;
    CHECK_EQ(Light2D::GatherLights2D(empty, out, kMaxLights2D, &dropped), 0u);
    CHECK_EQ(out.size(), size_t(0));
    CHECK_EQ(dropped, 0u);

    // Without a counter the gather still caps.
    CHECK_EQ(Light2D::GatherLights2D(registry, out, kMaxLights2D), kMaxLights2D);
}

static void testTheRecordIsTheShadersStride() {
    // The static_asserts in Light2D.hpp stop a build; these say what they stop.
    CHECK_EQ(sizeof(GpuLight2D), size_t(32));
    CHECK_EQ(sizeof(GpuLight2DHeader), size_t(16));
    CHECK_EQ(kLight2DBufferBytes, 16u + 32u * 64u);
    CHECK_EQ(kMaxLights2D, 64u);
}

// --- the light loop -----------------------------------------------------------

static void testAFlatSpriteIsLitByHeightAlone() {
    // Design E3's first pin: a flat normal, a light at height 6 over a receiver
    // at 0. The facing is then (L.z - P.z) / d = 6 / d, and the add is exactly
    // (1 - d^2/R^2) * (6/d). So a flat sprite is almost unlit except near the
    // light, which is what the original's actors look like beside a torch.
    const glm::vec3 flat = Light2D::WorldNormal(kFlat, false, spriteModel(0.0f));
    CHECK_NEAR(flat.x, 0.0f);
    CHECK_NEAR(flat.y, 0.0f);
    CHECK_NEAR(flat.z, 1.0f);

    const float range = 20.0f;
    const glm::vec3 receiver(0.0f, 0.0f, 0.0f);
    for (const float x : {0.0f, 2.0f, 5.0f, 8.0f, 12.0f, 18.0f}) {
        const GpuLight2D light = lightAt(glm::vec3(x, 0.0f, 6.0f), range);
        const float d = std::sqrt(x * x + 36.0f);
        const float expected = (1.0f - (d * d) / (range * range)) * (6.0f / d);
        CHECK_NEAR(addOf(light, receiver, flat), expected);
    }

    // Nearer is brighter, all the way in.
    const float near = addOf(lightAt(glm::vec3(1.0f, 0.0f, 6.0f), range), receiver, flat);
    const float far = addOf(lightAt(glm::vec3(10.0f, 0.0f, 6.0f), range), receiver, flat);
    CHECK(near > far);

    // The receiver's height is the other end of the same vector: raised to the
    // light's own height, a flat sprite faces it edge on and takes nothing.
    CHECK_NEAR(addOf(lightAt(glm::vec3(3.0f, 0.0f, 6.0f), range), glm::vec3(0.0f, 0.0f, 6.0f), flat),
               0.0f);
}

static void testANormalFacingImageRightIsLitFromTheRight() {
    const glm::vec3 right = Light2D::WorldNormal(kImageRight, false, spriteModel(0.0f));
    CHECK_NEAR(right.x, 1.0f);
    CHECK_NEAR(right.y, 0.0f);
    CHECK_NEAR(right.z, 0.0f);

    const glm::vec3 receiver(0.0f);
    const float fromRight = addOf(lightAt(glm::vec3(3.0f, 0.0f, 0.0f), 10.0f), receiver, right);
    const float fromLeft = addOf(lightAt(glm::vec3(-3.0f, 0.0f, 0.0f), 10.0f), receiver, right);
    CHECK_NEAR(fromRight, 1.0f - 9.0f / 100.0f);   // facing 1, attenuation 0.91
    CHECK_MSG(fromLeft == 0.0f,
              "a light behind the surface adds exactly nothing: the negative term is clamped, "
              "not subtracted");
    // And not from above or below either: y is the other axis of the map.
    CHECK(addOf(lightAt(glm::vec3(0.0f, 3.0f, 0.0f), 10.0f), receiver, right) == 0.0f);
}

static void testARotatedSpriteIsLitFromTheSideItTurnedTo() {
    // The same texel on a sprite turned +90 degrees about z. The model's first
    // column, the sprite's own right, now points along world +y, so the texel
    // that faced image-right faces up and is lit from above, and no longer from
    // the right.
    //
    // This is the ENGINE frame's claim: a rotation of the transform turns the
    // normals with the quad. engine_math.md section 2.6 measured the original's
    // rotated lightmaps against its drawing rotation (MAE 0.80, and correlation
    // -0.107 for the opposite sense); that the port hands the engine the angle in
    // that sense is the game's to show when it feeds its sprites' rotations (G5).
    const glm::mat4 turned = spriteModel(kHalfPi);
    CHECK_NEAR(turned[0].x, 0.0f);
    CHECK_NEAR(turned[0].y, 1.0f);

    const glm::vec3 normal = Light2D::WorldNormal(kImageRight, false, turned);
    CHECK_NEAR(normal.x, 0.0f);
    CHECK_NEAR(normal.y, 1.0f);

    const glm::vec3 receiver(0.0f);
    const float fromAbove = addOf(lightAt(glm::vec3(0.0f, 3.0f, 0.0f), 10.0f), receiver, normal);
    const float fromRight = addOf(lightAt(glm::vec3(3.0f, 0.0f, 0.0f), 10.0f), receiver, normal);
    const float fromBelow = addOf(lightAt(glm::vec3(0.0f, -3.0f, 0.0f), 10.0f), receiver, normal);
    CHECK_NEAR(fromAbove, 0.91f);
    CHECK_NEAR(fromRight, 0.0f);   // sin/cos of a float quarter turn leave ~1e-8 of facing
    CHECK_MSG(fromBelow == 0.0f, "and from the side it turned away from, nothing");

    // Turned the other way, the same texel faces down.
    const glm::vec3 back = Light2D::WorldNormal(kImageRight, false, spriteModel(-kHalfPi));
    CHECK_NEAR(back.y, -1.0f);
    CHECK_NEAR(addOf(lightAt(glm::vec3(0.0f, -3.0f, 0.0f), 10.0f), receiver, back), 0.91f);

    // A scaled sprite turns its normals without stretching them: the axes are
    // normalised, so a quad four units wide lights like a quad one unit wide.
    glm::mat4 wide = spriteModel(0.0f);
    wide[0] *= 4.0f;
    CHECK_NEAR(Light2D::WorldNormal(kImageRight, false, wide).x, 1.0f);
}

static void testAMirroredSpriteFlipsX() {
    // A sprite drawn flipped (scale.x = -1): its image-right is world left, so
    // the same texel is lit from -x and no longer from +x. The normal's y and z
    // are untouched by a horizontal mirror.
    const glm::mat4 mirrored = spriteModel(0.0f, -1.0f);
    const glm::vec3 normal = Light2D::WorldNormal(kImageRight, false, mirrored);
    CHECK_NEAR(normal.x, -1.0f);
    CHECK_NEAR(normal.y, 0.0f);

    const glm::vec3 receiver(0.0f);
    CHECK_NEAR(addOf(lightAt(glm::vec3(-3.0f, 0.0f, 0.0f), 10.0f), receiver, normal), 0.91f);
    CHECK(addOf(lightAt(glm::vec3(3.0f, 0.0f, 0.0f), 10.0f), receiver, normal) == 0.0f);

    const glm::vec3 up = Light2D::WorldNormal(kImageUp, false, mirrored);
    CHECK_NEAR(up.x, 0.0f);
    CHECK_NEAR(up.y, 1.0f);
}

static void testAMapWhoseGreenPointsDownIsReadDown() {
    // kNormalYDown: a map painted with green growing down the image (the
    // original's convention, engine_math.md section 4.5), in an engine whose y
    // is up. Green above 0.5 then faces DOWN, and without the switch it would
    // face up and light every ledge from the wrong side.
    const glm::mat4 model = spriteModel(0.0f);
    const glm::vec3 asPainted = Light2D::WorldNormal(kImageUp, false, model);
    const glm::vec3 yDown = Light2D::WorldNormal(kImageUp, true, model);
    CHECK_NEAR(asPainted.y, 1.0f);
    CHECK_NEAR(yDown.y, -1.0f);

    const glm::vec3 receiver(0.0f);
    CHECK_NEAR(addOf(lightAt(glm::vec3(0.0f, -3.0f, 0.0f), 10.0f), receiver, yDown), 0.91f);
    CHECK(addOf(lightAt(glm::vec3(0.0f, 3.0f, 0.0f), 10.0f), receiver, yDown) == 0.0f);

    // Red is untouched by the switch.
    CHECK_NEAR(Light2D::WorldNormal(kImageRight, true, model).x, 1.0f);
}

static void testTheNormalIsNotRenormalised() {
    // A texel of (0.75, 0.5, 0.5) decodes to half a unit along x, and that half
    // is part of the look: the original's shader does not renormalise, so the
    // add is half of what a unit normal would give.
    const glm::vec3 half = Light2D::WorldNormal(glm::vec3(0.75f, 0.5f, 0.5f), false, spriteModel(0.0f));
    CHECK_NEAR(glm::length(half), 0.5f);
    const GpuLight2D light = lightAt(glm::vec3(3.0f, 0.0f, 0.0f), 10.0f);
    CHECK_NEAR(addOf(light, glm::vec3(0.0f), half), 0.5f * 0.91f);
}

static void testNothingReachesAtOrBeyondTheRange() {
    const glm::vec3 flat = Light2D::WorldNormal(kFlat, false, spriteModel(0.0f));
    const glm::vec3 receiver(0.0f);

    // Straight above, so the facing is 1 and only the falloff decides.
    CHECK_MSG(addOf(lightAt(glm::vec3(0.0f, 0.0f, 5.0f), 5.0f), receiver, flat) == 0.0f,
              "exactly at the range the falloff is zero, and the loop skips it");
    CHECK(addOf(lightAt(glm::vec3(0.0f, 0.0f, 5.001f), 5.0f), receiver, flat) == 0.0f);
    CHECK(addOf(lightAt(glm::vec3(0.0f, 0.0f, 50.0f), 5.0f), receiver, flat) == 0.0f);
    // Diagonally past it too: d is three-dimensional.
    CHECK(addOf(lightAt(glm::vec3(3.0f, 3.0f, 3.0f), 5.0f), receiver, flat) == 0.0f);

    // And past it BEHIND the surface, which is the case the clamp alone would get
    // wrong: 1 - d2/r2 is negative there, the facing is negative, and their
    // product is a positive light from nowhere. The range test is what stops it.
    CHECK_MSG(addOf(lightAt(glm::vec3(0.0f, 0.0f, -6.0f), 5.0f), receiver, flat) == 0.0f,
              "a light beyond the range and behind the surface adds nothing");

    // Just inside, it is small and not zero: the falloff is continuous at R.
    const float inside = addOf(lightAt(glm::vec3(0.0f, 0.0f, 4.99f), 5.0f), receiver, flat);
    CHECK(inside > 0.0f);
    CHECK_NEAR(inside, 1.0f - (4.99f * 4.99f) / 25.0f);

    // A light sitting on the fragment divides by nothing rather than by zero.
    const glm::vec3 onTop = Light2D::Contribution(lightAt(receiver, 5.0f), 1, receiver, flat, kWhite);
    CHECK(std::isfinite(onTop.r) && std::isfinite(onTop.g) && std::isfinite(onTop.b));
    CHECK_NEAR(onTop.r, 0.0f);
}

static void testOnlyTheLayersInTheMaskLight() {
    const glm::vec3 flat = Light2D::WorldNormal(kFlat, false, spriteModel(0.0f));
    const glm::vec3 receiver(0.0f);
    const GpuLight2D staticLayer = lightAt(glm::vec3(0.0f, 0.0f, 2.0f), 5.0f, 0b10);

    CHECK(addOf(staticLayer, receiver, flat, 0b10) > 0.0f);
    CHECK(addOf(staticLayer, receiver, flat, 0b11) > 0.0f);
    CHECK(addOf(staticLayer, receiver, flat, 0b01) == 0.0f);
    CHECK_MSG(addOf(staticLayer, receiver, flat, 0) == 0.0f, "a mask of zero takes no light");
    CHECK(addOf(lightAt(glm::vec3(0.0f, 0.0f, 2.0f), 5.0f, 0x80), receiver, flat, 0x80) > 0.0f);
}

static void testTheAddIsTheTintTimesTheColourAndClampedPerChannel() {
    const glm::vec3 flat = Light2D::WorldNormal(kFlat, false, spriteModel(0.0f));
    const glm::vec3 receiver(0.0f);

    // Straight above at a quarter of the range: facing 1, attenuation 15/16.
    GpuLight2D light = lightAt(glm::vec3(0.0f, 0.0f, 1.0f), 4.0f);
    light.color = glm::vec3(3.0f, 0.5f, 0.1f);
    const glm::vec3 tint(0.4f, 0.8f, 1.0f);
    const glm::vec3 add = Light2D::Contribution(light, 1, receiver, flat, tint);
    const float geometry = 15.0f / 16.0f;
    CHECK_NEAR(add.r, 1.0f);                        // 0.4 x 3 x 0.9375 = 1.125, clamped
    CHECK_NEAR(add.g, 0.8f * 0.5f * geometry);
    CHECK_NEAR(add.b, 1.0f * 0.1f * geometry);

    // A black texel takes no light whatever the lamp, which is why the shader
    // masks a transparent texel's colour to zero before it gets here.
    const glm::vec3 none = Light2D::Contribution(light, 1, receiver, flat, glm::vec3(0.0f));
    CHECK(none == glm::vec3(0.0f));
}

static void runTests() {
    testTheGatherPacksWhatTheShaderReads();
    testAParentedLightIsWhereItsParentPutIt();
    testAZeroColourOrADisabledLightIsNotPacked();
    testTheCapDropsAndCounts();
    testTheRecordIsTheShadersStride();

    testAFlatSpriteIsLitByHeightAlone();
    testANormalFacingImageRightIsLitFromTheRight();
    testARotatedSpriteIsLitFromTheSideItTurnedTo();
    testAMirroredSpriteFlipsX();
    testAMapWhoseGreenPointsDownIsReadDown();
    testTheNormalIsNotRenormalised();
    testNothingReachesAtOrBeyondTheRange();
    testOnlyTheLayersInTheMaskLight();
    testTheAddIsTheTintTimesTheColourAndClampedPerChannel();
}

TEST_MAIN("test_light2d", 80)
