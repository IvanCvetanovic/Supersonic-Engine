// Regression tests for cascaded shadow map fitting.
//
// Every failure mode here is a convention mistake that renders *something*, so
// none of them show up as a crash or a validation error:
//
//   - integer division in the split formula collapses every cascade onto the
//     near plane, and the scene still draws, just with one usable cascade;
//   - a radius that is not quantised, or a centre that is not snapped to the
//     shadow map's own texel grid, makes every shadow edge crawl as the camera
//     moves - visible only in motion, never in a screenshot;
//   - fitting the depth range to the slice rather than to the scene clips
//     casters standing between the light and that slice, and their shadows
//     simply disappear.
//
// test_frustum covers Frustum::FromMatrix. This is a different code path with
// the same traps in it, so it gets its own guards.

#include "TestHarness.hpp"
#include "renderer/ShadowCascades.hpp"

#include <cmath>
#include <vector>

using namespace Supersonic;

static CameraComponent makeCamera(const glm::vec3& position = glm::vec3(0.0f, 2.0f, 10.0f),
                                  float yaw = -90.0f) {
    CameraComponent camera;
    camera.fov = 50.0f;
    camera.aspect = 16.0f / 9.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 200.0f;
    camera.position = position;
    camera.yaw = yaw;
    camera.pitch = -10.0f;
    camera.updateCameraVectors();
    return camera;
}

static bool matricesEqual(const glm::mat4& a, const glm::mat4& b) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (a[column][row] != b[column][row]) return false;
        }
    }
    return true;
}

static void testSplitsIncreaseAndReachTheShadowDistance() {
    const auto splits = ShadowCascades::ComputeSplits(0.1f, 60.0f);

    CHECK_MSG(splits[0] > 0.1f, "the first split must be past the near plane");
    for (uint32_t i = 1; i < kShadowCascadeCount; ++i) {
        CHECK_MSG(splits[i] > splits[i - 1], "splits must increase");
    }
    CHECK_NEAR(splits[kShadowCascadeCount - 1], 60.0f);
}

static void testSplitsDoNotCollapseOntoTheNearPlane() {
    // The integer-division trap: (i + 1) / kShadowCascadeCount evaluated in
    // integer arithmetic is 0 for every cascade but the last, so every split
    // lands on the near plane and three of the four cascades cover nothing.
    const auto splits = ShadowCascades::ComputeSplits(0.1f, 100.0f);
    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        CHECK_MSG(splits[i] > 0.5f,
                  "cascade " + std::to_string(i) + " must cover real distance, not sit on the near plane");
    }
    CHECK_MSG(splits[0] < splits[kShadowCascadeCount - 1] * 0.5f,
              "and the near cascades must be the small ones");
}

static void testUniformAndLogarithmicSplitsDiffer() {
    const auto uniform = ShadowCascades::ComputeSplits(1.0f, 100.0f, 0.0f);
    const auto logarithmic = ShadowCascades::ComputeSplits(1.0f, 100.0f, 1.0f);

    // Uniform divides the range evenly: 25.75, 50.5, 75.25, 100.
    CHECK_NEAR(uniform[0], 25.75f);
    CHECK_NEAR(uniform[1], 50.5f);

    CHECK_MSG(logarithmic[0] < uniform[0],
              "a logarithmic split must put the first cascade closer than a uniform one");
}

static void testSliceCornersMatchTheCameraFrustum() {
    const CameraComponent camera = makeCamera(glm::vec3(0.0f, 0.0f, 0.0f), -90.0f);
    const auto corners = ShadowCascades::SliceCorners(camera, 2.0f, 10.0f);

    // The mean of the four near corners is the slice's near-plane centre.
    glm::vec3 nearCentre(0.0f);
    for (int i = 0; i < 4; ++i) nearCentre += corners[static_cast<size_t>(i)];
    nearCentre /= 4.0f;

    const glm::vec3 expectedNear = camera.position + glm::normalize(camera.front) * 2.0f;
    CHECK_NEAR(nearCentre.x, expectedNear.x);
    CHECK_NEAR(nearCentre.y, expectedNear.y);
    CHECK_NEAR(nearCentre.z, expectedNear.z);

    // The far end is wider than the near end, which is what a perspective
    // frustum is.
    const float nearWidth = glm::length(corners[1] - corners[0]);
    const float farWidth = glm::length(corners[5] - corners[4]);
    CHECK_MSG(farWidth > nearWidth * 4.0f, "the far slice face must be much wider than the near one");

    // Vertical half extent must follow the FOV, and horizontal must follow it
    // scaled by the aspect ratio - swapping the two is an easy mistake that
    // still produces a plausible-looking frustum.
    const float tanHalfV = std::tan(glm::radians(camera.fov) * 0.5f);
    CHECK_NEAR(farWidth * 0.5f, 10.0f * tanHalfV * camera.aspect);
}

