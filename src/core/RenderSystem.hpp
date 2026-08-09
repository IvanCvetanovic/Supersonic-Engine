#pragma once

#include <entt/entt.hpp>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include "core/Components.hpp"
#include "renderer/VulkanDevice.hpp"
#include "renderer/VulkanPipeline.hpp"

namespace Engine {

class RenderSystem {
public:
    static void Render(
        entt::registry& registry,
        VulkanPipeline& pipeline,
        vk::CommandBuffer commandBuffer,
        vk::DescriptorSet descriptorSet,
        vk::Buffer vertexBuffer,
        vk::Buffer indexBuffer,
        uint32_t indexCount
    );
};

} // namespace Engine
