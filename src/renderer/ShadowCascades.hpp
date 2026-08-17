#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "core/Components.hpp"
#include "renderer/Frustum.hpp"

namespace Supersonic {

// Cascaded shadow maps.
//
// One 2048x2048 map covered a fixed 48-unit box around the origin. Anything
// outside it had no shadow at all, and inside it every texel spanned ~23mm of
// world space whether it was under the camera or forty units away - so near
// shadows were blocky and the map was mostly wasted on geometry nobody was
// looking at. Cascades spend the same texels where they are actually visible.
//
// Deliberately free of Vulkan: this is where the convention mistakes live, and
// keeping it to glm plus Components.hpp is what lets a test link it.
inline constexpr uint32_t kShadowCascadeCount = 4;

// Everything the renderer and the shader need for one frame's cascades.
struct CascadeSetup {
    // Light-space transform per cascade, used both to rasterise the depth pass
    // and to look the result up in the fragment shader.
    std::array<glm::mat4, kShadowCascadeCount> viewProj{};

    // View-space distance to each cascade's far plane. The shader picks a
    // cascade by comparing against these, and treats anything past the last one
    // as unshadowed.
    std::array<float, kShadowCascadeCount> splitDepth{};

    // World-space size of one shadow texel in each cascade. Drives the shader's
    // normal offset, which is what lets the rasteriser bias stay small.
    std::array<float, kShadowCascadeCount> texelWorldSize{};

    // Culling frustum per cascade for the depth pass.
    std::array<Frustum, kShadowCascadeCount> frustum{};
};

class ShadowCascades {
public:
    // Split distances, blending a uniform division with a logarithmic one.
    //
    // Purely logarithmic packs almost everything into the first cascade, which
    // starves the distance; purely uniform wastes the near cascades where the
    // detail is actually visible. lambda mixes them - 0 is uniform, 1 is
    // logarithmic.
    static std::array<float, kShadowCascadeCount> ComputeSplits(
        float nearPlane, float shadowDistance, float lambda = 0.85f);

    // Builds this frame's cascades.
    //
    // sceneMin/sceneMax are the world bounds of everything that can cast or
    // receive, and are used ONLY for the depth range along the light axis. They
    // deliberately do not influence the cascade extents: depth clamping is not
    // enabled on this device, so a near plane fitted tightly to a slice would
    // clip casters standing between the light and that slice and silently drop
    // their shadows.
    static CascadeSetup Build(const CameraComponent& camera,
                              const glm::vec3& lightDirection,
                              const glm::vec3& sceneMin,
                              const glm::vec3& sceneMax,
                              uint32_t resolution,
                              float shadowDistance = 60.0f,
                              float lambda = 0.85f);

    // The eight world-space corners of the camera frustum slice between two
    // view-space depths. Derived from the camera basis and field of view rather
    // than by unprojecting through inverse(proj * view): that would reintroduce
    // the zero-to-one depth range and the Vulkan Y flip as two more chances to
    // get a convention wrong.
    static std::array<glm::vec3, 8> SliceCorners(const CameraComponent& camera,
                                                 float nearDistance, float farDistance);

};

} // namespace Supersonic
