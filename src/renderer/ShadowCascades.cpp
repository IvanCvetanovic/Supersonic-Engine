#include "renderer/ShadowCascades.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

namespace Supersonic {

namespace {

// Padding on the fitted radius, applied BEFORE the radius is quantised and
// before the texel size is derived from it. The order matters: texel snapping
// displaces the cascade centre by up to one texel, and the 3x3 PCF kernel then
// reads one texel beyond that, so without the margin both walk off the edge of
// the fitted region.
constexpr float kRadiusPadding = 1.02f;

// The radius is quantised so it only changes in steps. A radius that varies
// continuously with camera position re-scales the projection every frame, which
// defeats the snapping and puts the shimmer straight back.
constexpr float kRadiusQuantum = 0.5f;

glm::vec3 safeLightDirection(const glm::vec3& lightDirection) {
    glm::vec3 dir = lightDirection;
    if (glm::length(dir) < 1e-4f) dir = glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(dir);
}

// Avoids a degenerate up vector when the light is straight overhead.
//
// This threshold is a discontinuity: crossing it flips the light basis, which
// rotates the snap grid and pops every cascade in one frame. Harmless for a
// static sun, and the reason a moving sun would need `up` derived from the
// smallest component of the direction instead.
glm::vec3 upFor(const glm::vec3& dir) {
    return std::abs(dir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                   : glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace

std::array<float, kShadowCascadeCount> ShadowCascades::ComputeSplits(
    float nearPlane, float shadowDistance, float lambda) {

    std::array<float, kShadowCascadeCount> splits{};

    const float nearD = std::max(nearPlane, 0.001f);
    const float farD = std::max(shadowDistance, nearD + 0.01f);
    const float range = farD - nearD;
    const float ratio = farD / nearD;

    lambda = std::clamp(lambda, 0.0f, 1.0f);

    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        // Both casts are required. i / kShadowCascadeCount in integer arithmetic
        // is 0 for every cascade but the last, which collapses every split onto
        // the near plane.
        const float p = static_cast<float>(i + 1) / static_cast<float>(kShadowCascadeCount);

        const float logSplit = nearD * std::pow(ratio, p);
        const float uniformSplit = nearD + range * p;

        splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }

    // The last cascade ends exactly at the shadow distance, so the shader's
    // "past the last split means unshadowed" test lines up with it.
    splits[kShadowCascadeCount - 1] = farD;
    return splits;
}

std::array<glm::vec3, 8> ShadowCascades::SliceCorners(const CameraComponent& camera,
                                                      float nearDistance, float farDistance) {
    // glm::perspective takes a VERTICAL field of view, so the vertical half
    // extent is the one derived directly and the horizontal one scales by the
    // aspect ratio.
    const float tanHalfV = std::tan(glm::radians(camera.fov) * 0.5f);
    const float tanHalfH = tanHalfV * std::max(camera.aspect, 0.0001f);

    // AN ORTHOGRAPHIC FRUSTUM IS A BOX, and this built a pyramid for it.
    //
    // The half extents below were `d * tanHalf`, which is the perspective
    // construction: the slice widens with distance because the rays diverge.
    // Under an orthographic projection they do not diverge at all - the extent
    // is `orthoHeight` at every depth, near and far alike.
    //
    // Using the perspective form anyway did not merely misfit the cascade, it
    // inverted its shape. The near slice sits at a small `d`, so its corners
    // collapsed towards the camera axis and the cascade covering the player was
    // fitted to almost nothing; the far slice was handed a volume that grows
    // without bound. The visible result is shadows missing near the camera and
    // a texel density far away that no resolution rescues - in a 2D scene, which
    // is what an orthographic camera is usually for, that is every shadow in the
    // frame.
    const bool orthographic = camera.isOrthographic();
    const float orthoHalfV = camera.orthoHeight * 0.5f;
    const float orthoHalfH = orthoHalfV * std::max(camera.aspect, 0.0001f);

    const glm::vec3 forward = glm::normalize(camera.front);
    const glm::vec3 right = glm::normalize(camera.right);
    const glm::vec3 up = glm::normalize(camera.up);

    std::array<glm::vec3, 8> corners{};
    const float distances[2] = { nearDistance, farDistance };

    for (int plane = 0; plane < 2; ++plane) {
        const float d = distances[plane];
        const glm::vec3 centre = camera.position + forward * d;
        const glm::vec3 halfUp = up * (orthographic ? orthoHalfV : d * tanHalfV);
        const glm::vec3 halfRight = right * (orthographic ? orthoHalfH : d * tanHalfH);

        corners[static_cast<size_t>(plane) * 4 + 0] = centre - halfUp - halfRight;
        corners[static_cast<size_t>(plane) * 4 + 1] = centre - halfUp + halfRight;
        corners[static_cast<size_t>(plane) * 4 + 2] = centre + halfUp - halfRight;
        corners[static_cast<size_t>(plane) * 4 + 3] = centre + halfUp + halfRight;
    }
    return corners;
}

CascadeSetup ShadowCascades::Build(const CameraComponent& camera,
                                   const glm::vec3& lightDirection,
                                   const glm::vec3& sceneMin,
                                   const glm::vec3& sceneMax,
                                   uint32_t resolution,
                                   float shadowDistance,
                                   float lambda) {
    CascadeSetup setup{};

    const glm::vec3 dir = safeLightDirection(lightDirection);
    const float dim = static_cast<float>(resolution == 0 ? 1 : resolution);

    const float shadowFar = std::min(std::max(shadowDistance, camera.nearPlane + 1.0f), camera.farPlane);
    setup.splitDepth = ComputeSplits(camera.nearPlane, shadowFar, lambda);

    // World -> light space, about the ORIGIN. Constant for the whole frame, so
    // nothing downstream of it can drift with the camera.
    const glm::mat4 toLight = glm::lookAt(dir, glm::vec3(0.0f), upFor(dir));

    // Depth range along the light axis, measured over the whole scene rather
    // than per cascade.
    //
    // Two reasons. Depth clamping is not enabled on this device, so a near plane
    // fitted to a slice would clip casters standing between the light and that
    // slice and silently drop their shadows. And a per-cascade range would move
    // with the camera, which would re-scale the depth row of every cascade
    // transform every frame and undo the snapping below.
    float minDepth = std::numeric_limits<float>::max();
    float maxDepth = std::numeric_limits<float>::lowest();
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 point(
            (corner & 1) ? sceneMax.x : sceneMin.x,
            (corner & 2) ? sceneMax.y : sceneMin.y,
            (corner & 4) ? sceneMax.z : sceneMin.z);
        // Light space looks down -Z, so distance along the view direction is -z.
        const float depth = -(toLight * glm::vec4(point, 1.0f)).z;
        minDepth = std::min(minDepth, depth);
        maxDepth = std::max(maxDepth, depth);
    }
    if (!(maxDepth > minDepth)) {
        // Degenerate or empty scene bounds must not produce an inverted range.
        minDepth = -1.0f;
        maxDepth = 1.0f;
    }
    // Slack on each side so geometry exactly on the bound is not lost to
    // floating-point error.
    minDepth -= 1.0f;
    maxDepth += 1.0f;

    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        const float sliceNear = (i == 0) ? camera.nearPlane : setup.splitDepth[i - 1];
        const float sliceFar = setup.splitDepth[i];

        const auto corners = SliceCorners(camera, sliceNear, sliceFar);

        // Bounding sphere of the slice: its centre is the mean of the corners
        // and its radius the furthest of them. A sphere is invariant under
        // camera rotation, so the extent does not breathe as the camera turns -
        // fitting a box in light space directly does breathe, and the shadows
        // crawl with it.
        glm::vec3 centre(0.0f);
        for (const auto& corner : corners) centre += corner;
        centre /= static_cast<float>(corners.size());

        float radius = 0.0f;
        for (const auto& corner : corners) {
            radius = std::max(radius, glm::length(corner - centre));
        }

        // Pad, then quantise, then derive the texel from the FINAL radius.
        // Deriving it before either step leaves the snap grid mismatched against
        // the grid actually sampled, and the crawl comes straight back.
        radius *= kRadiusPadding;
        radius = std::ceil(radius / kRadiusQuantum) * kRadiusQuantum;
        radius = std::max(radius, kRadiusQuantum);

        const float texelWorld = (2.0f * radius) / dim;
        setup.texelWorldSize[i] = texelWorld;

        // Snap the centre to the shadow map's own texel grid, in light space.
        // Without this the sampled grid slides continuously with the camera and
        // every shadow edge visibly crawls.
        const glm::vec3 centreLight = glm::vec3(toLight * glm::vec4(centre, 1.0f));
        const float snappedX = std::floor(centreLight.x / texelWorld) * texelWorld;
        const float snappedY = std::floor(centreLight.y / texelWorld) * texelWorld;

        // Re-centre in light space on X and Y only. Leaving Z alone is what
        // keeps the depth range - and therefore the projection's depth row -
        // identical across all four cascades and across frames.
        const glm::mat4 view =
            glm::translate(glm::mat4(1.0f), glm::vec3(-snappedX, -snappedY, 0.0f)) * toLight;

        // Symmetric, so the Vulkan Y flip stays a single negated element. An
        // off-centre ortho would also need its Y translation negated, and
        // forgetting that mirrors every shadow vertically.
        glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, minDepth, maxDepth);
        proj[1][1] *= -1.0f;

        setup.viewProj[i] = proj * view;
        setup.frustum[i] = Frustum::FromMatrix(setup.viewProj[i]);
    }

    return setup;
}

} // namespace Supersonic
