// Regression tests for the froxel grid that lifted the eight-light cap.
//
// Every failure here is silent and shaped like nothing. A light missing from a
// cluster it reaches is a dark rectangle on a wall, the size of one screen tile,
// that moves when the camera turns. A light present in a cluster it does not
// reach costs a wasted iteration and looks perfectly correct. A slice boundary
// off by one is a seam at a particular distance from the camera that nobody can
// reproduce without standing in the right place.
//
// None of that is visible in a screenshot of a scene with three lamps in it,
// which is what makes the arithmetic worth testing on its own - and why it lives
// in core/ with no Vulkan in it, like LightSelection beside it.

#include "TestHarness.hpp"
#include "core/ClusterGrid.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

using namespace Supersonic;
using ClusterGrid::LocalLight;

namespace {

// A 60-degree vertical field of view at 16:9, which is roughly what the sample
// scene uses.
constexpr float kNear = 0.1f;
constexpr float kFar = 100.0f;
const float kTanHalfFovY = std::tan(glm::radians(60.0f) * 0.5f);
constexpr float kAspect = 16.0f / 9.0f;

LocalLight lightAt(float x, float y, float z, float radius) {
    LocalLight light;
    light.viewPosition = glm::vec3(x, y, z);
    light.radius = radius;
    return light;
}

// Every cluster a light actually landed in, read back out of the packed lists.
std::vector<uint32_t> clustersHolding(const ClusterGrid::Assignment& assignment, uint32_t light) {
    std::vector<uint32_t> found;
    for (uint32_t cluster = 0; cluster < ClusterGrid::kClusterCount; ++cluster) {
        const auto& range = assignment.clusters[cluster];
        for (uint32_t i = 0; i < range.count; ++i) {
            if (assignment.indices[range.offset + i] == light) {
                found.push_back(cluster);
                break;
            }
        }
    }
    return found;
}

} // namespace

static void testSlicesCoverTheFrustumWithNoGapAndNoOverlap() {
    // The property the whole lookup rests on: every depth in front of the camera
    // lands in exactly one slice, and the slices meet exactly. A gap is a band of
    // the world lit by nothing.
    float previousFar = 0.0f;
    for (uint32_t slice = 0; slice < ClusterGrid::kSlices; ++slice) {
        float sliceNear = 0.0f;
        float sliceFar = 0.0f;
        ClusterGrid::SliceDepthRange(slice, kNear, kFar, sliceNear, sliceFar);

        CHECK_MSG(sliceFar > sliceNear, "a slice must have depth");
        if (slice == 0) {
            CHECK_NEAR(sliceNear, kNear);
        } else {
            CHECK_MSG(std::fabs(sliceNear - previousFar) < 1e-3f,
                      "each slice must start exactly where the last one ended");
        }
        previousFar = sliceFar;
    }
    CHECK_MSG(std::fabs(previousFar - kFar) < 1e-2f,
              "and the last one must reach the far plane");
}

static void testSlicesAreExponentialNotUniform() {
    // A uniform division puts almost every froxel out where the frustum is
    // enormous and nothing is standing, and crams the near field - where the
    // lamps are - into the first slice.
    float firstNear = 0.0f, firstFar = 0.0f;
    float lastNear = 0.0f, lastFar = 0.0f;
    ClusterGrid::SliceDepthRange(0, kNear, kFar, firstNear, firstFar);
    ClusterGrid::SliceDepthRange(ClusterGrid::kSlices - 1, kNear, kFar, lastNear, lastFar);

    const float firstDepth = firstFar - firstNear;
    const float lastDepth = lastFar - lastNear;
    CHECK_MSG(lastDepth > firstDepth * 10.0f,
              "the far slice must be far deeper than the near one, or this is a uniform grid");
}

static void testDepthLandsInTheSliceThatContainsIt() {
    // The round trip. SliceForDepth and SliceDepthRange are two descriptions of
    // one division, and they are exactly the pair that drifts.
    for (uint32_t slice = 0; slice < ClusterGrid::kSlices; ++slice) {
        float sliceNear = 0.0f, sliceFar = 0.0f;
        ClusterGrid::SliceDepthRange(slice, kNear, kFar, sliceNear, sliceFar);

        const float middle = std::sqrt(sliceNear * sliceFar);  // geometric, to match the spacing
        CHECK_MSG(ClusterGrid::SliceForDepth(middle, kNear, kFar) == slice,
                  "a depth inside a slice must resolve to that slice");
    }
}

