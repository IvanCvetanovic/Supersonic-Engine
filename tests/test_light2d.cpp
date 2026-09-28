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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
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

// --- the header's eye ----------------------------------------------------------

static void testTheHeaderCarriesTheFramesEye() {
    // Two of the header's spare words, so the record and the array behind it
    // do not move: the offsets are the layout test_materials holds the shader to.
    CHECK_EQ(offsetof(GpuLight2DHeader, count), size_t(0));
    CHECK_EQ(offsetof(GpuLight2DHeader, eyeMirrorY), size_t(4));
    CHECK_EQ(offsetof(GpuLight2DHeader, eyeHeight), size_t(8));
    CHECK_EQ(sizeof(GpuLight2DHeader), size_t(16));

    // Without a Light2DEye the words are the zeros they were as padding, so a
    // scene that never heard of the eye uploads the bytes it uploaded before.
    entt::registry registry;
    GpuLight2DHeader header = Light2D::MakeHeader(registry, 3);
    CHECK_EQ(header.count, 3u);
    CHECK_MSG(header.eyeMirrorY == 0.0f && header.eyeHeight == 0.0f && header.passAlphaIntensity == 0.0f,
              "no eye and no alpha test in the context is zeros, as the padding was");

    // Penumbra's: camera top at y 240, a 768-pixel screen.
    registry.ctx().emplace<Light2DEye>(Light2DEye{-(240.0f + 0.75f * 768.0f), 768.0f});
    header = Light2D::MakeHeader(registry, 0);
    CHECK_EQ(header.count, 0u);
    CHECK_NEAR(header.eyeMirrorY, -816.0f);
    CHECK_NEAR(header.eyeHeight, 768.0f);
}

// --- standing up ---------------------------------------------------------------

static void testAStandingSpriteFacesDownTheScreen() {
    // Ethanon's ET_VERTICAL: a flat texel faces +y in its y-down pixels, down
    // the screen. In the engine's y-up world that is -y, and a light has to be
    // BELOW the sprite's base line on screen to reach it.
    const glm::vec3 flat = Light2D::WorldNormal(kFlat, true, spriteModel(0.0f));
    const Light2D::StoodUp up = Light2D::StandUp(glm::vec3(3.0f, 20.0f, 0.0f), flat, 0.0f);
    CHECK_NEAR(up.normal.x, 0.0f);
    CHECK_NEAR(up.normal.y, -1.0f);
    CHECK_NEAR(up.normal.z, 0.0f);

    // The fragment 20 units up the sprite is 20 units HIGH, on the base line.
    CHECK_NEAR(up.point.x, 3.0f);
    CHECK_NEAR(up.point.y, 0.0f);
    CHECK_NEAR(up.point.z, 20.0f);

    const float range = 50.0f;
    CHECK_NEAR(addOf(lightAt(glm::vec3(3.0f, -10.0f, 20.0f), range), up.point, up.normal),
               1.0f - 100.0f / 2500.0f);
    CHECK_MSG(addOf(lightAt(glm::vec3(3.0f, 10.0f, 20.0f), range), up.point, up.normal) == 0.0f,
              "a light above the base line is behind a standing sprite");
    CHECK_NEAR(addOf(lightAt(glm::vec3(3.0f, 0.0f, 45.0f), range), up.point, up.normal), 0.0f);

    // Lying flat, the same texel is lit from above instead, by the light a
    // standing one turns its back on.
    CHECK(addOf(lightAt(glm::vec3(3.0f, 30.0f, 10.0f), range), glm::vec3(3.0f, 20.0f, 0.0f), flat) > 0.0f);
}

static void testAStandingSpriteTakesItsHeightFromItsRow() {
    // One sprite on the line y = -100 at height 5, and a light in front of it
    // at the height of the row 40 units up. That row faces it square on; the
    // base row sees it from above, at a slant and further away. A flat sprite
    // lights every row at one height, which is what this replaces.
    const glm::vec3 flat = Light2D::WorldNormal(kFlat, true, spriteModel(0.0f));
    const float baseY = -100.0f;
    const Light2D::StoodUp upper = Light2D::StandUp(glm::vec3(0.0f, -60.0f, 5.0f), flat, baseY);
    const Light2D::StoodUp base = Light2D::StandUp(glm::vec3(0.0f, -100.0f, 5.0f), flat, baseY);
    CHECK_NEAR(upper.point.z, 45.0f);
    CHECK_NEAR(base.point.z, 5.0f);
    CHECK_NEAR(upper.point.y, baseY);
    CHECK_NEAR(base.point.y, baseY);

    const GpuLight2D light = lightAt(glm::vec3(0.0f, -130.0f, 45.0f), 100.0f);
    CHECK_NEAR(addOf(light, upper.point, upper.normal), 1.0f - 900.0f / 10000.0f);    // facing 1
    CHECK_NEAR(addOf(light, base.point, base.normal), (1.0f - 2500.0f / 10000.0f) * 0.6f);  // 30/50
}