static void testNearCascadesHaveFinerTexelsThanFarOnes() {
    const CameraComponent camera = makeCamera();
    const CascadeSetup setup = ShadowCascades::Build(
        camera, glm::vec3(0.6f, 1.0f, 0.5f), glm::vec3(-40.0f), glm::vec3(40.0f), 2048);

    for (uint32_t i = 1; i < kShadowCascadeCount; ++i) {
        CHECK_MSG(setup.texelWorldSize[i] > setup.texelWorldSize[i - 1],
                  "cascade " + std::to_string(i) + " must cover more world per texel than the one before it");
    }
    CHECK_MSG(setup.texelWorldSize[kShadowCascadeCount - 1] > setup.texelWorldSize[0] * 3.0f,
              "and the difference must be large, or the cascades are not buying anything");
}

static void testEachCascadeContainsItsOwnSlice() {
    const CameraComponent camera = makeCamera();
    // Scene bounds generous enough to enclose every slice, so this test measures
    // the XY fit and nothing else. With tight bounds the far cascade correctly
    // stops at the edge of the scene, which is a different property - see
    // testDepthRangeSpansTheSceneNotTheSlice.
    const CascadeSetup setup = ShadowCascades::Build(
        camera, glm::vec3(0.6f, 1.0f, 0.5f), glm::vec3(-300.0f), glm::vec3(300.0f), 2048);

    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        const float sliceNear = (i == 0) ? camera.nearPlane : setup.splitDepth[i - 1];
        const float sliceFar = setup.splitDepth[i];
        const auto corners = ShadowCascades::SliceCorners(camera, sliceNear, sliceFar);

        for (const auto& corner : corners) {
            const glm::vec3 tiny(0.01f);
            CHECK_MSG(setup.frustum[i].IntersectsAABB(corner - tiny, corner + tiny),
                      "cascade " + std::to_string(i) + " must cover every corner of its own slice");
        }
    }
}

static void testCascadeCentresAreSnappedToTheTexelGrid() {
    // The anti-shimmer guarantee, stated as something measurable: sweeping the
    // camera across less than one texel of the near cascade must leave the
    // cascade transform on at most two distinct values, because it is quantised
    // rather than sliding continuously.
    const CameraComponent base = makeCamera();
    const CascadeSetup reference = ShadowCascades::Build(
        base, glm::vec3(0.6f, 1.0f, 0.5f), glm::vec3(-40.0f), glm::vec3(40.0f), 2048);

    const float texel = reference.texelWorldSize[0];
    CHECK_MSG(texel > 0.0f, "the near cascade must have a real texel size");

    std::vector<glm::mat4> distinct;
    constexpr int kSteps = 40;
    for (int step = 0; step < kSteps; ++step) {
        CameraComponent moved = base;
        // Total sweep is 0.4 of a texel, so at most one grid boundary is crossed.
        moved.position.x += texel * 0.4f * (static_cast<float>(step) / static_cast<float>(kSteps));
        moved.updateCameraVectors();

        const CascadeSetup setup = ShadowCascades::Build(
            moved, glm::vec3(0.6f, 1.0f, 0.5f), glm::vec3(-40.0f), glm::vec3(40.0f), 2048);

        bool seen = false;
        for (const auto& existing : distinct) {
            if (matricesEqual(existing, setup.viewProj[0])) { seen = true; break; }
        }
        if (!seen) distinct.push_back(setup.viewProj[0]);
    }

    CHECK_MSG(distinct.size() <= 2,
              "sub-texel camera movement must not move the cascade (" +
                  std::to_string(distinct.size()) + " distinct transforms over " +
                  std::to_string(kSteps) + " steps)");
}

