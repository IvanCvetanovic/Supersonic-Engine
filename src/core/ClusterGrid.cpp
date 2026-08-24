#include "core/ClusterGrid.hpp"

#include <algorithm>
#include <cmath>

namespace Supersonic {

namespace ClusterGrid {

namespace {

// The near plane cannot be zero and the far plane cannot equal it: the slice
// distribution is a logarithm of their ratio, and both cases divide by zero.
// Clamped rather than asserted, because a camera with a silly near plane should
// light the scene badly rather than take the process down.
void sanitisePlanes(float& nearPlane, float& farPlane) {
    nearPlane = std::max(nearPlane, 1e-4f);
    farPlane = std::max(farPlane, nearPlane * 1.0001f);
}

// The froxel's extent along one screen axis, as a slab in view space at a given
// depth. The frustum widens with distance, so the slab is computed at the depth
// the caller passes in.
void axisBounds(uint32_t tile, uint32_t tileCount, float halfExtentAtDepth,
                float& outMin, float& outMax) {
    const float span = 2.0f * halfExtentAtDepth;
    const float step = span / static_cast<float>(tileCount);
    outMin = -halfExtentAtDepth + step * static_cast<float>(tile);
    outMax = outMin + step;
}

// Squared distance from a point to an axis-aligned box, which is the standard
// sphere-versus-box test with the square root left off.
float distanceSquaredToBox(const glm::vec3& point, const glm::vec3& boxMin,
                           const glm::vec3& boxMax) {
    float total = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        const float value = point[axis];
        if (value < boxMin[axis]) {
            const float delta = boxMin[axis] - value;
            total += delta * delta;
        } else if (value > boxMax[axis]) {
            const float delta = value - boxMax[axis];
            total += delta * delta;
        }
    }
    return total;
}

// The froxel as an axis-aligned box in view space.
//
// A froxel is not actually a box - it is a frustum slab, wider at its far face
// than its near one - so this takes the union of the two faces. That is
// deliberately CONSERVATIVE: it can call a light present in a cluster it only
// grazes, which costs a wasted iteration, and it can never drop one it reaches,
// which would be a dark patch in a shape nobody could explain.
void clusterBounds(uint32_t x, uint32_t y, uint32_t z,
                   float nearPlane, float farPlane,
                   float tanHalfFovY, float aspect,
                   glm::vec3& outMin, glm::vec3& outMax) {
    float sliceNear = 0.0f;
    float sliceFar = 0.0f;
    SliceDepthRange(z, nearPlane, farPlane, sliceNear, sliceFar);

    // Evaluated at BOTH faces and unioned, because the frustum widens.
    const float halfYNear = tanHalfFovY * sliceNear;
    const float halfYFar = tanHalfFovY * sliceFar;

    float minXNear, maxXNear, minXFar, maxXFar;
    axisBounds(x, kTilesX, halfYNear * aspect, minXNear, maxXNear);
    axisBounds(x, kTilesX, halfYFar * aspect, minXFar, maxXFar);

    float minYNear, maxYNear, minYFar, maxYFar;
    axisBounds(y, kTilesY, halfYNear, minYNear, maxYNear);
    axisBounds(y, kTilesY, halfYFar, minYFar, maxYFar);

    outMin = glm::vec3(std::min(minXNear, minXFar), std::min(minYNear, minYFar), sliceNear);
    outMax = glm::vec3(std::max(maxXNear, maxXFar), std::max(maxYNear, maxYFar), sliceFar);
}

} // namespace

uint32_t SliceForDepth(float viewZ, float nearPlane, float farPlane) {
    sanitisePlanes(nearPlane, farPlane);

    if (viewZ <= nearPlane) return 0;
    if (viewZ >= farPlane) return kSlices - 1;

    // The standard exponential distribution: slice = log(z/near) / log(far/near),
    // scaled to the slice count. Uniform slices would put almost every froxel out
    // where the frustum is enormous and nothing is standing, and squeeze the near
    // field - where the lamps are - into the first one.
    const float ratio = std::log(viewZ / nearPlane) / std::log(farPlane / nearPlane);
    const auto slice = static_cast<uint32_t>(ratio * static_cast<float>(kSlices));
    return std::min(slice, kSlices - 1);
}

void SliceDepthRange(uint32_t slice, float nearPlane, float farPlane,
                     float& outNear, float& outFar) {
    sanitisePlanes(nearPlane, farPlane);
    slice = std::min(slice, kSlices - 1);

    const float ratio = farPlane / nearPlane;
    const auto exponent = [&](uint32_t index) {
        return nearPlane * std::pow(ratio, static_cast<float>(index) / static_cast<float>(kSlices));
    };

    outNear = exponent(slice);
    outFar = exponent(slice + 1);
}

uint32_t ClusterForFragment(const glm::vec2& fragCoord, float viewZ,
                            const glm::vec2& targetSize,
                            float nearPlane, float farPlane) {
    const glm::vec2 size = glm::max(targetSize, glm::vec2(1.0f));
    const glm::vec2 tileSize = size / glm::vec2(static_cast<float>(kTilesX),
                                                static_cast<float>(kTilesY));

    // Y IS FLIPPED. The projection multiplies its second row by -1 for Vulkan,
    // so view-space +Y - up - lands at fragCoord.y = 0, the TOP of the image,
    // while the froxel bounds above number their rows bottom-first because that
    // is the space they are computed in. Left unflipped, every light is looked
    // up in the row mirrored about the horizon: a lamp lighting the floor lights
    // the ceiling instead, and the scene still looks lit.
    const glm::vec2 gridCoord(fragCoord.x, size.y - fragCoord.y);

    const auto tileX = static_cast<uint32_t>(
        std::clamp(gridCoord.x / tileSize.x, 0.0f, static_cast<float>(kTilesX - 1)));
    const auto tileY = static_cast<uint32_t>(
        std::clamp(gridCoord.y / tileSize.y, 0.0f, static_cast<float>(kTilesY - 1)));

    const uint32_t slice = SliceForDepth(viewZ, nearPlane, farPlane);
    return tileX + tileY * kTilesX + slice * kTilesX * kTilesY;
}

bool SphereTouchesCluster(const LocalLight& light, uint32_t x, uint32_t y, uint32_t z,
                          float nearPlane, float farPlane,
                          float tanHalfFovY, float aspect) {
    if (x >= kTilesX || y >= kTilesY || z >= kSlices) return false;
    if (light.radius <= 0.0f) return false;

    glm::vec3 boxMin(0.0f);
    glm::vec3 boxMax(0.0f);
    clusterBounds(x, y, z, nearPlane, farPlane, tanHalfFovY, aspect, boxMin, boxMax);

    // The light's position is in view space with z as positive distance in
    // front of the camera, matching the box the bounds above describe.
    return distanceSquaredToBox(light.viewPosition, boxMin, boxMax) <=
           light.radius * light.radius;
}

Assignment Assign(const std::vector<LocalLight>& lights,
                  float nearPlane, float farPlane,
                  float tanHalfFovY, float aspect) {
    sanitisePlanes(nearPlane, farPlane);

    Assignment out;
    out.clusters.assign(kClusterCount, ClusterRange{});
    out.indices.clear();
    out.indices.reserve(std::min<size_t>(kMaxLightIndices, lights.size() * 32));

    // Each light's DEPTH SPAN, worked out once instead of per froxel.
    //
    // Without it this is kClusterCount x lights box tests - three and a half
    // thousand multiply-adds per light, almost all of them against froxels the
    // light is nowhere near - in a feature whose entire justification is not
    // paying for lights that cannot reach you. A lamp with a two-metre range
    // touches two or three of the twenty-four slices, so an integer compare
    // ahead of the box test removes about nine tenths of the work.
    struct Span { uint32_t minSlice; uint32_t maxSlice; };
    std::vector<Span> spans;
    spans.reserve(lights.size());
    for (const LocalLight& light : lights) {
        Span span{1, 0};  // deliberately empty: min > max skips every slice
        if (light.radius > 0.0f) {
            span.minSlice = SliceForDepth(light.viewPosition.z - light.radius, nearPlane, farPlane);
            span.maxSlice = SliceForDepth(light.viewPosition.z + light.radius, nearPlane, farPlane);
        }
        spans.push_back(span);
    }

    // Cluster-major, so each cluster's indices are contiguous and the range is
    // an offset and a length rather than a linked list. The loop is over
    // clusters on the outside and lights on the inside for exactly that reason:
    // the other order would need a second pass to compact.
    for (uint32_t z = 0; z < kSlices; ++z) {
        for (uint32_t y = 0; y < kTilesY; ++y) {
            for (uint32_t x = 0; x < kTilesX; ++x) {
                const uint32_t cluster = x + y * kTilesX + z * kTilesX * kTilesY;
                ClusterRange& range = out.clusters[cluster];
                range.offset = static_cast<uint32_t>(out.indices.size());
                range.count = 0;

                glm::vec3 boxMin(0.0f);
                glm::vec3 boxMax(0.0f);
                clusterBounds(x, y, z, nearPlane, farPlane, tanHalfFovY, aspect, boxMin, boxMax);

                for (size_t i = 0; i < lights.size(); ++i) {
                    const LocalLight& light = lights[i];
                    if (light.radius <= 0.0f) continue;

                    // Two integer compares before any arithmetic. Conservative
                    // by construction: the span is taken from the light's own
                    // sphere, so a slice it is skipped for is one the box test
                    // would have rejected anyway.
                    if (z < spans[i].minSlice || z > spans[i].maxSlice) continue;

                    if (distanceSquaredToBox(light.viewPosition, boxMin, boxMax) >
                        light.radius * light.radius) {
                        continue;
                    }

                    if (out.indices.size() >= kMaxLightIndices) {
                        ++out.dropped;
                        continue;
                    }
                    out.indices.push_back(static_cast<uint32_t>(i));
                    ++range.count;
                }
            }
        }
    }

    return out;
}

} // namespace ClusterGrid

} // namespace Supersonic