static void testAStandingSpriteTurnsItsPaintedNormalsWithIt() {
    const glm::mat4 model = spriteModel(0.0f);

    // Painted facing up the image: standing, it faces the sky.
    const Light2D::StoodUp sky = Light2D::StandUp(glm::vec3(0.0f), Light2D::WorldNormal(kImageUp, false, model), 0.0f);
    CHECK_NEAR(sky.normal.x, 0.0f);
    CHECK_NEAR(sky.normal.y, 0.0f);
    CHECK_NEAR(sky.normal.z, 1.0f);
    CHECK_NEAR(addOf(lightAt(glm::vec3(0.0f, 0.0f, 3.0f), 10.0f), sky.point, sky.normal), 0.91f);
    CHECK(addOf(lightAt(glm::vec3(0.0f, -3.0f, 0.0f), 10.0f), sky.point, sky.normal) == 0.0f);

    // Painted facing image-right: still right, and a mirror still flips it.
    const Light2D::StoodUp right = Light2D::StandUp(glm::vec3(0.0f), Light2D::WorldNormal(kImageRight, false, model), 0.0f);
    CHECK_NEAR(right.normal.x, 1.0f);
    CHECK_NEAR(right.normal.y, 0.0f);
    const Light2D::StoodUp mirrored = Light2D::StandUp(
        glm::vec3(0.0f), Light2D::WorldNormal(kImageRight, false, spriteModel(0.0f, -1.0f)), 0.0f);
    CHECK_NEAR(mirrored.normal.x, -1.0f);

    // Not renormalised by standing up, any more than lying down: a rotation.
    const glm::vec3 half = Light2D::WorldNormal(glm::vec3(0.75f, 0.5f, 0.5f), false, model);
    CHECK_NEAR(glm::length(Light2D::StandUp(glm::vec3(0.0f), half, 0.0f).normal), 0.5f);
}

// --- the highlight ---------------------------------------------------------------

static void testAHighlightIsBlinnFromTheEyeTimesTheGloss() {
    // A flat texel at the origin, a light straight above it at height 4 and an
    // eye straight above it too (mirrorY 0 puts the eye's y at -L.y = 0): the
    // half vector IS the normal, so the highlight is the gloss at full shine.
    const glm::vec3 flat = Light2D::WorldNormal(kFlat, false, spriteModel(0.0f));
    GpuLight2D light = lightAt(glm::vec3(0.0f, 0.0f, 4.0f), 8.0f);
    light.color = glm::vec3(0.5f, 0.25f, 0.125f);

    Light2D::Highlight highlight;
    highlight.gloss = glm::vec3(0.5f, 1.0f, 1.0f);
    highlight.power = 50.0f;
    highlight.eyeMirrorY = 0.0f;
    highlight.eyeHeight = 768.0f;

    const glm::vec3 eye = Light2D::EyeFor(light.position, highlight.eyeMirrorY, highlight.eyeHeight);
    CHECK_NEAR(eye.x, 0.0f);
    CHECK_NEAR(eye.y, 0.0f);
    CHECK_NEAR(eye.z, 768.0f);

    const glm::vec3 tint(0.2f);
    const float attenuation = 1.0f - 16.0f / 64.0f;
    const glm::vec3 add = Light2D::SpecularContribution(light, 1, glm::vec3(0.0f), flat, tint, highlight);
    // diffuse (tint x color x att x facing 1) + color x gloss x att x shine 1
    CHECK_NEAR(add.r, 0.2f * 0.5f * attenuation + 0.5f * 0.5f * attenuation);
    CHECK_NEAR(add.g, 0.2f * 0.25f * attenuation + 0.25f * attenuation);
    CHECK_NEAR(add.b, 0.2f * 0.125f * attenuation + 0.125f * attenuation);

    // Off the peak the power decides how fast it goes: a sharper exponent
    // keeps less of the same angle. The eye at y 400, 768 up: the half vector
    // is about 14 degrees off the normal, where 0.97^20 is 0.56 and ^50 0.23.
    highlight.eyeMirrorY = 200.0f;
    const float soft = [&] {
        Light2D::Highlight h = highlight;
        h.power = 20.0f;
        return Light2D::SpecularContribution(light, 1, glm::vec3(0.0f), flat, glm::vec3(0.0f), h).g;
    }();
    const float sharp = Light2D::SpecularContribution(light, 1, glm::vec3(0.0f), flat, glm::vec3(0.0f), highlight).g;
    CHECK(soft > sharp);
    CHECK(sharp > 0.0f);
}

