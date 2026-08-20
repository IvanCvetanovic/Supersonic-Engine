// Pins down the maths conventions the rest of the engine depends on.
//
// These are not academic checks. GLM_FORCE_DEPTH_ZERO_TO_ONE used to be defined
// in two headers rather than on the target, so whichever header pulled in GLM
// first decided the clip-space convention for that translation unit and
// getProjectionMatrix() silently compiled two different ways across the build.
// If someone moves those defines back into a header, this test starts failing.

#include "TestHarness.hpp"
#include <glm/gtc/constants.hpp>
#include "core/Components.hpp"
#include "core/EcsUtils.hpp"

#include <glm/gtc/matrix_transform.hpp>

using namespace Supersonic;

static void testDepthRangeIsZeroToOne() {
    // With GLM_FORCE_DEPTH_ZERO_TO_ONE, a point on the near plane maps to z=0
    // and one on the far plane to z=1. Under the OpenGL convention the near
    // plane would map to -1 instead.
    CameraComponent cam;
    cam.fov = 45.0f;
    cam.aspect = 16.0f / 9.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 100.0f;
    cam.position = glm::vec3(0.0f);
    cam.front = glm::vec3(0.0f, 0.0f, -1.0f);
    cam.up = glm::vec3(0.0f, 1.0f, 0.0f);

    const glm::mat4 proj = cam.getProjectionMatrix();

    auto ndcZ = [&](float viewDistance) {
        glm::vec4 clip = proj * glm::vec4(0.0f, 0.0f, -viewDistance, 1.0f);
        return clip.z / clip.w;
    };

    CHECK_NEAR(ndcZ(cam.nearPlane), 0.0f);
    CHECK_NEAR(ndcZ(cam.farPlane), 1.0f);
}

static void testProjectionFlipsYForVulkan() {
    // Vulkan's clip space has +Y pointing down, so getProjectionMatrix negates
    // the Y row. A point above the camera axis must land at negative NDC Y.
    CameraComponent cam;
    cam.position = glm::vec3(0.0f);
    cam.front = glm::vec3(0.0f, 0.0f, -1.0f);
    cam.up = glm::vec3(0.0f, 1.0f, 0.0f);

    const glm::mat4 proj = cam.getProjectionMatrix();
    glm::vec4 clip = proj * glm::vec4(0.0f, 1.0f, -5.0f, 1.0f);

    CHECK_MSG(clip.y / clip.w < 0.0f, "world +Y must map to Vulkan NDC -Y");
}