static void testDepthOutsideTheFrustumStillLandsSomewhere() {
    // Not a nicety. An out-of-range index reads another cluster's lights, so
    // something a hair in front of the near plane would be lit by whatever
    // happened to sit at that offset.
    CHECK_EQ(ClusterGrid::SliceForDepth(-5.0f, kNear, kFar), 0u);
    CHECK_EQ(ClusterGrid::SliceForDepth(0.0f, kNear, kFar), 0u);
    CHECK_EQ(ClusterGrid::SliceForDepth(kNear * 0.5f, kNear, kFar), 0u);
    CHECK_EQ(ClusterGrid::SliceForDepth(kFar * 10.0f, kNear, kFar), ClusterGrid::kSlices - 1u);
    CHECK_EQ(ClusterGrid::SliceForDepth(kFar, kNear, kFar), ClusterGrid::kSlices - 1u);
}

static void testDegeneratePlanesDoNotDivideByZero() {
    // The slice distribution is a logarithm of far/near, so a zero near plane or
    // equal planes are both a division by zero. A camera authored with silly
    // numbers should light its scene badly, not take the process down.
    CHECK_EQ(ClusterGrid::SliceForDepth(1.0f, 0.0f, 0.0f), ClusterGrid::kSlices - 1u);
    float a = 0.0f, b = 0.0f;
    ClusterGrid::SliceDepthRange(3, 0.0f, 0.0f, a, b);
    CHECK_MSG(b > a, "even a degenerate frustum must produce an ordered slice");
}

static void testALightReachesTheClusterItSitsIn() {
    // The floor of the whole feature: a lamp must light the froxel it is inside.
    const float depth = 5.0f;
    std::vector<LocalLight> lights{ lightAt(0.0f, 0.0f, depth, 1.0f) };

    const auto assignment = ClusterGrid::Assign(lights, kNear, kFar, kTanHalfFovY, kAspect);
    const auto found = clustersHolding(assignment, 0);
    CHECK_MSG(!found.empty(), "a light must appear in at least the cluster it is standing in");

    const uint32_t slice = ClusterGrid::SliceForDepth(depth, kNear, kFar);
    bool inItsOwnSlice = false;
    for (const uint32_t cluster : found) {
        if (cluster / (ClusterGrid::kTilesX * ClusterGrid::kTilesY) == slice) inItsOwnSlice = true;
    }
    CHECK_MSG(inItsOwnSlice, "and specifically in the depth slice it occupies");
}

static void testATinyLightDoesNotReachTheWholeScreen() {
    // The point of culling. A pinpoint lamp in the middle of the view must not
    // end up in every cluster, or the grid costs memory and buys nothing.
    std::vector<LocalLight> lights{ lightAt(0.0f, 0.0f, 5.0f, 0.05f) };
    const auto assignment = ClusterGrid::Assign(lights, kNear, kFar, kTanHalfFovY, kAspect);

    const auto found = clustersHolding(assignment, 0);
    CHECK_MSG(!found.empty(), "it still has to light something");
    CHECK_MSG(found.size() < ClusterGrid::kClusterCount / 8,
              "a 5cm lamp must not be in a large fraction of the frustum");
}

static void testAHugeLightReachesEverything() {
    // The other end, and the one that catches an inverted test: a light whose
    // radius swallows the frustum must be in every cluster.
    std::vector<LocalLight> lights{ lightAt(0.0f, 0.0f, 1.0f, 10000.0f) };
    const auto assignment = ClusterGrid::Assign(lights, kNear, kFar, kTanHalfFovY, kAspect);

    CHECK_EQ(clustersHolding(assignment, 0).size(), size_t{ClusterGrid::kClusterCount});
}

static void testALightBehindTheCameraLightsNothingInFront() {
    // View space with z as distance in FORWARD of the camera. Getting the sign
    // backwards puts every lamp behind the viewer into the near slice, which
    // looks like the scene being lit from the wrong side and is one character.
    std::vector<LocalLight> lights{ lightAt(0.0f, 0.0f, -20.0f, 1.0f) };
    const auto assignment = ClusterGrid::Assign(lights, kNear, kFar, kTanHalfFovY, kAspect);

    CHECK_MSG(clustersHolding(assignment, 0).empty(),
              "a lamp twenty metres behind the camera must not reach the frustum");
}