static void testNoGlossIsTheDiffuseTermExactly() {
    // A zero gloss (a zero strength, a black map or a transparent texel) adds
    // exactly nothing, to the bit: the shader's own zero-strength test runs the
    // old loop instead, and this is why nothing would move if it did not.
    const glm::vec3 normal = Light2D::WorldNormal(glm::vec3(0.8f, 0.4f, 0.7f), true, spriteModel(0.3f));
    const glm::vec3 surface(1.0f, -2.0f, 0.5f);
    const glm::vec3 tint(0.7f, 0.5f, 0.3f);
    Light2D::Highlight none;
    none.eyeMirrorY = -500.0f;
    none.eyeHeight = 768.0f;
    for (const glm::vec3 position : {glm::vec3(3.0f, 1.0f, 2.0f), glm::vec3(-4.0f, -1.0f, 6.0f),
                                     glm::vec3(0.5f, -3.0f, -1.0f)}) {
        GpuLight2D light = lightAt(position, 9.0f);
        light.color = glm::vec3(2.0f, 1.4f, 0.6f);
        const glm::vec3 diffuse = Light2D::Contribution(light, 1, surface, normal, tint);
        const glm::vec3 withNone = Light2D::SpecularContribution(light, 1, surface, normal, tint, none);
        CHECK(diffuse == withNone);
    }
}

static void testALightBehindTakesBackItsOwnHighlight() {
    // One clamp over the sum, as the one pass that drew both: behind the
    // surface the diffuse is negative and eats into the highlight, where two
    // clamps would have kept the highlight whole.
    const glm::vec3 right = Light2D::WorldNormal(kImageRight, false, spriteModel(0.0f));
    const GpuLight2D light = lightAt(glm::vec3(-1.0f, 0.0f, 1.0f), 10.0f);   // behind, and above
    Light2D::Highlight highlight;
    highlight.gloss = glm::vec3(1.0f);
    highlight.power = 1.0f;
    highlight.eyeMirrorY = 0.0f;
    highlight.eyeHeight = 0.0f;

    // The eye mirrors the light to y 0 at height 0: (-1, 0, 0), straight
    // behind too - so the half vector points back and there is nothing to
    // take back from.
    CHECK(Light2D::SpecularContribution(light, 1, glm::vec3(0.0f), right, glm::vec3(1.0f), highlight) ==
          glm::vec3(0.0f));

    // A texel leaning right, (0.8, 0, 0.6), with a light behind it at (-2, 0, 2)
    // - dot(L - P, N) = -0.4 - and the eye high above. The half vector is
    // between them and still on the texel's side, so there is a highlight
    // (dot(N, H) about 0.25), and the negative diffuse eats into it.
    const glm::vec3 leaning(0.8f, 0.0f, 0.6f);
    const GpuLight2D behind = lightAt(glm::vec3(-2.0f, 0.0f, 2.0f), 10.0f);
    Light2D::Highlight high = highlight;
    high.eyeHeight = 768.0f;
    CHECK_MSG(Light2D::Contribution(behind, 1, glm::vec3(0.0f), leaning, glm::vec3(1.0f)) == glm::vec3(0.0f),
              "the light is behind the texel: no diffuse on its own");
    const glm::vec3 alone = Light2D::SpecularContribution(behind, 1, glm::vec3(0.0f), leaning,
                                                          glm::vec3(0.0f), high);
    const glm::vec3 withDiffuse = Light2D::SpecularContribution(behind, 1, glm::vec3(0.0f), leaning,
                                                                glm::vec3(1.0f), high);
    const float attenuation = 1.0f - 8.0f / 100.0f;
    const float diffuse = attenuation * (-0.4f / std::sqrt(8.0f));
    CHECK(alone.r > 0.1f);
    CHECK_MSG(withDiffuse.r > 0.0f && withDiffuse.r < alone.r, "one clamp over the sum, not one per term");
    CHECK_NEAR(withDiffuse.r, alone.r + diffuse);

    // Degenerate: eye and light exactly opposite through the fragment, so
    // their unit vectors cancel. The guard gives no shine rather than a NaN.
    const GpuLight2D above = lightAt(glm::vec3(0.0f, 5.0f, 0.0f), 10.0f);
    Light2D::Highlight opposite = highlight;   // mirror 0, height 0: the eye at (0, -5, 0)
    const glm::vec3 flat = Light2D::WorldNormal(kFlat, false, spriteModel(0.0f));
    const glm::vec3 cancelled = Light2D::SpecularContribution(above, 1, glm::vec3(0.0f), flat, glm::vec3(0.0f), opposite);
    CHECK(std::isfinite(cancelled.r) && std::isfinite(cancelled.g) && std::isfinite(cancelled.b));
    CHECK_NEAR(cancelled.r, 0.0f);
}

