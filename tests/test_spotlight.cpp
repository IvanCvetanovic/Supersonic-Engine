// Regression tests for spot light geometry.
//
// The cone falloff and the shadow frustum are a handful of dot products and
// one matrix, and every way of getting them wrong still produces something
// that looks like a spotlight: a cone that inverts when the angles are
// authored the wrong way round, a lamp pointing straight down that comes out
// as NaN, or a shadow frustum narrower than the light it is meant to cover -
// which lights geometry through walls at the edge of the cone.

#include "TestHarness.hpp"
#include "renderer/SpotLight.hpp"

#include <cmath>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Supersonic;

namespace {

// A point projected through the light's transform, in NDC.
glm::vec3 project(const glm::mat4& viewProj, const glm::vec3& point) {
    const glm::vec4 clip = viewProj * glm::vec4(point, 1.0f);
    if (std::abs(clip.w) < 1e-9f) return glm::vec3(0.0f);
    return glm::vec3(clip) / clip.w;
}

const float kInner = glm::radians(20.0f);
const float kOuter = glm::radians(30.0f);

} // namespace

static void testStraightAheadIsFullyLit() {
    const glm::vec3 down(0.0f, -1.0f, 0.0f);
    CHECK_NEAR(SpotLight::ConeAttenuation(down, down, kInner, kOuter), 1.0f);
}

static void testBehindTheLightIsDark() {
    const glm::vec3 down(0.0f, -1.0f, 0.0f);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    CHECK_NEAR(SpotLight::ConeAttenuation(down, up, kInner, kOuter), 0.0f);
}

static void testInsideTheInnerConeIsFullBrightness() {
    const glm::vec3 axis(0.0f, -1.0f, 0.0f);
    // 10 degrees off axis, comfortably inside the 20 degree inner cone.
    const glm::vec3 near = glm::normalize(glm::vec3(std::sin(glm::radians(10.0f)),
                                                    -std::cos(glm::radians(10.0f)), 0.0f));
    CHECK_NEAR(SpotLight::ConeAttenuation(axis, near, kInner, kOuter), 1.0f);
}

static void testOutsideTheOuterConeIsDark() {
    const glm::vec3 axis(0.0f, -1.0f, 0.0f);
    const glm::vec3 wide = glm::normalize(glm::vec3(std::sin(glm::radians(45.0f)),
                                                    -std::cos(glm::radians(45.0f)), 0.0f));
    CHECK_NEAR(SpotLight::ConeAttenuation(axis, wide, kInner, kOuter), 0.0f);
}

static void testTheFalloffIsMonotonic() {
    // Brightness must never rise as a fragment moves away from the axis. A
    // fold in the falloff shows up as a bright ring at the edge of the cone.
    const glm::vec3 axis(0.0f, -1.0f, 0.0f);
    float previous = 2.0f;

    for (float degrees = 0.0f; degrees <= 50.0f; degrees += 1.0f) {
        const float radians = glm::radians(degrees);
        const glm::vec3 direction(std::sin(radians), -std::cos(radians), 0.0f);
        const float value = SpotLight::ConeAttenuation(axis, direction, kInner, kOuter);

        CHECK_MSG(value <= previous + 1e-5f,
                  "brightness rose at " + std::to_string(degrees) + " degrees: " +
                      std::to_string(previous) + " then " + std::to_string(value));
        CHECK_MSG(value >= 0.0f && value <= 1.0f,
                  "attenuation left 0..1 at " + std::to_string(degrees) + " degrees");
        previous = value;
    }
}

static void testTheEdgeIsSoftRatherThanABinaryCutoff() {
    // Between the two angles the light has to actually fade. A cone that jumps
    // straight from lit to dark reads as a hard-edged circle crawling across
    // the floor, which is the classic look of a spotlight done wrong.
    const glm::vec3 axis(0.0f, -1.0f, 0.0f);
    const float mid = glm::radians(25.0f);
    const glm::vec3 direction(std::sin(mid), -std::cos(mid), 0.0f);

    const float value = SpotLight::ConeAttenuation(axis, direction, kInner, kOuter);
    CHECK_MSG(value > 0.05f && value < 0.95f,
              "halfway between the angles must be partly lit: got " + std::to_string(value));
}