static void testALightOffToOneSideStaysOnThatSide() {
    // Catches a swapped x/y or an inverted tile index, which a symmetric test
    // cannot see at all.
    const float depth = 8.0f;
    const float halfWidth = kTanHalfFovY * depth * kAspect;
    std::vector<LocalLight> lights{ lightAt(halfWidth * 0.8f, 0.0f, depth, 0.3f) };

    const auto assignment = ClusterGrid::Assign(lights, kNear, kFar, kTanHalfFovY, kAspect);
    const auto found = clustersHolding(assignment, 0);
    CHECK_MSG(!found.empty(), "it is inside the frustum and must light something");

    for (const uint32_t cluster : found) {
        const uint32_t x = cluster % ClusterGrid::kTilesX;
        CHECK_MSG(x >= ClusterGrid::kTilesX / 2,
                  "a light in the right half of the view must not reach the left half");
    }
}

static void testTheRangesDescribeTheListTheyIndex() {
    // Structural, and the one that catches a compaction bug: every range must
    // point inside the list, and the ranges must tile it without overlapping.
    std::vector<LocalLight> lights{
        lightAt(0.0f, 0.0f, 3.0f, 2.0f),
        lightAt(2.0f, 1.0f, 9.0f, 4.0f),
        lightAt(-3.0f, -1.0f, 20.0f, 8.0f),
    };
    const auto assignment = ClusterGrid::Assign(lights, kNear, kFar, kTanHalfFovY, kAspect);

    CHECK_EQ(assignment.clusters.size(), size_t{ClusterGrid::kClusterCount});
    CHECK_EQ(assignment.dropped, 0u);

    // Accumulated rather than asserted per cluster. Three thousand four
    // hundred and fifty-six CHECKs here would swamp the suite's minimum-check
    // floor, and a floor that every case clears on its own cannot catch the
    // one thing it exists for - a case quietly deleted.
    size_t walked = 0;
    bool contiguous = true;
    bool inBounds = true;
    bool namesRealLights = true;
    for (const auto& range : assignment.clusters) {
        if (range.offset != walked) contiguous = false;
        if (range.offset + range.count > assignment.indices.size()) inBounds = false;
        for (uint32_t i = 0; i < range.count; ++i) {
            if (assignment.indices[range.offset + i] >= lights.size()) namesRealLights = false;
        }
        walked += range.count;
    }
    CHECK_MSG(contiguous, "each cluster's slice must start where the last one ended");
    CHECK_MSG(inBounds, "and must not run off the end of the list");
    CHECK_MSG(namesRealLights, "every index must name a light that exists");
    CHECK_EQ(walked, assignment.indices.size());
}

static void testALightWithNoRadiusIsNotAssigned() {
    // A range of zero is how a light is turned off, and a zero-radius sphere
    // touching a box it is inside would put it in every cluster it stood in.
    std::vector<LocalLight> lights{ lightAt(0.0f, 0.0f, 5.0f, 0.0f) };
    const auto assignment = ClusterGrid::Assign(lights, kNear, kFar, kTanHalfFovY, kAspect);
    CHECK_MSG(clustersHolding(assignment, 0).empty(), "a light with no range lights nothing");
}

static void testTheClusterPredicateAgreesWithTheAssignment() {
    // Assign is a loop over the predicate, and a test that only reads the packed
    // lists cannot say which of the two was wrong. This pins them together.
    const LocalLight light = lightAt(1.0f, 0.5f, 6.0f, 2.0f);
    const auto assignment = ClusterGrid::Assign({light}, kNear, kFar, kTanHalfFovY, kAspect);

    int checked = 0;
    for (uint32_t z = 0; z < ClusterGrid::kSlices; z += 5) {
        for (uint32_t y = 0; y < ClusterGrid::kTilesY; y += 3) {
            for (uint32_t x = 0; x < ClusterGrid::kTilesX; x += 5) {
                const uint32_t cluster = x + y * ClusterGrid::kTilesX +
                                         z * ClusterGrid::kTilesX * ClusterGrid::kTilesY;
                const bool packed = assignment.clusters[cluster].count > 0;
                const bool predicate = ClusterGrid::SphereTouchesCluster(
                    light, x, y, z, kNear, kFar, kTanHalfFovY, kAspect);
                CHECK_MSG(packed == predicate,
                          "the packed list and the predicate must agree about every cluster");
                ++checked;
            }
        }
    }
    CHECK_MSG(checked > 30, "the sweep must have looked at a spread of clusters");
}

