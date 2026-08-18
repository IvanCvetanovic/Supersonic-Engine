// Regression tests for omnidirectional shadow geometry.
//
// A point light sees in every direction, so its shadow map is a cube rendered
// six times. Two things have to agree exactly or the result looks almost right
// and is wrong: the orientation of each rendered face must match the one the
// cube sampler picks for a given direction, and the depth the shader compares
// against must be the depth that face actually wrote.
//
// Neither is visible in a screenshot as anything more specific than "the
// shadows are a bit off", which is why they are checked here instead.

#include "TestHarness.hpp"
#include "renderer/PointShadow.hpp"

#include <cmath>
#include <string>
#include <vector>

#include <glm/glm.hpp>

using namespace Supersonic;

namespace {

// Directions covering every face, plus the diagonals where the face choice
// changes and an off-axis case that is nowhere near a boundary.
std::vector<glm::vec3> sampleDirections() {
    return {
        {  1.0f,  0.0f,  0.0f }, { -1.0f,  0.0f,  0.0f },
        {  0.0f,  1.0f,  0.0f }, {  0.0f, -1.0f,  0.0f },
        {  0.0f,  0.0f,  1.0f }, {  0.0f,  0.0f, -1.0f },
        {  3.0f,  1.0f,  0.5f }, { -2.5f,  0.4f, -1.2f },
        {  0.7f,  4.0f, -0.9f }, {  0.2f, -3.3f,  1.1f },
        { -0.6f,  0.8f,  5.0f }, {  1.3f, -0.5f, -6.0f },
        {  2.0f,  1.9f,  0.1f }, {  0.05f, 0.04f, 0.03f },
    };
}

// Projects a world point through one face's matrix and returns its NDC.
glm::vec3 project(const glm::mat4& viewProj, const glm::vec3& worldPoint) {
    const glm::vec4 clip = viewProj * glm::vec4(worldPoint, 1.0f);
    if (std::abs(clip.w) < 1e-9f) return glm::vec3(0.0f);
    return glm::vec3(clip) / clip.w;
}

} // namespace

static void testEveryDirectionLandsInsideItsOwnFace() {
    // The face a direction is looked up through has to be the face that
    // direction was rendered into. If it is not, a fragment samples a
    // neighbouring face and is shadowed by geometry that is somewhere else.
    const glm::vec3 light(2.0f, 3.0f, -1.0f);
    const float far = 25.0f;
    const auto faces = PointShadow::BuildFaceViewProj(light, far);

    for (const glm::vec3& direction : sampleDirections()) {
        const uint32_t face = PointShadow::FaceForDirection(direction);
        const glm::vec3 world = light + direction;
        const glm::vec3 ndc = project(faces[face], world);

        const std::string which = "direction (" + std::to_string(direction.x) + ", " +
                                  std::to_string(direction.y) + ", " +
                                  std::to_string(direction.z) + ") on face " +
                                  std::to_string(face);

        CHECK_MSG(ndc.x >= -1.0001f && ndc.x <= 1.0001f, which + " fell outside the face in x");
        CHECK_MSG(ndc.y >= -1.0001f && ndc.y <= 1.0001f, which + " fell outside the face in y");
        CHECK_MSG(ndc.z >= -0.0001f && ndc.z <= 1.0001f, which + " fell outside the depth range");
    }
}

static void testAnalyticDepthMatchesTheProjectedDepth() {
    // The shader does not have the six matrices; it reconstructs the depth from
    // the distance along the major axis. That shortcut is only valid if it
    // agrees with what the face's projection actually wrote - to within the
    // precision of the depth buffer, not "roughly".
    const glm::vec3 light(-1.5f, 2.0f, 4.0f);

    for (const float far : { 5.0f, 25.0f, 100.0f }) {
        const auto faces = PointShadow::BuildFaceViewProj(light, far);

        for (const glm::vec3& direction : sampleDirections()) {
            const uint32_t face = PointShadow::FaceForDirection(direction);
            const glm::vec3 world = light + direction;

            const float projected = project(faces[face], world).z;
            const float analytic = PointShadow::DepthForDirection(direction, far);

            CHECK_MSG(std::abs(projected - analytic) < 1e-4f,
                      "far=" + std::to_string(far) + " face=" + std::to_string(face) +
                          ": projection wrote " + std::to_string(projected) +
                          " but the shader would compare against " + std::to_string(analytic));
        }
    }
}

static void testDepthIncreasesWithDistance() {
    // Monotonic, or the comparison in the shader has no meaning: a nearer
    // occluder has to produce a smaller depth than the fragment behind it.
    const float far = 30.0f;
    float previous = -1.0f;

    for (float distance = 0.2f; distance < 25.0f; distance += 0.4f) {
        const float depth = PointShadow::DepthForDirection(glm::vec3(0.0f, 0.0f, distance), far);
        CHECK_MSG(depth > previous,
                  "depth must grow with distance: " + std::to_string(depth) + " at " +
                      std::to_string(distance));
        previous = depth;
    }
}