static void testRotatingInPlaceKeepsTheCascadeExtent() {
    // Fitted to the bounding SPHERE of the slice, so turning the camera must not
    // change how much world a texel covers. Fitting a box directly makes the
    // extent breathe through the turn and the shadows crawl with it.
    const CascadeSetup a = ShadowCascades::Build(
        makeCamera(glm::vec3(0.0f, 2.0f, 10.0f), -90.0f),
        glm::vec3(0.6f, 1.0f, 0.5f), glm::vec3(-40.0f), glm::vec3(40.0f), 2048);
    const CascadeSetup b = ShadowCascades::Build(
        makeCamera(glm::vec3(0.0f, 2.0f, 10.0f), -35.0f),
        glm::vec3(0.6f, 1.0f, 0.5f), glm::vec3(-40.0f), glm::vec3(40.0f), 2048);

    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        CHECK_NEAR(a.texelWorldSize[i], b.texelWorldSize[i]);
    }
}

static void testDepthRangeSpansTheSceneNotTheSlice() {
    // A caster high above the camera's slice must still be inside the cascade,
    // because depth clamping is not enabled on this device and anything outside
    // the near plane is clipped away rather than flattened onto it.
    const CameraComponent camera = makeCamera();
    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f)); // straight overhead

    const CascadeSetup setup = ShadowCascades::Build(
        camera, lightDir, glm::vec3(-40.0f, 0.0f, -40.0f), glm::vec3(40.0f, 60.0f, 40.0f), 2048);

    // Directly above the MIDDLE of the near cascade's slice, and 55 units up -
    // well outside the slice's own depth extent but well inside the scene.
    // Placing it anywhere else tests the cascade's XY fit instead, which is a
    // different property.
    const float sliceMid = (camera.nearPlane + setup.splitDepth[0]) * 0.5f;
    const glm::vec3 high = camera.position + glm::normalize(camera.front) * sliceMid
                         + glm::vec3(0.0f, 55.0f, 0.0f);
    const glm::vec3 tiny(0.05f);

    CHECK_MSG(setup.frustum[0].IntersectsAABB(high - tiny, high + tiny),
              "a caster between the light and the slice must be inside the cascade, not clipped");
}

static void testDegenerateInputsProduceFiniteMatrices() {
    const CameraComponent camera = makeCamera();

    // Zero light direction, and a scene with no extent at all.
    const CascadeSetup setup = ShadowCascades::Build(
        camera, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), 2048);

    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!std::isfinite(setup.viewProj[i][column][row])) {
                    CHECK_MSG(false, "cascade " + std::to_string(i) + " produced a non-finite matrix");
                    return;
                }
            }
        }
        CHECK_MSG(setup.texelWorldSize[i] > 0.0f, "and a positive texel size");
    }
    CHECK(true);
}

static void testZeroResolutionDoesNotDivideByZero() {
    const CascadeSetup setup = ShadowCascades::Build(
        makeCamera(), glm::vec3(0.6f, 1.0f, 0.5f), glm::vec3(-10.0f), glm::vec3(10.0f), 0);

    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        CHECK_MSG(std::isfinite(setup.texelWorldSize[i]), "a zero resolution must not produce infinity");
    }
}

static void testShadowDistanceIsClampedToTheCamera() {
    CameraComponent camera = makeCamera();
    camera.farPlane = 30.0f;

    const CascadeSetup setup = ShadowCascades::Build(
        camera, glm::vec3(0.6f, 1.0f, 0.5f), glm::vec3(-40.0f), glm::vec3(40.0f), 2048, 500.0f);

    CHECK_MSG(setup.splitDepth[kShadowCascadeCount - 1] <= 30.0f + 1e-3f,
              "cascades must not be fitted past the camera's own far plane");
}

