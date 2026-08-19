#include "core/RenderSystem.hpp"

#include <algorithm>

#include <limits>
#include "core/TransformSystem.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Supersonic {

namespace {

PushConstantData buildPushConstants(const entt::registry& registry, entt::entity entity,
                                    const glm::mat4& model) {
    PushConstantData push{};
    push.model = model;

    if (const auto* material = registry.try_get<MaterialComponent>(entity)) {
        push.albedoColor = material->albedoColor;
        push.material = glm::vec4(material->roughness, material->metallic, material->ao, 0.0f);
    } else {
        push.albedoColor = glm::vec4(1.0f);
        push.material = glm::vec4(0.4f, 0.1f, 1.0f, 0.0f);
    }

    // Both passes go through this one function, so the shadow pass skins with
    // no further change. The -1 default stands for everything else.
    if (const auto* skin = registry.try_get<SkinnedMeshComponent>(entity)) {
        push.skinPaletteBase = skin->paletteBase;
        push.skinJointCount = static_cast<int32_t>(skin->jointMatrices.size());
    }
    return push;
}

} // namespace

void RenderSystem::SyncResources(entt::registry& registry, MeshRegistry& meshes, TextureRegistry& textures) {
    // Uploads happen here, before recording starts, because both registries
    // submit transfer command buffers of their own.
    for (auto entity : registry.view<RenderableComponent>()) {
        auto& renderable = registry.get<RenderableComponent>(entity);

        if (const auto* mesh = registry.try_get<MeshComponent>(entity)) {
            renderable.meshID = meshes.Acquire(mesh->primitiveType, mesh->filePath);
        } else if (renderable.meshID == MeshRegistry::kInvalidMesh) {
            renderable.meshID = meshes.GetCubeMesh();
        }

        if (const GpuMesh* gpuMesh = meshes.Get(renderable.meshID)) {
            renderable.localBoundsMin = gpuMesh->boundsMin;
            renderable.localBoundsMax = gpuMesh->boundsMax;
        }

        // Both texture paths used to be fields nothing read.
        if (const auto* material = registry.try_get<MaterialComponent>(entity)) {
            renderable.albedoTextureID = material->albedoTexturePath.empty()
                                       ? textures.GetWhiteTexture()
                                       : textures.Acquire(material->albedoTexturePath, true);
            // srgb=false: a normal map holds directions, not colour, so it must
            // not be gamma-decoded on read.
            renderable.normalTextureID = material->normalTexturePath.empty()
                                       ? textures.GetFlatNormalTexture()
                                       : textures.Acquire(material->normalTexturePath, false);
        } else {
            renderable.albedoTextureID = textures.GetWhiteTexture();
            renderable.normalTextureID = textures.GetFlatNormalTexture();
        }
    }
}

void RenderSystem::RenderDepthOnly(
    entt::registry& registry,
    VulkanPipeline& pipeline,
    MeshRegistry& meshes,
    vk::CommandBuffer commandBuffer,
    vk::DescriptorSet sceneSet,
    const glm::mat4& cascadeViewProj,
    const Frustum& lightFrustum,
    Stats& stats) {

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.GetPipeline());
    // Bound for the joint palette at binding 2. The depth pass reads nothing
    // else from set 0 - the cascade transform arrives premultiplied in the push
    // constant - but a skinned draw cannot skin without it.
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.GetLayout(),
                                     VulkanPipeline::kSceneSet, 1, &sceneSet, 0, nullptr);

    uint32_t boundMesh = MeshRegistry::kInvalidMesh;

    auto view = registry.view<WorldTransformComponent, RenderableComponent>();
    for (auto entity : view) {
        const auto& world = view.get<WorldTransformComponent>(entity);
        const auto& renderable = view.get<RenderableComponent>(entity);

        if (!renderable.isVisible || !renderable.castsShadow) continue;

        const GpuMesh* mesh = meshes.Get(renderable.meshID);
        if (!mesh || mesh->indexCount == 0) continue;

        // Cull against the LIGHT's frustum here, not the camera's: an object
        // behind the viewer can still cast a shadow into view.
        glm::vec3 worldMin, worldMax;
        Frustum::TransformAABB(world.matrix, renderable.localBoundsMin, renderable.localBoundsMax,
                               worldMin, worldMax);
        if (!lightFrustum.IntersectsAABB(worldMin, worldMax)) {
            ++stats.shadowCulled;
            continue;
        }
        ++stats.shadowDrawn;

        if (renderable.meshID != boundMesh) {
            const vk::Buffer buffers[] = { mesh->vertexBuffer->GetBuffer() };
            const vk::DeviceSize offsets[] = { 0 };
            commandBuffer.bindVertexBuffers(0, 1, buffers, offsets);
            commandBuffer.bindIndexBuffer(mesh->indexBuffer->GetBuffer(), 0, vk::IndexType::eUint32);
            boundMesh = renderable.meshID;
        }

        // World matrix so a child follows its parent, plus this cascade's
        // transform - the depth pass has no other use for the scene UBO.
        // Premultiplied on the CPU: two matrices would be the whole 128-byte
        // push constant budget, leaving nothing for the skinning indices. The
        // per-vertex skin matrix still composes correctly on the right.
        ShadowPushConstantData push{};
        push.viewProjModel = cascadeViewProj * world.matrix;
        if (const auto* skin = registry.try_get<SkinnedMeshComponent>(entity)) {
            push.skinPaletteBase = skin->paletteBase;
            push.skinJointCount = static_cast<int32_t>(skin->jointMatrices.size());
        }
        commandBuffer.pushConstants(
            pipeline.GetLayout(), vk::ShaderStageFlagBits::eVertex,
            0, sizeof(ShadowPushConstantData), &push);

        commandBuffer.drawIndexed(mesh->indexCount, 1, 0, 0, 0);
    }
}

