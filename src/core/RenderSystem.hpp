#pragma once

#include <entt/entt.hpp>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include "core/Components.hpp"
#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanPipeline.hpp"
#include "renderer/MeshRegistry.hpp"

namespace Engine {

class RenderSystem {
public:
    // Draws every visible entity using its own MeshComponent geometry and
    // MaterialComponent parameters, rather than one hardcoded cube for all.
    static void Render(
        entt::registry& registry,
        VulkanPipeline& pipeline,
        MeshRegistry& meshes,
        vk::CommandBuffer commandBuffer,
        vk::DescriptorSet descriptorSet
    );

    // Resolves MeshComponent descriptions to GPU meshes, uploading any that are
    // new. Runs outside command buffer recording because it performs transfers.
    static void SyncMeshes(entt::registry& registry, MeshRegistry& meshes);
};

} // namespace Engine