static void testAnOutOfRangeClusterIsNotTouched() {
    const LocalLight light = lightAt(0.0f, 0.0f, 5.0f, 1000.0f);
    CHECK_MSG(!ClusterGrid::SphereTouchesCluster(light, ClusterGrid::kTilesX, 0, 0,
                                                 kNear, kFar, kTanHalfFovY, kAspect),
              "a tile index past the edge must be refused, not wrapped");
    CHECK_MSG(!ClusterGrid::SphereTouchesCluster(light, 0, 0, ClusterGrid::kSlices,
                                                 kNear, kFar, kTanHalfFovY, kAspect),
              "and so must a slice past the far plane");
}

// --- the radius means what it says -----------------------------------------
//
// Every case above is a one-sided existence check, and a review of this suite
// against deliberately broken copies of the implementation found all of them
// green with the radius halved, and green again with it multiplied by ten. A
// passing run was not evidence the sphere test was right, only that it was
// monotone. These two pin the actual distance.

static void testTheRadiusIsExactAtASliceFace() {
    float sliceNear = 0.0f, sliceFar = 0.0f;
    ClusterGrid::SliceDepthRange(10, kNear, kFar, sliceNear, sliceFar);

    // Straight down the middle of the view, so x and y cannot be what decides.
    const uint32_t midX = ClusterGrid::kTilesX / 2;
    const uint32_t midY = ClusterGrid::kTilesY / 2;

    const float gap = 1.0f;
    const LocalLight justShort = lightAt(0.0f, 0.0f, sliceNear - gap, gap * 0.9f);
    const LocalLight justReaches = lightAt(0.0f, 0.0f, sliceNear - gap, gap * 1.1f);

    CHECK_MSG(!ClusterGrid::SphereTouchesCluster(justShort, midX, midY, 10,
                                                 kNear, kFar, kTanHalfFovY, kAspect),
              "a sphere stopping short of the slice must not reach into it");
    CHECK_MSG(ClusterGrid::SphereTouchesCluster(justReaches, midX, midY, 10,
                                                kNear, kFar, kTanHalfFovY, kAspect),
              "and one reaching past that face must");
}

static void testTheRadiusIsExactAcrossTheScreen() {
    // The same knife edge on the X axis, which catches a radius scaled only in
    // depth and a half-extent taken at the wrong face of the slice.
    const float depth = 10.0f;
    const uint32_t slice = ClusterGrid::SliceForDepth(depth, kNear, kFar);

    float sliceNear = 0.0f, sliceFar = 0.0f;
    ClusterGrid::SliceDepthRange(slice, kNear, kFar, sliceNear, sliceFar);

    // The left edge of the middle tile at the WIDEST face of the slice, which is
    // the bound the box actually uses.
    const float halfWidth = kTanHalfFovY * sliceFar * kAspect;
    const float tileWidth = 2.0f * halfWidth / static_cast<float>(ClusterGrid::kTilesX);
    const uint32_t midX = ClusterGrid::kTilesX / 2;
    const uint32_t midY = ClusterGrid::kTilesY / 2;
    const float tileMinX = -halfWidth + tileWidth * static_cast<float>(midX);

    const float gap = tileWidth * 0.25f;
    const LocalLight shy = lightAt(tileMinX - gap, 0.0f, depth, gap * 0.8f);
    const LocalLight reaching = lightAt(tileMinX - gap, 0.0f, depth, gap * 1.2f);

    CHECK_MSG(!ClusterGrid::SphereTouchesCluster(shy, midX, midY, slice,
                                                 kNear, kFar, kTanHalfFovY, kAspect),
              "a sphere short of the tile edge must not reach across it");
    CHECK_MSG(ClusterGrid::SphereTouchesCluster(reaching, midX, midY, slice,
                                                kNear, kFar, kTanHalfFovY, kAspect),
              "and one reaching past it must");
}

