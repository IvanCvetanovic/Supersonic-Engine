#pragma once

#include <entt/entt.hpp>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include "core/Components.hpp"
#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanPipeline.hpp"
#include "renderer/MeshRegistry.hpp"
#include "renderer/TextureRegistry.hpp"

namespace Supersonic {

class RenderSystem {
public:
    // Draws every visible entity using its own MeshComponent geometry,
    // MaterialComponent parameters and its own albedo texture, rather than one
    // hardcoded cube and one global checkerboard for everything.
    static void Render(
        entt::registry& registry,
        VulkanPipeline& pipeline,
        MeshRegistry& meshes,
        TextureRegistry& textures,
        vk::CommandBuffer commandBuffer,
        vk::DescriptorSet sceneSet
    );

    // Shadow pass: same geometry, no materials, no textures. Only positions
    // matter, so this binds nothing but the scene set for the light matrix.
    static void RenderDepthOnly(
        entt::registry& registry,
        VulkanPipeline& pipeline,
        MeshRegistry& meshes,
        vk::CommandBuffer commandBuffer,
        vk::DescriptorSet sceneSet
    );

    // Resolves MeshComponent descriptions and material texture paths to GPU
    // resources, uploading any that are new. Runs outside command buffer
    // recording because it performs transfers.
    static void SyncResources(entt::registry& registry, MeshRegistry& meshes, TextureRegistry& textures);
};

} // namespace Supersonic
