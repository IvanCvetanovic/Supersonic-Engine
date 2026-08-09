#include "core/RenderSystem.hpp"

namespace Engine {

void RenderSystem::Render(
    entt::registry& registry,
    VulkanPipeline& pipeline,
    vk::CommandBuffer commandBuffer,
    vk::DescriptorSet descriptorSet,
    vk::Buffer vertexBuffer,
    vk::Buffer indexBuffer,
    uint32_t indexCount) {

    // Bind Graphics Pipeline
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.GetPipeline());

    // Bind Descriptor Set (UBO & Texture Sampler)
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipeline.GetLayout(),
        0,
        1,
        &descriptorSet,
        0,
        nullptr
    );

    // Bind Vertex & Index Buffers
    vk::Buffer vertexBuffers[] = { vertexBuffer };
    vk::DeviceSize offsets[] = { 0 };
    commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint16);

    // Query EnTT Registry for all entities with TransformComponent and RenderableComponent
    auto view = registry.view<TransformComponent, RenderableComponent>();
    for (auto entity : view) {
        const auto& transform = view.get<TransformComponent>(entity);
        const auto& renderable = view.get<RenderableComponent>(entity);

        if (!renderable.isVisible) continue;

        // Push per-entity Model Matrix via Push Constants
        PushConstantData pushData{};
        pushData.model = transform.getModelMatrix();

        commandBuffer.pushConstants(
            pipeline.GetLayout(),
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(PushConstantData),
            &pushData
        );

        // Issue Indexed Draw Call for entity
        commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
    }
}

} // namespace Engine