static void testTheHighlightKeepsTheRangeAndTheMask() {
    const glm::vec3 flat = Light2D::WorldNormal(kFlat, false, spriteModel(0.0f));
    Light2D::Highlight highlight;
    highlight.gloss = glm::vec3(1.0f);
    highlight.eyeHeight = 768.0f;
    const GpuLight2D light = lightAt(glm::vec3(0.0f, 0.0f, 5.0f), 5.0f, 0b10);
    CHECK_MSG(Light2D::SpecularContribution(light, 0b10, glm::vec3(0.0f), flat, kWhite, highlight) ==
                  glm::vec3(0.0f),
              "at the range there is no highlight either");
    const GpuLight2D inside = lightAt(glm::vec3(0.0f, 0.0f, 2.0f), 5.0f, 0b10);
    CHECK(Light2D::SpecularContribution(inside, 0b10, glm::vec3(0.0f), flat, kWhite, highlight).r > 0.0f);
    CHECK(Light2D::SpecularContribution(inside, 0b01, glm::vec3(0.0f), flat, kWhite, highlight) ==
          glm::vec3(0.0f));
}

// --- held to the original -------------------------------------------------------
//
// Ethanon 0.7.12's light pass for one pixel, transliterated from the Cg the game
// shipped (hPixelLight.cg, vPixelLight.cg, pixelLightVS.cg) in ITS axes - y down
// the screen - and set against the engine's frame through the mapping a port
// uses: world (x, -y), heights as they are, normalYDown, the light's colour
// times the scene's lightIntensity, the base line at -y, the eye's mirror line
// at -(camY + 0.75 screenH), and the specular strength divided by the intensity.

namespace {

struct EthLight {
    glm::vec3 position;   // pixels, y down; z the height
    glm::vec3 color;      // raw: lightIntensity is separate in 0.7.12
    float range;
};

// n = -normalize(2 * (nm - 0.5)); vertical: normalColor.xzy, z *= -1.
glm::vec3 ethNormal(const glm::vec3& texel, bool vertical) {
    glm::vec3 n = -glm::normalize(2.0f * (texel - 0.5f));
    if (vertical) {
        n = glm::vec3(n.x, n.z, n.y);
        n.z *= -1.0f;
    }
    return n;
}

float ethAttenuation(const glm::vec3& lightVec, float range) {
    const float squaredDist = glm::dot(lightVec, lightVec);
    const float squaredRange = std::max(squaredDist, range * range);
    return 1.0f - squaredDist / squaredRange;
}

// vPixelLight main, and mainSpecular (both h and v) when `gloss` is given; the
// 8-bit target clamps the pass. hPixelLight main is left out: its * T.a is the
// engine's Alpha blend, not the light term.
glm::vec3 ethPass(const EthLight& light, float lightIntensity, const glm::vec3& pixel3D,
                  const glm::vec3& texel, bool vertical, const glm::vec3& diffuse, float diffuseAlpha,
                  const glm::vec3* gloss, float specularPower, const glm::vec3& fakeEye) {
    const glm::vec3 n = ethNormal(texel, vertical);
    const glm::vec3 lightVec = pixel3D - light.position;
    const float attenBias = ethAttenuation(lightVec, light.range);
    if (gloss == nullptr) {
        const float diffuseLight = glm::dot(glm::normalize(lightVec), n);
        return glm::clamp(diffuse * diffuseLight * attenBias * light.color * lightIntensity, 0.0f, 1.0f);
    }
    const glm::vec3 eyeVec = pixel3D - fakeEye;
    const glm::vec3 halfVec = glm::normalize(lightVec / glm::length(lightVec) + eyeVec / glm::length(eyeVec));
    const float diffuseLight = glm::dot(lightVec / glm::length(lightVec), n);
    const glm::vec3 specular =
        light.color * std::pow(glm::clamp(glm::dot(n, halfVec), 0.0f, 1.0f), specularPower);
    return glm::clamp((diffuse * diffuseLight * light.color * lightIntensity + specular * diffuseAlpha * *gloss) *
                          attenBias,
                      0.0f, 1.0f);
}

// An encoded texel whose decoded vector is `direction`, unit length, so the
// engine's not renormalising and 0.7.12's renormalising agree.
glm::vec3 texelFacing(const glm::vec3& direction) {
    return glm::normalize(direction) * 0.5f + 0.5f;
}

} // namespace

