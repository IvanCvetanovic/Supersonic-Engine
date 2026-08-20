#pragma once

#include <entt/entt.hpp>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include "core/Components.hpp"
#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanPipeline.hpp"
#include "renderer/MeshRegistry.hpp"
#include "renderer/TextureRegistry.hpp"
#include "renderer/Frustum.hpp"

namespace Supersonic {

class RenderSystem {
public:
    // Per-pass counters, surfaced in the editor so culling is measurable
    // rather than something you have to take on faith.
    struct Stats {
        uint32_t drawn{0};
        uint32_t culled{0};
        // Summed across every cascade, so with four cascades an object visible
        // in two of them counts twice - which is what it costs.
        uint32_t shadowDrawn{0};
        uint32_t shadowCulled{0};

        // Joint matrices uploaded this frame, across every skinned entity.
        uint32_t skinnedMatrices{0};
    };

    // Draws every visible entity using its own MeshComponent geometry,
    // MaterialComponent parameters and its own albedo texture, rather than one
    // hardcoded cube and one global checkerboard for everything.
    // Opaque geometry first, then transparent back-to-front.
    //
    // Two passes rather than one sorted list: opaque draws want to be grouped
    // by material to avoid rebinding, and transparent draws must be ordered by
    // distance instead. Those are different orders and cannot both be had from
    // one traversal.
    static void Render(
        entt::registry& registry,
        VulkanPipeline& pipeline,
        VulkanPipeline& transparentPipeline,
        MeshRegistry& meshes,
        TextureRegistry& textures,
        vk::CommandBuffer commandBuffer,
        vk::DescriptorSet sceneSet,
        const Frustum& frustum,
        const glm::vec3& viewPosition,
        Stats& stats
    );

    // One entity, resolved down to what a depth pass actually needs.
    //
    // A frame runs EIGHTEEN depth passes - four cascades, six faces for each
    // point-light slot, one for each spot slot - and every one of them used to
    // walk the registry from scratch: two component lookups, a mesh-registry
    // lookup, a try_get for the skin, and eight corner transforms to build the
    // world bounds. All of that is identical in all eighteen. The only thing
    // that differs between passes is which frustum the bounds are tested
    // against.
    //
    // So it is gathered once and the passes read it. That is the difference
    // between eighteen registry traversals per frame and one.
    struct ShadowCaster {
        glm::mat4 model{1.0f};

        // WORLD bounds, already transformed. This is the eight-corner
        // transform that was being redone per pass.
        glm::vec3 worldMin{0.0f};
        glm::vec3 worldMax{0.0f};

        // Resolved once. Safe to hold for the frame because the mesh registry
        // is write-once and every upload has already happened by the time
        // DrawFrame runs - SyncResources is called before it, deliberately.
        const GpuMesh* mesh{nullptr};
        uint32_t meshID{0};

        uint32_t skinPaletteBase{0};
        int32_t skinJointCount{0};
    };

    // Everything visible that casts a shadow, in registry order. Clears `out`
    // and refills it, so a caller can keep one vector for the life of the
    // renderer and never allocate again after the first frame.
    static void GatherShadowCasters(entt::registry& registry, MeshRegistry& meshes,
                                    std::vector<ShadowCaster>& out);

    // Shadow pass for one cascade, cube face or spot: same geometry, no
    // materials, no textures. Only positions matter, and the light's transform
    // arrives in the push constant, so this binds no descriptor set at all.
    static void RenderDepthOnly(
        const std::vector<ShadowCaster>& casters,
        VulkanPipeline& pipeline,
        vk::CommandBuffer commandBuffer,
        vk::DescriptorSet sceneSet,
        const glm::mat4& lightViewProj,
        const Frustum& lightFrustum,
        Stats& stats
    );

    // World bounds of everything that can cast or receive, used to fit the
    // cascades' depth range along the light axis. Returns false when the scene
    // has nothing renderable in it.
    static bool ComputeSceneBounds(entt::registry& registry, MeshRegistry& meshes,
                                   glm::vec3& outMin, glm::vec3& outMax);

    // Resolves MeshComponent descriptions and material texture paths to GPU
    // resources, uploading any that are new. Runs outside command buffer
    // recording because it performs transfers.
    static void SyncResources(entt::registry& registry, MeshRegistry& meshes, TextureRegistry& textures);
};

} // namespace Supersonic