static void testAnglesAuthoredBackwardsStillProduceACone() {
    // Inner wider than outer would divide by a negative span and invert the
    // cone, lighting everything except the middle.
    const glm::vec3 axis(0.0f, -1.0f, 0.0f);

    const float centre = SpotLight::ConeAttenuation(axis, axis, kOuter, kInner);
    CHECK_MSG(centre > 0.99f, "the middle of a cone must be lit whichever way the angles were given");

    const glm::vec3 wide = glm::normalize(glm::vec3(std::sin(glm::radians(60.0f)),
                                                    -std::cos(glm::radians(60.0f)), 0.0f));
    CHECK_MSG(SpotLight::ConeAttenuation(axis, wide, kOuter, kInner) < 0.01f,
              "and well outside it must still be dark");
}

static void testEqualAnglesGiveAHardEdgeRatherThanNaN() {
    const glm::vec3 axis(0.0f, -1.0f, 0.0f);
    const float angle = glm::radians(25.0f);

    const float inside = SpotLight::ConeAttenuation(axis, axis, angle, angle);
    CHECK_MSG(std::isfinite(inside) && inside > 0.99f, "a zero-width falloff must not divide by zero");

    const glm::vec3 outside = glm::normalize(glm::vec3(std::sin(glm::radians(40.0f)),
                                                       -std::cos(glm::radians(40.0f)), 0.0f));
    CHECK(SpotLight::ConeAttenuation(axis, outside, angle, angle) < 0.01f);
}

static void testADirectionlessLightDoesNotProduceNaN() {
    const glm::vec3 none(0.0f);
    const float value = SpotLight::ConeAttenuation(none, glm::vec3(0.0f, -1.0f, 0.0f), kInner, kOuter);
    CHECK_MSG(std::isfinite(value), "a light authored with no direction must not produce NaN");
}

static void testTheShadowFrustumCoversTheWholeCone() {
    // The frustum's field of view is the FULL angle, not the half angle. Too
    // narrow and the edge of the cone falls outside the shadow map, where
    // there is no depth to compare against and the light passes through walls.
    const glm::vec3 position(0.0f, 5.0f, 0.0f);
    const glm::vec3 direction(0.0f, -1.0f, 0.0f);
    const float range = 20.0f;

    const glm::mat4 viewProj = SpotLight::BuildViewProj(position, direction, kOuter, range);

    // A point on the very edge of the cone, five units along it.
    const float distance = 5.0f;
    const glm::vec3 edge = position +
        glm::normalize(glm::vec3(std::sin(kOuter), -std::cos(kOuter), 0.0f)) * distance;

    const glm::vec3 ndc = project(viewProj, edge);
    CHECK_MSG(std::abs(ndc.x) <= 1.0f && std::abs(ndc.y) <= 1.0f,
              "the cone's edge must fall inside the shadow map: ndc (" +
                  std::to_string(ndc.x) + ", " + std::to_string(ndc.y) + ")");
    CHECK_MSG(ndc.z >= 0.0f && ndc.z <= 1.0f, "and within its depth range");
}

static void testTheAxisProjectsToTheCentreOfTheMap() {
    const glm::vec3 position(2.0f, 6.0f, -3.0f);
    const glm::vec3 direction(0.0f, -1.0f, 0.0f);

    const glm::mat4 viewProj = SpotLight::BuildViewProj(position, direction, kOuter, 25.0f);
    const glm::vec3 ndc = project(viewProj, position + direction * 8.0f);

    CHECK_MSG(std::abs(ndc.x) < 1e-3f && std::abs(ndc.y) < 1e-3f,
              "a point straight ahead belongs at the centre: (" + std::to_string(ndc.x) +
                  ", " + std::to_string(ndc.y) + ")");
}