static void testAStandingSpriteIsLitAsVPixelLightLitIt() {
    // A barrel-like sprite standing at Ethanon (400, 336, 0): its rows from 336
    // up to 300, lit by a fire at (460, 346, 16) - below the base line, so in
    // front - and one at (380, 320, 30), behind it.
    const float lightIntensity = 2.0f;
    const glm::vec3 entity(400.0f, 336.0f, 0.0f);
    const glm::vec3 diffuse(0.8f, 0.6f, 0.5f);   // texel x instance colour
    const std::vector<EthLight> lights{
        {glm::vec3(460.0f, 346.0f, 16.0f), glm::vec3(1.0f, 0.7f, 0.3f), 150.0f},
        {glm::vec3(380.0f, 320.0f, 30.0f), glm::vec3(0.5f, 0.5f, 1.0f), 120.0f},
        {glm::vec3(410.0f, 420.0f, 60.0f), glm::vec3(0.9f, 0.9f, 0.9f), 200.0f},
    };
    const std::vector<glm::vec3> texels{
        texelFacing(glm::vec3(0.0f, 0.0f, 1.0f)),     // flat
        texelFacing(glm::vec3(0.6f, 0.0f, 0.8f)),     // leaning right
        texelFacing(glm::vec3(-0.3f, 0.5f, 0.8f)),    // left and down the image
        texelFacing(glm::vec3(0.2f, -0.7f, 0.5f)),    // up the image
    };

    int compared = 0;
    int lit = 0;
    float worst = 0.0f;
    for (const EthLight& light : lights) {
        GpuLight2D engineLight;
        engineLight.position = glm::vec3(light.position.x, -light.position.y, light.position.z);
        engineLight.range = light.range;
        engineLight.color = light.color * lightIntensity;
        engineLight.layers = 1u;
        for (const glm::vec3& texel : texels) {
            for (const glm::vec2 pixel : {glm::vec2(390.0f, 336.0f), glm::vec2(405.0f, 318.0f),
                                          glm::vec2(420.0f, 300.0f)}) {
                // pixelLightVS verticalSprite_ppl: (x - ox + u w, y, z + oy - v h),
                // i.e. the row drawn at screen y is at z + (entity.y - y).
                const glm::vec3 ethPixel(pixel.x, entity.y, entity.z + (entity.y - pixel.y));
                const glm::vec3 expected = ethPass(light, lightIntensity, ethPixel, texel, true, diffuse, 1.0f,
                                                   nullptr, 0.0f, glm::vec3(0.0f));

                const glm::vec3 flat = Light2D::WorldNormal(texel, true, spriteModel(0.0f));
                const Light2D::StoodUp up =
                    Light2D::StandUp(glm::vec3(pixel.x, -pixel.y, entity.z), flat, -entity.y);
                const glm::vec3 got = Light2D::Contribution(engineLight, 1, up.point, up.normal, diffuse);

                worst = std::max(worst, glm::length(got - expected));
                ++compared;
                if (expected != glm::vec3(0.0f)) ++lit;
            }
        }
    }
    CHECK_EQ(compared, 36);
    CHECK_MSG(lit >= 12, "enough of the cases are lit for the comparison to mean something");
    CHECK_MSG(worst < 1e-4f, "every pixel within 1e-4 of vPixelLight: " + std::to_string(worst));
}