// --- the screen-space half, which used to exist only in GLSL ---------------

static void testTheTopOfTheImageIsTheTopOfTheGrid() {
    // THE bug this feature shipped with in its first draft: the projection flips
    // Y for Vulkan, so gl_FragCoord.y = 0 is the TOP of the image while the grid
    // numbers its rows in view space, bottom first. Unflipped, a lamp lighting
    // the floor lights the ceiling instead - and the scene still looks lit,
    // which is what made it invisible in every scene whose lamps reach every
    // froxel anyway.
    const glm::vec2 target(1600.0f, 900.0f);

    const uint32_t top = ClusterGrid::ClusterForFragment(
        glm::vec2(800.0f, 10.0f), 10.0f, target, kNear, kFar);
    const uint32_t bottom = ClusterGrid::ClusterForFragment(
        glm::vec2(800.0f, 890.0f), 10.0f, target, kNear, kFar);

    const uint32_t topRow = (top / ClusterGrid::kTilesX) % ClusterGrid::kTilesY;
    const uint32_t bottomRow = (bottom / ClusterGrid::kTilesX) % ClusterGrid::kTilesY;

    CHECK_MSG(topRow == ClusterGrid::kTilesY - 1,
              "the top of the image must be the HIGHEST row in view space");
    CHECK_EQ(bottomRow, 0u);
}

static void testAFragmentAndALightAgreeAboutTheirCluster() {
    // The round trip, and the only thing tying the two halves of the mapping
    // together: put a small light at a known view-space point, project it the
    // way the engine's own camera does, and check that the fragment it lands on
    // resolves to a cluster the light was actually assigned to.
    const glm::vec2 target(1600.0f, 900.0f);
    const float depth = 12.0f;

    glm::mat4 proj = glm::perspective(glm::radians(60.0f), kAspect, kNear, kFar);
    proj[1][1] *= -1.0f;   // exactly what CameraComponent::getProjectionMatrix does

    const float ys[3] = { -2.0f, 0.0f, 2.0f };
    const float xs[3] = { -4.0f, 0.0f, 4.0f };

    int checked = 0;
    for (const float y : ys) {
        for (const float x : xs) {
            const LocalLight light = lightAt(x, y, depth, 0.6f);

            // The grid's view space has z FORWARD; the projection wants the
            // right-handed form, so z is negated on the way in.
            const glm::vec4 clip = proj * glm::vec4(x, y, -depth, 1.0f);
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            const glm::vec2 fragCoord((ndc.x * 0.5f + 0.5f) * target.x,
                                      (ndc.y * 0.5f + 0.5f) * target.y);

            const uint32_t cluster =
                ClusterGrid::ClusterForFragment(fragCoord, depth, target, kNear, kFar);

            const auto assignment =
                ClusterGrid::Assign({light}, kNear, kFar, kTanHalfFovY, kAspect);
            CHECK_MSG(assignment.clusters[cluster].count == 1,
                      "the froxel a fragment resolves to must hold the light standing in it");
            ++checked;
        }
    }
    CHECK_EQ(checked, 9);
}

static void runTests() {
    testSlicesCoverTheFrustumWithNoGapAndNoOverlap();
    testSlicesAreExponentialNotUniform();
    testDepthLandsInTheSliceThatContainsIt();
    testDepthOutsideTheFrustumStillLandsSomewhere();
    testDegeneratePlanesDoNotDivideByZero();

    testALightReachesTheClusterItSitsIn();
    testATinyLightDoesNotReachTheWholeScreen();
    testAHugeLightReachesEverything();
    testALightBehindTheCameraLightsNothingInFront();
    testALightOffToOneSideStaysOnThatSide();

    testTheRangesDescribeTheListTheyIndex();
    testALightWithNoRadiusIsNotAssigned();
    testTheClusterPredicateAgreesWithTheAssignment();
    testAnOutOfRangeClusterIsNotTouched();

    testTheRadiusIsExactAtASliceFace();
    testTheRadiusIsExactAcrossTheScreen();
    testTheTopOfTheImageIsTheTopOfTheGrid();
    testAFragmentAndALightAgreeAboutTheirCluster();
}

TEST_MAIN("test_clustergrid", 55)