static void testModelMatrixEulerOrder() {
    // getModelMatrix composes translate * Rx * Ry * Rz * scale. The gizmo
    // write-back path has to decompose using the same convention or objects
    // snap the moment a handle is touched.
    TransformComponent t;
    t.position = glm::vec3(1.0f, 2.0f, 3.0f);
    t.rotation = glm::vec3(glm::radians(30.0f), glm::radians(45.0f), glm::radians(60.0f));
    t.scale = glm::vec3(2.0f, 2.0f, 2.0f);

    glm::mat4 expected = glm::translate(glm::mat4(1.0f), t.position);
    expected = glm::rotate(expected, t.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    expected = glm::rotate(expected, t.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    expected = glm::rotate(expected, t.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    expected = glm::scale(expected, t.scale);

    const glm::mat4 actual = t.getModelMatrix();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            CHECK_NEAR(actual[c][r], expected[c][r]);
        }
    }

    // Translation must survive untouched in the last column.
    CHECK_NEAR(actual[3][0], 1.0f);
    CHECK_NEAR(actual[3][1], 2.0f);
    CHECK_NEAR(actual[3][2], 3.0f);
}

static void testCameraVectorsStayOrthonormal() {
    CameraComponent cam;
    cam.yaw = -90.0f;
    cam.pitch = -10.0f;
    cam.updateCameraVectors();

    CHECK_NEAR(glm::length(cam.front), 1.0f);
    CHECK_NEAR(glm::length(cam.right), 1.0f);
    CHECK_NEAR(glm::length(cam.up), 1.0f);
    CHECK_NEAR(glm::dot(cam.front, cam.right), 0.0f);
    CHECK_NEAR(glm::dot(cam.front, cam.up), 0.0f);
    CHECK_NEAR(glm::dot(cam.right, cam.up), 0.0f);
}

// Which light supplies the scene's ambient.
//
// This used to be whichever light EnTT iterated first, and EnTT walks its
// packed array backwards, so it was the light created LAST: a sun carrying a
// carefully authored sky colour was ignored in favour of a lamp's default, and
// adding any light changed the ambient of the whole scene.
static void testAmbientComesFromTheSkyLight() {
    entt::registry registry;

    // Created first, so it is the LAST one EnTT hands back - which is exactly
    // the case that used to pick the wrong light.
    const auto sun = registry.create();
    auto& sunLight = registry.emplace<LightComponent>(sun);
    sunLight.type = static_cast<int>(LightType::Directional);
    sunLight.castsShadow = true;

    const auto lamp = registry.create();
    auto& lampLight = registry.emplace<LightComponent>(lamp);
    lampLight.type = static_cast<int>(LightType::Point);

    CHECK_MSG(FindAmbientLight(registry) == sun,
              "the shadow-casting directional light supplies the ambient, "
              "not whichever light happens to be iterated first");
}

static void testANonCastingDirectionalStillBeatsAPointLight() {
    entt::registry registry;

    const auto lamp = registry.create();
    registry.emplace<LightComponent>(lamp).type = static_cast<int>(LightType::Point);

    const auto sun = registry.create();
    auto& sunLight = registry.emplace<LightComponent>(sun);
    sunLight.type = static_cast<int>(LightType::Directional);
    sunLight.castsShadow = false;

    CHECK_MSG(FindAmbientLight(registry) == sun,
              "hemispheric ambient stands in for the sky, so a directional "
              "light supplies it whether or not it casts");
}

static void testAPointLightOnlySceneStillHasAmbient() {
    // Order-dependent by design in this case: with no directional light there
    // is no sky light to prefer, and any answer is as good as another. What
    // must not happen is returning nothing, which would leave the scene black
    // rather than merely differently lit.
    entt::registry registry;
    const auto lamp = registry.create();
    registry.emplace<LightComponent>(lamp).type = static_cast<int>(LightType::Point);

    CHECK_MSG(FindAmbientLight(registry) != entt::null,
              "a scene of nothing but lamps must still get an ambient term");
}

static void testAnEmptySceneHasNoAmbientLight() {
    entt::registry registry;
    CHECK(FindAmbientLight(registry) == entt::null);
}


// --- the model matrix -------------------------------------------------------

// The definition, spelled the way it was written before it was multiplied out:
// translate, then rotate about x, y and z in that order, then scale. This is
// the reference the expansion is checked against, and it is here rather than in
// the header precisely so that the two are independent - a bug copied into both
// would be checked against itself.
static glm::mat4 chainedModelMatrix(const TransformComponent& t) {
    glm::mat4 mat = glm::translate(glm::mat4(1.0f), t.position);
    mat = glm::rotate(mat, t.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    mat = glm::rotate(mat, t.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    mat = glm::rotate(mat, t.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    mat = glm::scale(mat, t.scale);
    return mat;
}

static void testTheModelMatrixMatchesTheChainItReplaces() {
    // getModelMatrix builds the product directly instead of through five 4x4
    // multiplies. Multiplying Rx*Ry*Rz out by hand is exactly the kind of thing
    // that is wrong in one entry and looks right in every screenshot until
    // something is rotated about two axes at once - so every entry is compared,
    // over a spread of angles that includes the signs and the poles.
    const float angles[] = {
        0.0f, 0.3f, -0.3f, 1.2f, -1.2f,
        glm::half_pi<float>(), -glm::half_pi<float>(),
        glm::pi<float>(), 2.7f, -2.7f, 5.9f,
    };
    const int count = static_cast<int>(sizeof(angles) / sizeof(angles[0]));

    int compared = 0;
    for (int i = 0; i < count; ++i) {
        for (int j = 0; j < count; ++j) {
            for (int k = 0; k < count; ++k) {
                TransformComponent t;
                t.position = glm::vec3(1.5f - static_cast<float>(i),
                                       static_cast<float>(j) * 0.7f,
                                       -4.0f + static_cast<float>(k));
                t.rotation = glm::vec3(angles[i], angles[j], angles[k]);
                // Non-uniform, and one of them negative: scale multiplies the
                // columns, and getting that backwards is invisible while every
                // scale is the same number.
                t.scale = glm::vec3(0.5f + static_cast<float>(i) * 0.3f,
                                    2.0f - static_cast<float>(j) * 0.1f,
                                    (k % 2 == 0) ? 1.3f : -0.8f);

                const glm::mat4 expected = chainedModelMatrix(t);
                const glm::mat4 actual = t.getModelMatrix();

                for (int c = 0; c < 4; ++c) {
                    for (int r = 0; r < 4; ++r) {
                        if (std::fabs(expected[c][r] - actual[c][r]) >= 1e-5f) {
                            CHECK_MSG(false,
                                      "entry [" + std::to_string(c) + "][" + std::to_string(r) +
                                          "] differs at rotation (" + std::to_string(t.rotation.x) +
                                          ", " + std::to_string(t.rotation.y) + ", " +
                                          std::to_string(t.rotation.z) + "): expected " +
                                          std::to_string(expected[c][r]) + " got " +
                                          std::to_string(actual[c][r]));
                            return;
                        }
                    }
                }
                ++compared;
            }
        }
    }

    // One check for the whole sweep, plus this: a loop that compared nothing
    // would otherwise pass silently, which is the usual way a table-driven test
    // stops testing.
    CHECK_MSG(compared == count * count * count,
              "the sweep must have compared every combination: " + std::to_string(compared));
    CHECK_MSG(compared > 1000, "and there must be enough of them to mean something");
}

static void runTests() {
    testTheModelMatrixMatchesTheChainItReplaces();
    testDepthRangeIsZeroToOne();
    testProjectionFlipsYForVulkan();
    testModelMatrixEulerOrder();
    testCameraVectorsStayOrthonormal();
    testAmbientComesFromTheSkyLight();
    testANonCastingDirectionalStillBeatsAPointLight();
    testAPointLightOnlySceneStillHasAmbient();
    testAnEmptySceneHasNoAmbientLight();
}

TEST_MAIN("test_transform", 30)
