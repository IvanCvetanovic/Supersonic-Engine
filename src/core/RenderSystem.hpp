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

    // Shadow pass for one cascade: same geometry, no materials, no textures.
    // Only positions matter, and the cascade's transform arrives in the push
    // constant, so this binds no descriptor set at all.
    static void RenderDepthOnly(
        entt::registry& registry,
        VulkanPipeline& pipeline,
        MeshRegistry& meshes,
        vk::CommandBuffer commandBuffer,
        vk::DescriptorSet sceneSet,
        const glm::mat4& cascadeViewProj,
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
