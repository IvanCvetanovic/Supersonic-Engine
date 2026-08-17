#include "core/RenderSystem.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

void RenderSystem::SyncMeshes(entt::registry& registry, MeshRegistry& meshes) {
    // Uploads happen here, before recording starts, because MeshRegistry submits
    // transfer command buffers of its own.
    for (auto entity : registry.view<RenderableComponent>()) {
        auto& renderable = registry.get<RenderableComponent>(entity);

        if (const auto* mesh = registry.try_get<MeshComponent>(entity)) {
            renderable.meshID = meshes.Acquire(mesh->primitiveType, mesh->filePath);
        } else if (renderable.meshID == MeshRegistry::kInvalidMesh) {
            renderable.meshID = meshes.GetCubeMesh();
        }

        // Keep the picking bounds in step with the resolved geometry.
        if (const GpuMesh* gpuMesh = meshes.Get(renderable.meshID)) {
            renderable.localBoundsMin = gpuMesh->boundsMin;
            renderable.localBoundsMax = gpuMesh->boundsMax;
        }
    }
}

void RenderSystem::Render(
    entt::registry& registry,
    VulkanPipeline& pipeline,
    MeshRegistry& meshes,
    vk::CommandBuffer commandBuffer,
    vk::DescriptorSet descriptorSet) {

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.GetPipeline());

    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipeline.GetLayout(),
        0, 1, &descriptorSet,
        0, nullptr);

    // Avoid redundant vertex/index buffer rebinds when consecutive entities
    // share a mesh, which is the common case.
    uint32_t boundMesh = MeshRegistry::kInvalidMesh;

    auto view = registry.view<TransformComponent, RenderableComponent>();
    for (auto entity : view) {
        const auto& transform = view.get<TransformComponent>(entity);
        const auto& renderable = view.get<RenderableComponent>(entity);

        if (!renderable.isVisible) continue;

        const GpuMesh* mesh = meshes.Get(renderable.meshID);
        if (!mesh || mesh->indexCount == 0) continue;

        if (renderable.meshID != boundMesh) {
            const vk::Buffer vertexBuffers[] = { mesh->vertexBuffer->GetBuffer() };
            const vk::DeviceSize offsets[] = { 0 };
            commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
            commandBuffer.bindIndexBuffer(mesh->indexBuffer->GetBuffer(), 0, vk::IndexType::eUint32);
            boundMesh = renderable.meshID;
        }

        PushConstantData pushData{};
        pushData.model = transform.getModelMatrix();

        if (const auto* material = registry.try_get<MaterialComponent>(entity)) {
            pushData.albedoColor = material->albedoColor;
            pushData.material = glm::vec4(material->roughness, material->metallic, material->ao, 0.0f);
        } else {
            pushData.albedoColor = glm::vec4(1.0f);
            pushData.material = glm::vec4(0.4f, 0.1f, 1.0f, 0.0f);
        }

        commandBuffer.pushConstants(
            pipeline.GetLayout(),
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            sizeof(PushConstantData),
            &pushData);

        commandBuffer.drawIndexed(mesh->indexCount, 1, 0, 0, 0);
    }

    // Particles. Previously simulated into a pool nothing ever read, so no
    // particle could appear on screen. Drawn as small cubes reusing the scene
    // pipeline, shrinking as they age.
    const GpuMesh* particleMesh = meshes.Get(meshes.GetCubeMesh());
    if (!particleMesh || particleMesh->indexCount == 0) return;

    bool particleMeshBound = false;
    for (auto entity : registry.view<ParticleEmitterComponent>()) {
        const auto& emitter = registry.get<ParticleEmitterComponent>(entity);

        for (const auto& particle : emitter.particles) {
            if (!particle.active) continue;

            if (!particleMeshBound) {
                const vk::Buffer vertexBuffers[] = { particleMesh->vertexBuffer->GetBuffer() };
                const vk::DeviceSize offsets[] = { 0 };
                commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
                commandBuffer.bindIndexBuffer(particleMesh->indexBuffer->GetBuffer(), 0, vk::IndexType::eUint32);
                particleMeshBound = true;
            }

            const float age = particle.maxLifetime > 0.0f ? particle.lifetime / particle.maxLifetime : 0.0f;
            const float size = emitter.particleSize * glm::clamp(age, 0.15f, 1.0f);

            PushConstantData pushData{};
            pushData.model = glm::scale(glm::translate(glm::mat4(1.0f), particle.position), glm::vec3(size));
            pushData.albedoColor = particle.color;
            // Fully rough and emissive-ish so particles read as glowing motes.
            pushData.material = glm::vec4(1.0f, 0.0f, 1.0f, 0.0f);

            commandBuffer.pushConstants(
                pipeline.GetLayout(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, sizeof(PushConstantData), &pushData);

            commandBuffer.drawIndexed(particleMesh->indexCount, 1, 0, 0, 0);
        }
    }
}

} // namespace Engine