static void testAHighlightIsMainSpecularThroughThePortsMapping() {
    // Both mainSpecular variants, h and v, at two camera positions (the eye
    // moves with the camera), a partly transparent texel, gloss 0.5 grey and
    // the powers the game uses.
    const float lightIntensity = 2.0f;
    const float screenH = 768.0f;
    const float specularBrightness = 1.0f;
    const glm::vec3 diffuse(0.7f, 0.55f, 0.4f);
    const float texelAlpha = 0.6f;
    const glm::vec3 gloss(0.5f);
    const std::vector<EthLight> lights{
        {glm::vec3(520.0f, 380.0f, 40.0f), glm::vec3(1.0f, 0.7f, 0.3f), 200.0f},
        {glm::vec3(300.0f, 700.0f, 16.0f), glm::vec3(0.6f, 0.8f, 1.0f), 450.0f},
    };
    const std::vector<glm::vec3> texels{
        texelFacing(glm::vec3(0.0f, 0.0f, 1.0f)),
        texelFacing(glm::vec3(0.5f, 0.3f, 0.8f)),
        texelFacing(glm::vec3(-0.4f, -0.4f, 0.8f)),
    };

    int compared = 0;
    int shining = 0;
    float worst = 0.0f;
    for (const float cameraY : {0.0f, 240.0f}) {
        Light2D::Highlight highlight;
        highlight.eyeMirrorY = -(cameraY + 0.75f * screenH);
        highlight.eyeHeight = 768.0f;
        highlight.gloss = gloss * (specularBrightness / lightIntensity) * texelAlpha;
        for (const float power : {50.0f, 20.0f}) {
            highlight.power = power;
            for (const EthLight& light : lights) {
                GpuLight2D engineLight;
                engineLight.position = glm::vec3(light.position.x, -light.position.y, light.position.z);
                engineLight.range = light.range;
                engineLight.color = light.color * lightIntensity;
                engineLight.layers = 1u;
                // ETHFakeEyePositionManager, real time.
                const glm::vec3 fakeEye(light.position.x, 2.0f * cameraY + 1.5f * screenH - light.position.y,
                                        768.0f);
                const glm::vec3 glossColor = gloss * specularBrightness;
                for (const glm::vec3& texel : texels) {
                    // Horizontal: (x - ox + u w, y - oy + v h, z), at z 4.
                    const glm::vec3 hPixel(500.0f, 420.0f, 4.0f);
                    const glm::vec3 hExpected = ethPass(light, lightIntensity, hPixel, texel, false, diffuse,
                                                        texelAlpha, &glossColor, power, fakeEye);
                    const glm::vec3 hGot = Light2D::SpecularContribution(
                        engineLight, 1, glm::vec3(hPixel.x, -hPixel.y, hPixel.z),
                        Light2D::WorldNormal(texel, true, spriteModel(0.0f)), diffuse, highlight);
                    worst = std::max(worst, glm::length(hGot - hExpected));

                    // Vertical: standing at (480, 560, 0), the row drawn at 530.
                    const glm::vec3 vPixel(470.0f, 560.0f, 0.0f + (560.0f - 530.0f));
                    const glm::vec3 vExpected = ethPass(light, lightIntensity, vPixel, texel, true, diffuse,
                                                        texelAlpha, &glossColor, power, fakeEye);
                    const Light2D::StoodUp up = Light2D::StandUp(
                        glm::vec3(470.0f, -530.0f, 0.0f), Light2D::WorldNormal(texel, true, spriteModel(0.0f)),
                        -560.0f);
                    const glm::vec3 vGot =
                        Light2D::SpecularContribution(engineLight, 1, up.point, up.normal, diffuse, highlight);
                    worst = std::max(worst, glm::length(vGot - vExpected));

                    compared += 2;
                    if (hExpected != glm::vec3(0.0f)) ++shining;
                    if (vExpected != glm::vec3(0.0f)) ++shining;
                }
            }
        }
    }
    CHECK_EQ(compared, 48);
    CHECK_MSG(shining >= 16, "enough of the cases are lit for the comparison to mean something");
    CHECK_MSG(worst < 1e-4f, "every pixel within 1e-4 of mainSpecular: " + std::to_string(worst));
}

// --- a baked light's eye -------------------------------------------------------

