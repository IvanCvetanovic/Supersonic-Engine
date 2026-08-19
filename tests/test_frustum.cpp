// Regression tests for view-frustum culling.
//
// Culling is the one feature whose bugs are invisible in a screenshot: a wrong
// plane either culls nothing (so the picture is right and the work is wasted)
// or culls something on screen (so the picture is wrong in a way that only
// shows from one specific angle). These pin the conventions down instead.
//
// The near plane is the trap. Under GLM's default OpenGL depth range it is
// row3 + row2; the engine builds every projection with
// GLM_FORCE_DEPTH_ZERO_TO_ONE, where it is row2 alone. Getting that wrong
// leaves geometry behind the camera passing the test.

#include "TestHarness.hpp"
#include "renderer/Frustum.hpp"
#include "core/Components.hpp"

#include <glm/gtc/matrix_transform.hpp>

using namespace Supersonic;

static CameraComponent makeCamera() {
    CameraComponent cam;
    cam.fov = 45.0f;
    cam.aspect = 1.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 100.0f;
    cam.position = glm::vec3(0.0f, 0.0f, 5.0f);
    cam.yaw = -90.0f; // looking down -Z
    cam.pitch = 0.0f;
    cam.updateCameraVectors();
    return cam;
}

static Frustum cameraFrustum(const CameraComponent& cam) {
    return Frustum::FromMatrix(cam.getProjectionMatrix() * cam.getViewMatrix());
}

// A small box centred on a point, which is what most of these tests want.
static void boxAt(const glm::vec3& centre, float halfSize, glm::vec3& min, glm::vec3& max) {
    min = centre - glm::vec3(halfSize);
    max = centre + glm::vec3(halfSize);
}

static void testBoxInFrontIsVisible() {
    const Frustum f = cameraFrustum(makeCamera());
    glm::vec3 min, max;
    boxAt(glm::vec3(0.0f, 0.0f, 0.0f), 0.5f, min, max);
    CHECK_MSG(f.IntersectsAABB(min, max),
              "a box directly ahead of the camera must survive culling");
}

static void testBoxBehindCameraIsCulled() {
    const Frustum f = cameraFrustum(makeCamera());
    glm::vec3 min, max;
    // Camera sits at z=+5 looking toward -Z, so z=+10 is squarely behind it.
    boxAt(glm::vec3(0.0f, 0.0f, 10.0f), 0.5f, min, max);
    CHECK_MSG(!f.IntersectsAABB(min, max),
              "a box behind the camera must be culled - this is the near-plane "
              "convention, and it is what an OpenGL-style row3+row2 gets wrong");
}

static void testBoxBeyondFarPlaneIsCulled() {
    const Frustum f = cameraFrustum(makeCamera());
    glm::vec3 min, max;
    boxAt(glm::vec3(0.0f, 0.0f, -200.0f), 0.5f, min, max);
    CHECK_MSG(!f.IntersectsAABB(min, max), "a box past the far plane must be culled");
}

static void testBoxOffToTheSideIsCulled() {
    const Frustum f = cameraFrustum(makeCamera());
    glm::vec3 min, max;
    // 45 degrees of vertical FOV at aspect 1 gives roughly +/-2 of half-width
    // five units ahead. 50 is far outside it on either side.
    boxAt(glm::vec3(50.0f, 0.0f, 0.0f), 0.5f, min, max);
    CHECK_MSG(!f.IntersectsAABB(min, max), "a box far to the right must be culled");

    boxAt(glm::vec3(-50.0f, 0.0f, 0.0f), 0.5f, min, max);
    CHECK_MSG(!f.IntersectsAABB(min, max), "and so must one far to the left");
}