static void testAnOrthographicSliceIsABoxAndNotAPyramid() {
    // The perspective construction is `d * tan(fov/2)`, which widens the slice
    // with distance because the rays diverge. Orthographic rays are parallel:
    // the extent is orthoHeight at every depth, near and far alike.
    //
    // Using the perspective form under an ortho camera did not merely misfit
    // the cascade, it inverted its shape. A near slice sits at a small d, so
    // its corners collapsed towards the camera axis and the cascade covering
    // whatever is closest was fitted to nearly nothing - while the far slice
    // was handed a volume that grows without bound.
    CameraComponent camera = makeCamera(glm::vec3(0.0f, 0.0f, 0.0f), -90.0f);
    camera.projection = CameraComponent::Projection::Orthographic;
    camera.orthoHeight = 10.0f;
    camera.aspect = 2.0f;

    const auto nearSlice = ShadowCascades::SliceCorners(camera, 2.0f, 10.0f);
    const auto farSlice  = ShadowCascades::SliceCorners(camera, 40.0f, 80.0f);

    const float nearWidth = glm::length(nearSlice[1] - nearSlice[0]);
    const float farWidth  = glm::length(nearSlice[5] - nearSlice[4]);

    // orthoHeight 10, aspect 2 -> 20 wide, and the same at both planes.
    CHECK_NEAR(nearWidth, 20.0f);
    CHECK_MSG(std::fabs(farWidth - nearWidth) < 1e-3f,
              "an orthographic slice does not widen with depth: " + std::to_string(nearWidth) +
                  " then " + std::to_string(farWidth));

    const float nearHeight = glm::length(nearSlice[2] - nearSlice[0]);
    CHECK_NEAR(nearHeight, 10.0f);

    // And a slice forty units out is the same box as one two units out. Under
    // the perspective construction it was twenty times larger.
    CHECK_MSG(std::fabs(glm::length(farSlice[1] - farSlice[0]) - nearWidth) < 1e-3f,
              "a distant orthographic slice is the same size as a near one");

    // The centres still track the camera, so only the extents changed.
    glm::vec3 centre(0.0f);
    for (int i = 0; i < 4; ++i) centre += farSlice[static_cast<size_t>(i)];
    centre /= 4.0f;
    const glm::vec3 expected = camera.position + glm::normalize(camera.front) * 40.0f;
    CHECK_NEAR(centre.z, expected.z);
}

static void testAPerspectiveSliceStillWidensWithDepth() {
    // The control. A fix that made every camera orthographic would pass the
    // test above and break every 3D scene in the project, and "the extents are
    // constant" is exactly what that failure looks like.
    CameraComponent camera = makeCamera(glm::vec3(0.0f, 0.0f, 0.0f), -90.0f);
    camera.projection = CameraComponent::Projection::Perspective;

    const auto corners = ShadowCascades::SliceCorners(camera, 2.0f, 10.0f);
    const float nearWidth = glm::length(corners[1] - corners[0]);
    const float farWidth  = glm::length(corners[5] - corners[4]);

    CHECK_MSG(farWidth > nearWidth * 4.0f,
              "a perspective slice five times deeper is five times wider: " +
                  std::to_string(nearWidth) + " then " + std::to_string(farWidth));
}

static void runTests() {
    testAnOrthographicSliceIsABoxAndNotAPyramid();
    testAPerspectiveSliceStillWidensWithDepth();
    testSplitsIncreaseAndReachTheShadowDistance();
    testSplitsDoNotCollapseOntoTheNearPlane();
    testUniformAndLogarithmicSplitsDiffer();
    testSliceCornersMatchTheCameraFrustum();
    testNearCascadesHaveFinerTexelsThanFarOnes();
    testEachCascadeContainsItsOwnSlice();
    testCascadeCentresAreSnappedToTheTexelGrid();
    testRotatingInPlaceKeepsTheCascadeExtent();
    testDepthRangeSpansTheSceneNotTheSlice();
    testDegenerateInputsProduceFiniteMatrices();
    testZeroResolutionDoesNotDivideByZero();
    testShadowDistanceIsClampedToTheCamera();
}

TEST_MAIN("test_cascades", 50)