static void testABakedLightIsSeenFromTheSpritesOwnEye() {
    // The gather marks a baked light above the layer byte, and only a baked one.
    entt::registry registry;
    const auto plain = registry.create();
    registry.emplace<TransformComponent>(plain);
    registry.emplace<Light2DComponent>(plain).layers = 0x02;
    const auto baked = registry.create();
    registry.emplace<TransformComponent>(baked);
    auto& bakedLight = registry.emplace<Light2DComponent>(baked);
    bakedLight.layers = 0x02;
    bakedLight.baked = true;
    std::vector<GpuLight2D> gathered;
    CHECK_EQ(Light2D::GatherLights2D(registry, gathered, kMaxLights2D), 2u);
    int plainSeen = 0;
    int bakedSeen = 0;
    for (const GpuLight2D& g : gathered) {
        if (g.layers == 0x02u) ++plainSeen;
        if (g.layers == (0x02u | kLight2DBakedBit)) ++bakedSeen;
    }
    CHECK_MSG(plainSeen == 1 && bakedSeen == 1, "a baked light carries kLight2DBakedBit, a plain one its byte alone");

    // The bit is above every mask, so which sprites a light reaches is unchanged.
    const glm::vec3 flat = Light2D::WorldNormal(kFlat, false, spriteModel(0.0f));
    GpuLight2D marked = lightAt(glm::vec3(0.0f, 0.0f, 4.0f), 8.0f, 0x02u | kLight2DBakedBit);
    CHECK(addOf(marked, glm::vec3(0.0f), flat, 0x02) == addOf(lightAt(glm::vec3(0.0f, 0.0f, 4.0f), 8.0f, 0x02u),
                                                              glm::vec3(0.0f), flat, 0x02));
    CHECK(addOf(marked, glm::vec3(0.0f), flat, 0x01) == 0.0f);

    // Ethanon's lightmap bake, a static glossy tile whose top edge is at y 256
    // and z -4, lit by a static torch: the receiver is moved to the render
    // target's corner and the eye set at (L.x, 1.5 screenH, 768) there
    // (ETHScene::GenerateLightmaps, ETHShaderManager::SetFakeEyePosition with
    // drawToTarget), which in the world is (L.x, top + 1.5 screenH, z + 768),
    // wherever the camera is.
    const float lightIntensity = 2.0f;
    const float screenH = 768.0f;
    const float top = 256.0f;
    const float tileZ = -4.0f;
    const EthLight torch{glm::vec3(300.0f, 330.0f, 20.0f), glm::vec3(1.0f, 0.7f, 0.3f), 227.5f};
    const glm::vec3 bakeEye(torch.position.x, top + 1.5f * screenH, tileZ + 768.0f);
    const glm::vec3 diffuse(0.6f, 0.5f, 0.45f);
    const glm::vec3 gloss(0.8f);

    GpuLight2D engineTorch;
    engineTorch.position = glm::vec3(torch.position.x, -torch.position.y, torch.position.z);
    engineTorch.range = torch.range;
    engineTorch.color = torch.color * lightIntensity;
    engineTorch.layers = 1u | kLight2DBakedBit;
    GpuLight2D liveTorch = engineTorch;
    liveTorch.layers = 1u;

    int compared = 0;
    int shining = 0;
    float worst = 0.0f;
    float moved = 0.0f;
    for (const glm::vec3& texel : {texelFacing(glm::vec3(0.0f, 0.0f, 1.0f)), texelFacing(glm::vec3(0.1f, 0.5f, 0.8f)),
                                   texelFacing(glm::vec3(-0.2f, 0.6f, 0.7f))}) {
        const glm::vec3 pixel(320.0f, 300.0f, tileZ);
        const glm::vec3 expected = ethPass(torch, lightIntensity, pixel, texel, false, diffuse, 1.0f, &gloss, 60.0f,
                                           bakeEye);
        const glm::vec3 normal = Light2D::WorldNormal(texel, true, spriteModel(0.0f));
        const glm::vec3 surface(pixel.x, -pixel.y, pixel.z);
        glm::vec3 first(0.0f);
        for (const float cameraY : {0.0f, 180.0f}) {
            Light2D::Highlight highlight;
            highlight.gloss = gloss / lightIntensity;
            highlight.power = 60.0f;
            highlight.eyeMirrorY = -(cameraY + 0.75f * screenH);
            highlight.eyeHeight = 768.0f;
            highlight.bakedEye = true;
            highlight.bakedEyeY = -(top + 1.5f * screenH);
            highlight.spriteHeight = tileZ;
            const glm::vec3 got =
                Light2D::SpecularContribution(engineTorch, 1, surface, normal, diffuse, highlight);
            worst = std::max(worst, glm::length(got - expected));
            if (cameraY == 0.0f) first = got;
            moved = std::max(moved, glm::length(got - first));

            // A light that is not baked is still seen from the frame's eye, and
            // a sprite without the switch sees a baked one from there too.
            Light2D::Highlight frameEye = highlight;
            frameEye.bakedEye = false;
            CHECK(Light2D::SpecularContribution(liveTorch, 1, surface, normal, diffuse, highlight) ==
                  Light2D::SpecularContribution(liveTorch, 1, surface, normal, diffuse, frameEye));
            CHECK(Light2D::SpecularContribution(engineTorch, 1, surface, normal, diffuse, frameEye) ==
                  Light2D::SpecularContribution(liveTorch, 1, surface, normal, diffuse, frameEye));
            ++compared;
            if (expected != glm::vec3(0.0f)) ++shining;
        }
    }
    CHECK_EQ(compared, 6);
    CHECK_MSG(shining >= 4, "enough of the cases are lit for the comparison to mean something");
    CHECK_MSG(worst < 1e-4f, "every pixel within 1e-4 of the bake's mainSpecular: " + std::to_string(worst));
    CHECK_MSG(moved == 0.0f, "and the highlight does not move with the camera");

    const glm::vec3 eye = Light2D::BakedEyeFor(glm::vec3(5.0f, 6.0f, 7.0f), -900.0f, -4.0f, 768.0f);
    CHECK_NEAR(eye.x, 5.0f);
    CHECK_NEAR(eye.y, -900.0f);
    CHECK_NEAR(eye.z, 764.0f);
}

// --- the light pass's alpha test ---------------------------------------------------