static void testBoxAboveAndBelowIsCulled() {
    const Frustum f = cameraFrustum(makeCamera());
    glm::vec3 min, max;
    boxAt(glm::vec3(0.0f, 50.0f, 0.0f), 0.5f, min, max);
    CHECK_MSG(!f.IntersectsAABB(min, max), "a box far above must be culled");

    boxAt(glm::vec3(0.0f, -50.0f, 0.0f), 0.5f, min, max);
    CHECK_MSG(!f.IntersectsAABB(min, max), "a box far below must be culled");
}

static void testStraddlingBoxIsKept() {
    const Frustum f = cameraFrustum(makeCamera());
    // Mostly off to the right but reaching back into view. The positive-vertex
    // test is conservative, so this must be kept: culling a partly visible
    // object is the failure mode that shows as geometry vanishing at the edge
    // of the screen.
    const glm::vec3 min(1.0f, -0.5f, -1.0f);
    const glm::vec3 max(60.0f, 0.5f, 1.0f);
    CHECK_MSG(f.IntersectsAABB(min, max),
              "a box straddling a side plane must be kept, not culled");
}

static void testHugeBoxEnclosingCameraIsKept() {
    const Frustum f = cameraFrustum(makeCamera());
    // The camera is inside this one. No corner is "in front" of every plane in
    // the naive sense, which is exactly the case a sloppy test gets wrong.
    const glm::vec3 min(-100.0f);
    const glm::vec3 max(100.0f);
    CHECK_MSG(f.IntersectsAABB(min, max), "a box containing the camera must be kept");
}

static void testTransformAABBTranslates() {
    const glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -2.0f, 1.0f));
    glm::vec3 outMin, outMax;
    Frustum::TransformAABB(m, glm::vec3(-0.5f), glm::vec3(0.5f), outMin, outMax);

    CHECK_NEAR(outMin.x, 2.5f);
    CHECK_NEAR(outMin.y, -2.5f);
    CHECK_NEAR(outMin.z, 0.5f);
    CHECK_NEAR(outMax.x, 3.5f);
    CHECK_NEAR(outMax.y, -1.5f);
    CHECK_NEAR(outMax.z, 1.5f);
}

static void testTransformAABBScales() {
    const glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 4.0f, 6.0f));
    glm::vec3 outMin, outMax;
    Frustum::TransformAABB(m, glm::vec3(-0.5f), glm::vec3(0.5f), outMin, outMax);

    CHECK_NEAR(outMax.x, 1.0f);
    CHECK_NEAR(outMax.y, 2.0f);
    CHECK_NEAR(outMax.z, 3.0f);
    CHECK_NEAR(outMin.x, -1.0f);
    CHECK_NEAR(outMin.y, -2.0f);
    CHECK_NEAR(outMin.z, -3.0f);
}

static void testTransformAABBRotationGrowsTheBox() {
    // A unit cube rotated 45 degrees about Y has a wider axis-aligned bound:
    // sqrt(2)/2 ~= 0.7071 rather than 0.5 on X and Z, and unchanged on Y.
    const glm::mat4 m = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::vec3 outMin, outMax;
    Frustum::TransformAABB(m, glm::vec3(-0.5f), glm::vec3(0.5f), outMin, outMax);

    CHECK_NEAR(outMax.x, 0.70710678f);
    CHECK_NEAR(outMax.z, 0.70710678f);
    CHECK_NEAR(outMax.y, 0.5f);
    CHECK_MSG(outMin.x < -0.7f && outMin.z < -0.7f, "the rotated bound must grow symmetrically");
}

static void testTransformAABBCombinesRotationScaleAndTranslation() {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
    m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    m = glm::scale(m, glm::vec3(1.0f, 3.0f, 1.0f));

    glm::vec3 outMin, outMax;
    Frustum::TransformAABB(m, glm::vec3(-0.5f), glm::vec3(0.5f), outMin, outMax);

    // The Y scale of 3 becomes an X extent of 1.5 after the 90-degree turn.
    CHECK_NEAR(outMin.x, 8.5f);
    CHECK_NEAR(outMax.x, 11.5f);
    CHECK_NEAR(outMin.y, -0.5f);
    CHECK_NEAR(outMax.y, 0.5f);
}

