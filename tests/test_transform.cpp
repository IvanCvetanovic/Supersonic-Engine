// Pins down the maths conventions the rest of the engine depends on.
//
// These are not academic checks. GLM_FORCE_DEPTH_ZERO_TO_ONE used to be defined
// in two headers rather than on the target, so whichever header pulled in GLM
// first decided the clip-space convention for that translation unit and
// getProjectionMatrix() silently compiled two different ways across the build.
// If someone moves those defines back into a header, this test starts failing.

#include "TestHarness.hpp"
#include "core/Components.hpp"

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

static void runTests() {
    testDepthRangeIsZeroToOne();
    testProjectionFlipsYForVulkan();
    testModelMatrixEulerOrder();
    testCameraVectorsStayOrthonormal();
}

TEST_MAIN("test_transform")