static void testTheLightPassIsAlphaTestedAsEthanonsWas() {
    // The frame's intensity reaches the header's last word.
    entt::registry registry;
    registry.ctx().emplace<Light2DAlphaTest>(Light2DAlphaTest{2.0f});
    CHECK_NEAR(Light2D::MakeHeader(registry, 0).passAlphaIntensity, 2.0f);

    // The pass's alpha, per variant (hPixelLight/vPixelLight main, mainSpecular).
    Light2D::PassAlpha pass;
    pass.albedoAlpha = 0.5f;
    pass.colorAlpha = 1.0f;
    pass.intensity = 2.0f;
    CHECK_NEAR(Light2D::PassAlphaOf(pass, 0.8f, 0.5f), 0.5f * 1.0f * 2.0f * 0.5f * 0.5f * 0.8f);
    pass.vertical = true;
    CHECK_NEAR(Light2D::PassAlphaOf(pass, 0.8f, 0.5f), 0.5f * 1.0f * 2.0f * 0.5f * 0.8f);
    pass.glossAlpha = 0.5f;
    CHECK_NEAR(Light2D::PassAlphaOf(pass, 0.8f, 0.5f, true, 0.25f), 0.5f * 0.5f * (0.8f * 2.0f + 0.25f * 0.5f * 2.0f));

    // An opaque texel at the very edge of a light's reach: its pass alpha,
    // facing x falloff x 2, is under 1.5/255, so it adds nothing - and the
    // colour it would have added is under a level anyway.
    const glm::vec3 flat = Light2D::WorldNormal(kFlat, false, spriteModel(0.0f));
    const GpuLight2D light = lightAt(glm::vec3(0.0f, 0.0f, 4.0f), 100.0f);
    Light2D::PassAlpha opaque;
    opaque.intensity = 2.0f;
    const glm::vec3 edge(99.9f, 0.0f, 0.0f);
    const float untested = addOf(light, edge, flat);
    CHECK_MSG(untested > 0.0f && untested < 1.0f / 255.0f, "untested, the edge adds a sliver under one level");
    CHECK(Light2D::Contribution(light, 1, edge, flat, kWhite, &opaque) == glm::vec3(0.0f));
    // Well inside the reach it is the untested add exactly.
    const glm::vec3 inside(10.0f, 0.0f, 0.0f);
    CHECK(Light2D::Contribution(light, 1, inside, flat, kWhite, &opaque) ==
          Light2D::Contribution(light, 1, inside, flat, kWhite));

    // A faint edge texel (alpha 8/255) of a standing sprite, whose pass
    // (vPixelLight main) does not weight the colour by that alpha: a light of
    // facing x falloff x 2 under 0.19 is tested away - a colour of up to 0.19 x
    // the texel, added at full weight over the background untested - and one
    // above adds. The geometry is a flat texel's; only the alpha is at issue.
    Light2D::PassAlpha faint;
    faint.albedoAlpha = 8.0f / 255.0f;
    faint.intensity = 2.0f;
    faint.vertical = true;
    const GpuLight2D weak = lightAt(glm::vec3(0.0f, 0.0f, 1.0f), 100.0f);   // grazing: facing about 1/d
    const glm::vec3 closeBy(3.0f, 0.0f, 0.0f);
    const float weakAlpha = Light2D::PassAlphaOf(faint, 1.0f / std::sqrt(10.0f), 1.0f - 10.0f / 10000.0f);
    CHECK(weakAlpha > Light2D::kLightPassAlphaRef);   // facing 0.32: the pass survives
    CHECK(Light2D::Contribution(weak, 1, closeBy, flat, kWhite, &faint) == Light2D::Contribution(weak, 1, closeBy, flat, kWhite));
    const glm::vec3 farOff(30.0f, 0.0f, 0.0f);         // facing 0.033, falloff 0.91: alpha 0.0019
    CHECK(Light2D::Contribution(weak, 1, farOff, flat, kWhite) != glm::vec3(0.0f));
    CHECK(Light2D::Contribution(weak, 1, farOff, flat, kWhite, &faint) == glm::vec3(0.0f));

    // With a highlight the highlight's own share can keep a pass the diffuse
    // alone would lose: mainSpecular's alpha adds shine x gloss alpha.
    Light2D::Highlight highlight;
    highlight.gloss = glm::vec3(1.0f);
    highlight.power = 1.0f;
    highlight.eyeMirrorY = 0.0f;
    highlight.eyeHeight = 768.0f;
    faint.glossAlpha = 1.0f;
    CHECK(Light2D::SpecularContribution(weak, 1, farOff, flat, kWhite, highlight, &faint) ==
          Light2D::SpecularContribution(weak, 1, farOff, flat, kWhite, highlight));
    faint.glossAlpha = 0.0f;
    CHECK(Light2D::SpecularContribution(weak, 1, farOff, flat, kWhite, highlight, &faint) == glm::vec3(0.0f));
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

    testTheHeaderCarriesTheFramesEye();
    testAStandingSpriteFacesDownTheScreen();
    testAStandingSpriteTakesItsHeightFromItsRow();
    testAStandingSpriteTurnsItsPaintedNormalsWithIt();
    testAHighlightIsBlinnFromTheEyeTimesTheGloss();
    testNoGlossIsTheDiffuseTermExactly();
    testALightBehindTakesBackItsOwnHighlight();
    testTheHighlightKeepsTheRangeAndTheMask();
    testAStandingSpriteIsLitAsVPixelLightLitIt();
    testAHighlightIsMainSpecularThroughThePortsMapping();
    testABakedLightIsSeenFromTheSpritesOwnEye();
    testTheLightPassIsAlphaTestedAsEthanonsWas();
}

TEST_MAIN("test_light2d", 140)