static void testALampPointingStraightDownIsNotDegenerate() {
    // glm::lookAt collapses when the up vector is parallel to the view
    // direction, and straight down is the most common way anyone places a
    // spot light.
    const glm::mat4 viewProj =
        SpotLight::BuildViewProj(glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
                                 kOuter, 15.0f);

    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CHECK_MSG(std::isfinite(viewProj[column][row]),
                      "a downward lamp produced a NaN transform");
        }
    }

    const glm::vec3 ndc = project(viewProj, glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK_MSG(std::abs(ndc.x) < 1e-3f && std::abs(ndc.y) < 1e-3f,
              "and the point below it belongs at the centre of the map");
}

static void testDepthGrowsWithDistanceFromTheLight() {
    // Monotonic, or the shadow comparison has no meaning.
    const glm::vec3 position(0.0f, 10.0f, 0.0f);
    const glm::vec3 direction(0.0f, -1.0f, 0.0f);
    const glm::mat4 viewProj = SpotLight::BuildViewProj(position, direction, kOuter, 30.0f);

    float previous = -1.0f;
    for (float distance = 0.5f; distance < 25.0f; distance += 0.5f) {
        const float depth = project(viewProj, position + direction * distance).z;
        CHECK_MSG(depth > previous,
                  "depth must grow with distance: " + std::to_string(depth) + " at " +
                      std::to_string(distance));
        previous = depth;
    }
}

static void testAZeroRangeLightDoesNotProduceNaN() {
    const glm::mat4 viewProj =
        SpotLight::BuildViewProj(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f), kOuter, 0.0f);

    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CHECK_MSG(std::isfinite(viewProj[column][row]),
                      "a zero-range light must not produce a NaN transform");
        }
    }
}

static void testTheMapOrientationMatchesTheCascadeConvention() {
    // A spot's depth map is sampled exactly like a cascade - project, take
    // xy * 0.5 + 0.5, compare - so its transform has to agree with the
    // cascades about which way is up. ShadowCascades negates the projection's
    // Y because GLM builds for OpenGL, whose clip space has +Y up, and
    // Vulkan's points down. Miss it and the shadow appears above whatever
    // casts it, which is the kind of wrong that still looks like a shadow.
    //
    // Both offsets are pinned, because the up vector this frustum picks for a
    // downward lamp decides them together: change either and one flips.
    const glm::vec3 position(0.0f, 5.0f, 0.0f);
    const glm::vec3 down(0.0f, -1.0f, 0.0f);
    const glm::mat4 viewProj = SpotLight::BuildViewProj(position, down, kOuter, 20.0f);

    const glm::vec3 towardPlusZ = project(viewProj, glm::vec3(0.0f, 1.0f, 1.0f));
    CHECK_MSG(towardPlusZ.y < -1e-3f,
              "a +Z offset must land in the upper half of the map: ndc.y " +
                  std::to_string(towardPlusZ.y));
    CHECK_MSG(std::abs(towardPlusZ.x) < 1e-3f, "and must not move in x at all");

    const glm::vec3 towardPlusX = project(viewProj, glm::vec3(1.0f, 1.0f, 0.0f));
    CHECK_MSG(towardPlusX.x < -1e-3f,
              "a +X offset must land in the left half: ndc.x " + std::to_string(towardPlusX.x));
    CHECK_MSG(std::abs(towardPlusX.y) < 1e-3f, "and must not move in y at all");
}
static void runTests() {
    testStraightAheadIsFullyLit();
    testBehindTheLightIsDark();
    testInsideTheInnerConeIsFullBrightness();
    testOutsideTheOuterConeIsDark();
    testTheFalloffIsMonotonic();
    testTheEdgeIsSoftRatherThanABinaryCutoff();
    testAnglesAuthoredBackwardsStillProduceACone();
    testEqualAnglesGiveAHardEdgeRatherThanNaN();
    testADirectionlessLightDoesNotProduceNaN();

    testTheShadowFrustumCoversTheWholeCone();
    testTheAxisProjectsToTheCentreOfTheMap();
    testALampPointingStraightDownIsNotDegenerate();
    testDepthGrowsWithDistanceFromTheLight();
    testAZeroRangeLightDoesNotProduceNaN();
    testTheMapOrientationMatchesTheCascadeConvention();
}

TEST_MAIN("test_spotlight", 140)