static void testOffCentreBoxTransformsAboutItsOwnCentre() {
    // TransformAABB moves the centre and rebuilds the extent, so a box whose
    // local centre is not the origin must not be pinned to the origin.
    const glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f));
    glm::vec3 outMin, outMax;
    Frustum::TransformAABB(m, glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(4.0f, 4.0f, 4.0f), outMin, outMax);

    CHECK_NEAR(outMin.x, 2.0f);
    CHECK_NEAR(outMin.y, 7.0f);
    CHECK_NEAR(outMax.y, 9.0f);
}

static void testRotatedCameraSeesWhatItFaces() {
    CameraComponent cam = makeCamera();
    cam.yaw = 90.0f; // turned around, now looking down +Z
    cam.updateCameraVectors();
    const Frustum f = cameraFrustum(cam);

    glm::vec3 min, max;
    boxAt(glm::vec3(0.0f, 0.0f, 10.0f), 0.5f, min, max);
    CHECK_MSG(f.IntersectsAABB(min, max), "after turning around, what was behind must be visible");

    boxAt(glm::vec3(0.0f, 0.0f, 0.0f), 0.5f, min, max);
    CHECK_MSG(!f.IntersectsAABB(min, max), "and what was in front must now be culled");
}

static void testPlanesAreNormalised() {
    const Frustum f = cameraFrustum(makeCamera());
    for (const auto& plane : f.Planes()) {
        const float length = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
        CHECK_NEAR(length, 1.0f);
    }
}

static void testOrthographicLightFrustumWorks() {
    // The shadow pass reuses FromMatrix with the light's orthographic matrix,
    // so the same extraction has to hold for a projection with no perspective
    // divide at all.
    glm::mat4 proj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 50.0f);
    proj[1][1] *= -1.0f; // the engine's Vulkan Y flip
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 20.0f, 0.0f),
                                       glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    const Frustum f = Frustum::FromMatrix(proj * view);

    glm::vec3 min, max;
    boxAt(glm::vec3(0.0f, 0.0f, 0.0f), 0.5f, min, max);
    CHECK_MSG(f.IntersectsAABB(min, max), "a box under the light must be lit");

    boxAt(glm::vec3(40.0f, 0.0f, 0.0f), 0.5f, min, max);
    CHECK_MSG(!f.IntersectsAABB(min, max), "a box outside the light's extent must be culled");
}

static void testDegenerateMatrixDoesNotCullEverything() {
    // A zero matrix has no meaningful planes. Normalising must not divide by
    // zero, and the result must not silently swallow the whole scene without a
    // trace - the plane equations end up all-zero, which passes the test.
    const Frustum f = Frustum::FromMatrix(glm::mat4(0.0f));
    CHECK_MSG(f.IntersectsAABB(glm::vec3(-1.0f), glm::vec3(1.0f)),
              "a degenerate matrix must not cull the scene away");
}

static void runTests() {
    testBoxInFrontIsVisible();
    testBoxBehindCameraIsCulled();
    testBoxBeyondFarPlaneIsCulled();
    testBoxOffToTheSideIsCulled();
    testBoxAboveAndBelowIsCulled();
    testStraddlingBoxIsKept();
    testHugeBoxEnclosingCameraIsKept();
    testTransformAABBTranslates();
    testTransformAABBScales();
    testTransformAABBRotationGrowsTheBox();
    testTransformAABBCombinesRotationScaleAndTranslation();
    testOffCentreBoxTransformsAboutItsOwnCentre();
    testRotatedCameraSeesWhatItFaces();
    testPlanesAreNormalised();
    testOrthographicLightFrustumWorks();
    testDegenerateMatrixDoesNotCullEverything();
}

TEST_MAIN("test_frustum", 30)