bool RenderSystem::ComputeSceneBounds(entt::registry& registry, MeshRegistry& meshes,
                                      glm::vec3& outMin, glm::vec3& outMax) {
    constexpr float big = std::numeric_limits<float>::max();
    outMin = glm::vec3(big);
    outMax = glm::vec3(-big);

    bool any = false;
    auto view = registry.view<WorldTransformComponent, RenderableComponent>();
    for (auto entity : view) {
        const auto& renderable = view.get<RenderableComponent>(entity);
        if (!renderable.isVisible) continue;

        const GpuMesh* mesh = meshes.Get(renderable.meshID);
        if (!mesh || mesh->indexCount == 0) continue;

        glm::vec3 worldMin, worldMax;
        Frustum::TransformAABB(view.get<WorldTransformComponent>(entity).matrix,
                               renderable.localBoundsMin, renderable.localBoundsMax,
                               worldMin, worldMax);
        outMin = glm::min(outMin, worldMin);
        outMax = glm::max(outMax, worldMax);
        any = true;
    }
    return any;
}

void RenderSystem::Render(
    entt::registry& registry,
    VulkanPipeline& pipeline,
    VulkanPipeline& transparentPipeline,
    MeshRegistry& meshes,
    TextureRegistry& textures,
    vk::CommandBuffer commandBuffer,
    vk::DescriptorSet sceneSet,
    const Frustum& frustum,
    const glm::vec3& viewPosition,
    Stats& stats) {

    // Transparent entities are collected during the opaque walk and drawn after
    // it, sorted back to front. Blending is order-dependent: two overlapping
    // panes drawn in the wrong order composite in the wrong order, and the
    // result is wrong rather than merely differently wrong.
    struct TransparentDraw {
        entt::entity entity{entt::null};
        const GpuMesh* mesh{nullptr};
        glm::mat4 matrix{1.0f};
        uint32_t albedoTextureID{0};
        uint32_t normalTextureID{0};
        float distanceSquared{0.0f};
    };
    std::vector<TransparentDraw> transparent;

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.GetPipeline());

    // Set 0 is per-frame and bound once.
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.GetLayout(),
                                     VulkanPipeline::kSceneSet, 1, &sceneSet, 0, nullptr);

    // Avoid redundant rebinds when consecutive entities share a mesh or
    // texture, which is the common case.
    uint32_t boundMesh = MeshRegistry::kInvalidMesh;
    vk::DescriptorSet boundMaterialSet{};

    auto view = registry.view<WorldTransformComponent, RenderableComponent>();
    for (auto entity : view) {
        const auto& world = view.get<WorldTransformComponent>(entity);
        const auto& renderable = view.get<RenderableComponent>(entity);

        if (!renderable.isVisible) continue;

        const GpuMesh* mesh = meshes.Get(renderable.meshID);
        if (!mesh || mesh->indexCount == 0) continue;

        glm::vec3 worldMin, worldMax;
        Frustum::TransformAABB(world.matrix, renderable.localBoundsMin, renderable.localBoundsMax,
                               worldMin, worldMax);
        if (!frustum.IntersectsAABB(worldMin, worldMax)) {
            ++stats.culled;
            continue;
        }
        ++stats.drawn;

        // Diverted AFTER culling, so an off-screen pane costs nothing and the
        // culled count stays honest.
        if (const auto* material = registry.try_get<MaterialComponent>(entity);
            material && material->transparent) {
            const glm::vec3 centre = (worldMin + worldMax) * 0.5f;
            const glm::vec3 toView = centre - viewPosition;
            transparent.push_back(TransparentDraw{
                entity, mesh, world.matrix,
                renderable.albedoTextureID, renderable.normalTextureID,
                glm::dot(toView, toView)});
            continue;
        }

        if (renderable.meshID != boundMesh) {
            const vk::Buffer buffers[] = { mesh->vertexBuffer->GetBuffer() };
            const vk::DeviceSize offsets[] = { 0 };
            commandBuffer.bindVertexBuffers(0, 1, buffers, offsets);
            commandBuffer.bindIndexBuffer(mesh->indexBuffer->GetBuffer(), 0, vk::IndexType::eUint32);
            boundMesh = renderable.meshID;
        }

        // One set per (albedo, normal) pair, cached, so entities sharing a
        // material do not rebind.
        if (vk::DescriptorSet materialSet =
                textures.AcquireMaterialSet(renderable.albedoTextureID, renderable.normalTextureID);
            materialSet && materialSet != boundMaterialSet) {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.GetLayout(),
                                             VulkanPipeline::kMaterialSet, 1, &materialSet, 0, nullptr);
            boundMaterialSet = materialSet;
        }

        // World matrix, so a child follows its parent.
        const PushConstantData push = buildPushConstants(registry, entity, world.matrix);
        commandBuffer.pushConstants(
            pipeline.GetLayout(),
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, sizeof(PushConstantData), &push);

        commandBuffer.drawIndexed(mesh->indexCount, 1, 0, 0, 0);
    }

    // ---- Transparent pass ------------------------------------------------
    //
    // Back to front, by squared distance to the view. Squared because the
    // ordering is all that matters and a square root per draw buys nothing.
    if (!transparent.empty()) {
        std::sort(transparent.begin(), transparent.end(),
                  [](const TransparentDraw& lhs, const TransparentDraw& rhs) {
                      return lhs.distanceSquared > rhs.distanceSquared;
                  });

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                   transparentPipeline.GetPipeline());
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         transparentPipeline.GetLayout(),
                                         VulkanPipeline::kSceneSet, 1, &sceneSet, 0, nullptr);

        // The rebind-avoidance state belongs to the pipeline that was bound, so
        // it resets here rather than carrying over from the opaque pass.
        boundMesh = MeshRegistry::kInvalidMesh;
        boundMaterialSet = vk::DescriptorSet{};

        for (const auto& draw : transparent) {
            if (draw.mesh->indexCount == 0) continue;

            const vk::Buffer buffers[] = { draw.mesh->vertexBuffer->GetBuffer() };
            const vk::DeviceSize offsets[] = { 0 };
            commandBuffer.bindVertexBuffers(0, 1, buffers, offsets);
            commandBuffer.bindIndexBuffer(draw.mesh->indexBuffer->GetBuffer(), 0,
                                          vk::IndexType::eUint32);

            if (vk::DescriptorSet materialSet =
                    textures.AcquireMaterialSet(draw.albedoTextureID, draw.normalTextureID);
                materialSet && materialSet != boundMaterialSet) {
                commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                 transparentPipeline.GetLayout(),
                                                 VulkanPipeline::kMaterialSet, 1, &materialSet,
                                                 0, nullptr);
                boundMaterialSet = materialSet;
            }

            const PushConstantData push = buildPushConstants(registry, draw.entity, draw.matrix);
            commandBuffer.pushConstants(
                transparentPipeline.GetLayout(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, sizeof(PushConstantData), &push);

            commandBuffer.drawIndexed(draw.mesh->indexCount, 1, 0, 0, 0);
        }

        // Back to the opaque pipeline, because the particle block below assumes
        // it is bound.
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.GetPipeline());
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.GetLayout(),
                                         VulkanPipeline::kSceneSet, 1, &sceneSet, 0, nullptr);
        boundMesh = MeshRegistry::kInvalidMesh;
        boundMaterialSet = vk::DescriptorSet{};
    }

    // Particles. Drawn as small cubes reusing the scene pipeline, shrinking as
    // they age. They were previously simulated into a pool nothing ever read.
    const GpuMesh* particleMesh = meshes.Get(meshes.GetCubeMesh());
    if (!particleMesh || particleMesh->indexCount == 0) return;

    bool particleMeshBound = false;
    for (auto entity : registry.view<ParticleEmitterComponent>()) {
        const auto& emitter = registry.get<ParticleEmitterComponent>(entity);

        for (const auto& particle : emitter.particles) {
            if (!particle.active) continue;

            if (!particleMeshBound) {
                const vk::Buffer buffers[] = { particleMesh->vertexBuffer->GetBuffer() };
                const vk::DeviceSize offsets[] = { 0 };
                commandBuffer.bindVertexBuffers(0, 1, buffers, offsets);
                commandBuffer.bindIndexBuffer(particleMesh->indexBuffer->GetBuffer(), 0, vk::IndexType::eUint32);

                if (vk::DescriptorSet whiteSet = textures.AcquireMaterialSet(
                        textures.GetWhiteTexture(), textures.GetFlatNormalTexture())) {
                    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.GetLayout(),
                                                     VulkanPipeline::kMaterialSet, 1, &whiteSet, 0, nullptr);
                }
                particleMeshBound = true;
            }

            const float age = particle.maxLifetime > 0.0f ? particle.lifetime / particle.maxLifetime : 0.0f;
            const float size = emitter.particleSize * glm::clamp(age, 0.15f, 1.0f);

            PushConstantData push{};
            push.model = glm::scale(glm::translate(glm::mat4(1.0f), particle.position), glm::vec3(size));
            push.albedoColor = particle.color;
            push.material = glm::vec4(1.0f, 0.0f, 1.0f, 0.0f);

            commandBuffer.pushConstants(
                pipeline.GetLayout(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, sizeof(PushConstantData), &push);

            commandBuffer.drawIndexed(particleMesh->indexCount, 1, 0, 0, 0);
        }
    }
}

} // namespace Supersonic