static void testDepthSpansTheFullRange() {
    const float far = 20.0f;

    const float atNear = PointShadow::DepthForDirection(
        glm::vec3(PointShadow::kNearPlane, 0.0f, 0.0f), far);
    const float atFar = PointShadow::DepthForDirection(glm::vec3(far, 0.0f, 0.0f), far);

    CHECK_MSG(std::abs(atNear) < 1e-3f, "the near plane must map to 0: got " + std::to_string(atNear));
    CHECK_MSG(std::abs(atFar - 1.0f) < 1e-3f, "the far plane must map to 1: got " + std::to_string(atFar));
}

static void testDegenerateInputsDoNotProduceNaN() {
    // A light authored with zero range, and a fragment exactly on the light.
    const auto faces = PointShadow::BuildFaceViewProj(glm::vec3(0.0f), 0.0f);
    for (const glm::mat4& face : faces) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                CHECK_MSG(std::isfinite(face[column][row]),
                          "a zero-range light must not produce a NaN matrix");
            }
        }
    }

    const float depth = PointShadow::DepthForDirection(glm::vec3(0.0f), 10.0f);
    CHECK_MSG(std::isfinite(depth), "a fragment on the light itself must not produce NaN");
}

static void testFaceSelectionMatchesTheMajorAxis() {
    CHECK_EQ(PointShadow::FaceForDirection({  2.0f, 1.0f, 1.0f }), 0u);
    CHECK_EQ(PointShadow::FaceForDirection({ -2.0f, 1.0f, 1.0f }), 1u);
    CHECK_EQ(PointShadow::FaceForDirection({  1.0f, 2.0f, 1.0f }), 2u);
    CHECK_EQ(PointShadow::FaceForDirection({  1.0f, -2.0f, 1.0f }), 3u);
    CHECK_EQ(PointShadow::FaceForDirection({  1.0f, 1.0f, 2.0f }), 4u);
    CHECK_EQ(PointShadow::FaceForDirection({  1.0f, 1.0f, -2.0f }), 5u);
}

// Where the sampler will look, per the Vulkan cube-map face table. The
// rendered face has to put the same direction at the same place, or every
// lookup is subtly displaced - and "lands somewhere on the right face", which
// the test above checks, is not nearly enough: an up vector rotated by 180
// degrees still lands inside the face, just upside down.
static glm::vec2 samplerUv(const glm::vec3& direction) {
    const glm::vec3 magnitude = glm::abs(direction);
    const float ma = std::max(magnitude.x, std::max(magnitude.y, magnitude.z));

    float sc = 0.0f;
    float tc = 0.0f;
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {
        sc = direction.x >= 0.0f ? -direction.z :  direction.z;
        tc = -direction.y;
    } else if (magnitude.y >= magnitude.z) {
        sc = direction.x;
        tc = direction.y >= 0.0f ?  direction.z : -direction.z;
    } else {
        sc = direction.z >= 0.0f ?  direction.x : -direction.x;
        tc = -direction.y;
    }

    return glm::vec2(0.5f * (sc / ma + 1.0f), 0.5f * (tc / ma + 1.0f));
}

static void testRenderedFacesMatchWhereTheSamplerLooks() {
    const glm::vec3 light(1.0f, -2.0f, 0.5f);
    const float far = 40.0f;
    const auto faces = PointShadow::BuildFaceViewProj(light, far);

    for (const glm::vec3& direction : sampleDirections()) {
        if (glm::length(direction) < 1e-3f) continue;

        const uint32_t face = PointShadow::FaceForDirection(direction);
        const glm::vec3 ndc = project(faces[face], light + direction);

        // Vulkan NDC runs -1..1 in x and y with +y downward, and the framebuffer
        // origin is top-left - so both map to 0..1 the same way.
        const glm::vec2 rendered(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f);
        const glm::vec2 sampled = samplerUv(direction);

        const std::string which = "face " + std::to_string(face) + ", direction (" +
                                  std::to_string(direction.x) + ", " +
                                  std::to_string(direction.y) + ", " +
                                  std::to_string(direction.z) + ")";

        CHECK_MSG(std::abs(rendered.x - sampled.x) < 1e-4f,
                  which + ": rendered u " + std::to_string(rendered.x) +
                      " but the sampler reads " + std::to_string(sampled.x));
        CHECK_MSG(std::abs(rendered.y - sampled.y) < 1e-4f,
                  which + ": rendered v " + std::to_string(rendered.y) +
                      " but the sampler reads " + std::to_string(sampled.y));
    }
}

static void runTests() {
    testFaceSelectionMatchesTheMajorAxis();
    testEveryDirectionLandsInsideItsOwnFace();
    testRenderedFacesMatchWhereTheSamplerLooks();
    testAnalyticDepthMatchesTheProjectedDepth();
    testDepthIncreasesWithDistance();
    testDepthSpansTheFullRange();
    testDegenerateInputsDoNotProduceNaN();
}

TEST_MAIN("test_pointshadow")
