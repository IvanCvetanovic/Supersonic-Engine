#include "core/RenderSystem.hpp"

#include <algorithm>

#include <limits>
#include "core/TransformSystem.hpp"
#include "core/MaterialSystem.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Supersonic {

namespace {

PushConstantData buildPushConstants(const entt::registry& registry, entt::entity entity,
                                    const glm::mat4& model) {
    PushConstantData push{};
    push.model = model;

    if (const auto* material = registry.try_get<MaterialComponent>(entity)) {
        push.albedoColor = material->albedoColor;
        // w is the alpha cutoff. Zero is no cutout, which is also what every
        // caller that builds a push constant by hand leaves it at.
        push.material = glm::vec4(material->roughness, material->metallic, material->ao,
                                  material->alphaCutoff);
        push.emissive = glm::vec4(material->emissiveColor * material->emissiveStrength, 0.0f);
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

uint64_t RenderSystem::ResourceSignature(const MeshComponent* mesh,
                                         const MaterialComponent* material,
                                         uint64_t meshGeneration,
                                         uint64_t textureGeneration) {
    uint64_t signature = MixSignature(1469598103934665603ull, &meshGeneration,
                                      sizeof(meshGeneration));
    signature = MixSignature(signature, &textureGeneration, sizeof(textureGeneration));

    // A marker per field, so "no MeshComponent" cannot hash the same as one
    // holding empty strings - they resolve to different meshes.
    const unsigned char present = 1;
    const unsigned char absent = 0;

    if (mesh) {
        signature = MixSignature(signature, &present, 1);
        signature = MixSignature(signature, mesh->primitiveType.data(),
                                 mesh->primitiveType.size());
        signature = MixSignature(signature, &present, 1);
        signature = MixSignature(signature, mesh->filePath.data(), mesh->filePath.size());
    } else {
        signature = MixSignature(signature, &absent, 1);
    }

    if (material) {
        signature = MixSignature(signature, &present, 1);
        signature = MixSignature(signature, material->albedoTexturePath.data(),
                                 material->albedoTexturePath.size());
        signature = MixSignature(signature, &present, 1);
        signature = MixSignature(signature, material->normalTexturePath.data(),
                                 material->normalTexturePath.size());
    } else {
        signature = MixSignature(signature, &absent, 1);
    }

    // Zero is what "never resolved" means on the component, so it must not be
    // an answer. Folding it onto one rather than reserving a bit: the collision
    // this introduces is between two inputs one of which hashes to zero, and
    // costs a redundant re-resolve rather than a stale one.
    return signature == 0 ? 1ull : signature;
}

void RenderSystem::SyncResources(entt::registry& registry, MeshRegistry& meshes, TextureRegistry& textures) {
    // Uploads happen here, before recording starts, because both registries
    // submit transfer command buffers of their own.
    const uint64_t meshGeneration = meshes.Generation();
    const uint64_t textureGeneration = textures.Generation();

    auto view = registry.view<RenderableComponent>();
    for (auto entity : view) {
        auto& renderable = view.get<RenderableComponent>(entity);

        auto* meshComponent = registry.try_get<MeshComponent>(entity);
        const auto* materialComponent = registry.try_get<MaterialComponent>(entity);

        // Nothing that decides these ids has changed, so neither have they.
        // Only the RESOLUTION is skipped - three hash-map lookups, each building
        // its key by concatenating strings.
        const uint64_t signature = ResourceSignature(meshComponent, materialComponent,
                                                     meshGeneration, textureGeneration);
        const bool resolve = signature != renderable.resourceSignature;

        if (resolve) {
            renderable.resourceSignature = signature;

            if (const auto* mesh = meshComponent) {
                renderable.meshID = meshes.Acquire(mesh->primitiveType, mesh->filePath);
            } else if (renderable.meshID == MeshRegistry::kInvalidMesh) {
                renderable.meshID = meshes.GetCubeMesh();
            }
        }

        // A model was assigned in the inspector and the file has now been
        // parsed, so what it says about its own surface can be copied onto the
        // entity. Exactly once, and only ever because someone asked: see the
        // comment on the flag for why this is not a standing rule.
        //
        // Folded into this loop rather than given a walk of its own - a second
        // pass over every entity every frame to service a flag set by a drag
        // would cost more than the feature is worth.
        if (meshComponent && meshComponent->importMaterialOnResolve) {
            meshComponent->importMaterialOnResolve = false;

            if (const MeshMaterial* imported = meshes.GetMaterial(renderable.meshID);
                imported && imported->present) {
                if (auto* material = registry.try_get<MaterialComponent>(entity)) {
                    MaterialSystem::ApplyImportedMaterial(*imported, *material);

                    // The ids above were resolved from the material this just
                    // replaced. Zero is what "never resolved" means, so the
                    // next frame re-reads the textures the file named.
                    renderable.resourceSignature = 0;
                }
            }
        }

        // NOT gated, and the comment on SkinnedMeshComponent::bindBoundsMin is
        // why: the pose bounds are a union against the bind box, and they are
        // only safe from feeding back into themselves because this line resets
        // them from the static mesh every frame. That comment says in as many
        // words that the coupling "would break silently the moment that refresh
        // was gated on anything". The first draft of this change gated it.
        //
        // It costs nothing to leave out of the skip. Get is a bounds check and
        // an index into a vector; it is Acquire above that builds strings.
        if (const GpuMesh* gpuMesh = meshes.Get(renderable.meshID)) {
            renderable.localBoundsMin = gpuMesh->boundsMin;
            renderable.localBoundsMax = gpuMesh->boundsMax;
        }

        if (!resolve) continue;

        // Both texture paths used to be fields nothing read.
        if (const auto* material = materialComponent) {
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

RenderSystem::ShadowAlpha RenderSystem::ShadowAlphaFor(const MaterialComponent* material) {
    ShadowAlpha alpha{};
    if (!material) return alpha;

    // A cutoff decides, whether or not the surface is also blended. The two are
    // documented as meaning something together - blend what survives the cut -
    // and what survives the cut is exactly what should cast. Reading
    // `transparent` first would quietly overrule an authored cutoff.
    if (material->alphaCutoff > 0.0f) {
        alpha.cutoff = material->alphaCutoff;
        alpha.baseAlpha = material->albedoColor.a;
        return alpha;
    }

    // Blended, and with nothing said about where it stops. It does not cast.
    //
    // `transparent` has exactly one meaning in this renderer - the blended
    // pipeline, which turns depth writes OFF - and a surface that declines to
    // occlude in the camera's depth buffer has no business occluding in the
    // light's. It cannot cast a partial shadow either: a shadow map records
    // "blocked" or "not blocked" and has no third answer, so the choice is
    // between nothing and a solid black rectangle, and the rectangle is the one
    // that is definitely wrong.
    //
    // Not silently final: a pane that should cast where it is solid says so
    // with a cutoff, which is the line above.
    alpha.casts = !material->transparent;
    return alpha;
}

void RenderSystem::GatherShadowCasters(entt::registry& registry, MeshRegistry& meshes,
                                       TextureRegistry& textures,
                                       std::vector<ShadowCaster>& out) {
    out.clear();

    auto view = registry.view<WorldTransformComponent, RenderableComponent>();
    for (auto entity : view) {
        const auto& world = view.get<WorldTransformComponent>(entity);
        const auto& renderable = view.get<RenderableComponent>(entity);

        if (!renderable.isVisible || !renderable.castsShadow) continue;

        // Before the mesh lookup and the eight-corner transform, so a pane of
        // glass costs one try_get rather than the whole caster.
        const ShadowAlpha alpha = ShadowAlphaFor(registry.try_get<MaterialComponent>(entity));
        if (!alpha.casts) continue;

        const GpuMesh* mesh = meshes.Get(renderable.meshID);
        if (!mesh || mesh->indexCount == 0) continue;

        ShadowCaster caster;
        caster.model = world.matrix;
        caster.vertexBuffer = mesh->vertexBuffer->GetBuffer();
        caster.indexBuffer = mesh->indexBuffer->GetBuffer();
        caster.indexCount = mesh->indexCount;
        caster.meshID = renderable.meshID;

        // The eight-corner transform, done once for the frame instead of once
        // per pass. The bounds do not depend on which light is looking.
        Frustum::TransformAABB(world.matrix, renderable.localBoundsMin,
                               renderable.localBoundsMax, caster.worldMin, caster.worldMax);

        if (const auto* skin = registry.try_get<SkinnedMeshComponent>(entity)) {
            caster.skinPaletteBase = skin->paletteBase;
            caster.skinJointCount = static_cast<int32_t>(skin->jointMatrices.size());
        }

        if (alpha.cutoff > 0.0f) {
            // Resolved HERE rather than in the depth pass, and that is not a
            // convenience. AcquireMaterialSet allocates, updates and can throw;
            // the depth pass runs eighteen times a frame with a render pass
            // open, and throwing out of the middle of one is not a failure this
            // engine could report. Once per frame, outside every render pass,
            // it is an ordinary call.
            //
            // A null set means the registry's pool is exhausted, which it has
            // already logged. Leaving the cutoff at zero degrades to the solid
            // rectangle this commit replaces, which beats a draw with nothing
            // bound at set 1 - and it makes "a positive cutoff has a set" true
            // by construction, so the depth pass needs no second check.
            if (const vk::DescriptorSet set =
                    textures.AcquireMaterialSet(renderable.albedoTextureID,
                                                renderable.normalTextureID)) {
                caster.alphaCutoff = alpha.cutoff;
                caster.baseAlpha = alpha.baseAlpha;
                caster.materialSet = set;
            }
        }

        out.push_back(caster);
    }

    // Solid casters first. The depth pass then changes pipeline once instead of
    // once per run of casters, and never at all in a scene with no cut-out
    // surface in it. Not stable: the order within each half reaches nothing but
    // the mesh-rebind batching and the pass signature, and both only need it to
    // be the SAME order every frame, which it is for a given input.
    std::partition(out.begin(), out.end(), [](const ShadowCaster& caster) {
        return caster.alphaCutoff <= 0.0f;
    });
}

uint64_t RenderSystem::MixSignature(uint64_t signature, const void* data, size_t bytes) {
    const auto* bytesIn = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < bytes; ++i) {
        signature ^= bytesIn[i];
        signature *= 1099511628211ull;
    }
    return signature;
}

uint64_t RenderSystem::ShadowPassSignature(const std::vector<ShadowCaster>& casters,
                                           const glm::mat4& lightViewProj,
                                           const Frustum& lightFrustum,
                                           uint64_t seed) {
    uint64_t signature = MixSignature(seed, &lightViewProj, sizeof(lightViewProj));

    // Mixed in even when nothing is visible, so an empty pass and a pass whose
    // casters all left are not confused with a pass that has not run.
    uint32_t visible = 0;

    for (const ShadowCaster& caster : casters) {
        if (!lightFrustum.IntersectsAABB(caster.worldMin, caster.worldMax)) continue;
        ++visible;

        signature = MixSignature(signature, &caster.model, sizeof(caster.model));
        signature = MixSignature(signature, &caster.worldMin, sizeof(caster.worldMin));
        signature = MixSignature(signature, &caster.worldMax, sizeof(caster.worldMax));
        signature = MixSignature(signature, &caster.meshID, sizeof(caster.meshID));
        signature = MixSignature(signature, &caster.skinPaletteBase, sizeof(caster.skinPaletteBase));
        signature = MixSignature(signature, &caster.skinJointCount, sizeof(caster.skinJointCount));

        // The buffer HANDLE, not only the id. MeshRegistry::Replace moves new
        // buffers over an existing id, so a reloaded asset keeps its id and
        // would otherwise leave a shadow of the geometry it replaced.
        const VkBuffer buffer = static_cast<VkBuffer>(caster.vertexBuffer);
        signature = MixSignature(signature, &buffer, sizeof(buffer));
        signature = MixSignature(signature, &caster.indexCount, sizeof(caster.indexCount));

        // What the cut is made against. Miss any of these three and the cache
        // serves a shadow map recorded before the material changed: edit a
        // leaf's cutoff and up to eighteen passes keep the silhouette it used
        // to have, looking entirely plausible. Nothing diagnoses that.
        //
        // The descriptor SET, for the same reason the vertex buffer handle is
        // here rather than the mesh id: ReplaceRGBA swaps an image under a
        // stable texture id and rebuilds the set, so the id alone is not a
        // statement about the pixels.
        signature = MixSignature(signature, &caster.alphaCutoff, sizeof(caster.alphaCutoff));
        signature = MixSignature(signature, &caster.baseAlpha, sizeof(caster.baseAlpha));
        const VkDescriptorSet materialSet = static_cast<VkDescriptorSet>(caster.materialSet);
        signature = MixSignature(signature, &materialSet, sizeof(materialSet));
    }

    return MixSignature(signature, &visible, sizeof(visible));
}

void RenderSystem::RenderDepthOnly(
    const std::vector<ShadowCaster>& casters,
    VulkanPipeline& pipeline,
    VulkanPipeline& cutoutPipeline,
    vk::CommandBuffer commandBuffer,
    vk::DescriptorSet sceneSet,
    const glm::mat4& lightViewProj,
    const Frustum& lightFrustum,
    Stats& stats) {

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.GetPipeline());
    // Bound for the joint palette at binding 2. The depth pass reads nothing
    // else from set 0 - the light's transform arrives premultiplied in the push
    // constant - but a skinned draw cannot skin without it.
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.GetLayout(),
                                     VulkanPipeline::kSceneSet, 1, &sceneSet, 0, nullptr);

    uint32_t boundMesh = MeshRegistry::kInvalidMesh;

    // The casters arrive partitioned - solid first - so this flips at most once
    // per pass, and stays false entirely in a scene with no cut-out surfaces.
    bool onCutoutPipeline = false;
    vk::DescriptorSet boundMaterialSet{};

    for (const ShadowCaster& caster : casters) {
        // Cull against the LIGHT's frustum here, not the camera's: an object
        // behind the viewer can still cast a shadow into view. This is the only
        // work in the loop that depends on which pass is running, which is why
        // everything else was worth hoisting out of it.
        if (!lightFrustum.IntersectsAABB(caster.worldMin, caster.worldMax)) {
            ++stats.shadowCulled;
            continue;
        }
        ++stats.shadowDrawn;

        // Crossing from the solid run into the cut-out one. The two pipeline
        // layouts are identically defined - same set layouts, same push range -
        // so set 0 survives the switch and is not rebound.
        VulkanPipeline& active = caster.alphaCutoff > 0.0f ? cutoutPipeline : pipeline;
        if ((caster.alphaCutoff > 0.0f) != onCutoutPipeline) {
            onCutoutPipeline = caster.alphaCutoff > 0.0f;
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, active.GetPipeline());
        }

        if (caster.meshID != boundMesh) {
            const vk::Buffer buffers[] = { caster.vertexBuffer };
            const vk::DeviceSize offsets[] = { 0 };
            commandBuffer.bindVertexBuffers(0, 1, buffers, offsets);
            commandBuffer.bindIndexBuffer(caster.indexBuffer, 0, vk::IndexType::eUint32);
            boundMesh = caster.meshID;
        }

        // Tracked separately from the mesh: two entities can share one mesh and
        // cut against different textures, and keying this off meshID would give
        // the second one the first one's holes.
        if (caster.materialSet && caster.materialSet != boundMaterialSet) {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, active.GetLayout(),
                                             VulkanPipeline::kMaterialSet, 1,
                                             &caster.materialSet, 0, nullptr);
            boundMaterialSet = caster.materialSet;
        }

        // World matrix so a child follows its parent, plus this light's
        // transform - the depth pass has no other use for the scene UBO.
        // Premultiplied on the CPU: two matrices would be the whole 128-byte
        // push constant budget, leaving nothing for the skinning indices. The
        // per-vertex skin matrix still composes correctly on the right.
        ShadowPushConstantData push{};
        push.viewProjModel = lightViewProj * caster.model;
        push.skinPaletteBase = caster.skinPaletteBase;
        push.skinJointCount = caster.skinJointCount;
        push.alphaCutoff = caster.alphaCutoff;
        push.baseAlpha = caster.baseAlpha;
        // Both stages, because the range declares both, and vkCmdPushConstants
        // requires the mask given here to name every stage the range does.
        // Pushing a vertex-only range against a layout claiming two is a
        // validation error on every shadow draw - the comment beside
        // pushConstantStages in the renderer records that being learned once.
        commandBuffer.pushConstants(
            active.GetLayout(),
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, sizeof(ShadowPushConstantData), &push);

        commandBuffer.drawIndexed(caster.indexCount, 1, 0, 0, 0);
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
    VulkanPipeline* skyPipeline,
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

    // ---- Sky -------------------------------------------------------------
    //
    // After the opaque pass so it only shades pixels nothing claimed, and
    // before the transparent one so it cannot paint over a blended surface
    // that deliberately left the depth buffer alone. A fullscreen triangle,
    // needing no vertex input and no material set.
    if (skyPipeline) {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, skyPipeline->GetPipeline());
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, skyPipeline->GetLayout(),
                                         VulkanPipeline::kSceneSet, 1, &sceneSet, 0, nullptr);
        commandBuffer.draw(3, 1, 0, 0);

        // Nothing is rebound here on purpose: both blocks below bind their own
        // pipeline and reset the rebind-avoidance state, and the opaque pass
        // above has already finished with it.
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

    }

    // ---- Particles -------------------------------------------------------
    //
    // Small cubes, shrinking as they age, on the TRANSPARENT pipeline - which
    // is what they always wanted. `ParticleEmitterComponent::endColor` defaults
    // to an alpha of zero, so every emitter in the engine is authored to fade
    // out; that alpha was computed, pushed, and then thrown away by an opaque
    // pipeline with blending disabled. Particles vanished at full brightness
    // instead of fading. The comment on MaterialComponent::transparent names
    // particle alpha as a motivation for the blended pass - the pass landed and
    // this caller was left behind on the opaque one.
    //
    // Sorted back to front among themselves, for the reason the meshes above
    // are. They are deliberately NOT merged into that list: a particle is a
    // different mesh with a different material set, so interleaving would cost
    // a mesh-and-material rebind per draw at the point in the frame with the
    // most draws in it. A particle behind a transparent pane therefore
    // composites in the wrong order. That is the trade, written down rather
    // than left to be discovered.
    const GpuMesh* particleMesh = meshes.Get(meshes.GetCubeMesh());
    if (!particleMesh || particleMesh->indexCount == 0) return;

    struct ParticleDraw {
        glm::vec3 position{0.0f};
        glm::vec4 color{1.0f};
        float size{1.0f};
        float distanceSquared{0.0f};
    };
    std::vector<ParticleDraw> particles;

    for (auto entity : registry.view<ParticleEmitterComponent>()) {
        const auto& emitter = registry.get<ParticleEmitterComponent>(entity);

        for (const auto& particle : emitter.particles) {
            if (!particle.active) continue;

            const float age = particle.maxLifetime > 0.0f ? particle.lifetime / particle.maxLifetime : 0.0f;
            const glm::vec3 toView = particle.position - viewPosition;

            particles.push_back(ParticleDraw{
                particle.position,
                particle.color,
                emitter.particleSize * glm::clamp(age, 0.15f, 1.0f),
                glm::dot(toView, toView)});
        }
    }

    if (particles.empty()) return;

    std::sort(particles.begin(), particles.end(),
              [](const ParticleDraw& lhs, const ParticleDraw& rhs) {
                  return lhs.distanceSquared > rhs.distanceSquared;
              });

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                               transparentPipeline.GetPipeline());
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     transparentPipeline.GetLayout(),
                                     VulkanPipeline::kSceneSet, 1, &sceneSet, 0, nullptr);

    const vk::Buffer buffers[] = { particleMesh->vertexBuffer->GetBuffer() };
    const vk::DeviceSize offsets[] = { 0 };
    commandBuffer.bindVertexBuffers(0, 1, buffers, offsets);
    commandBuffer.bindIndexBuffer(particleMesh->indexBuffer->GetBuffer(), 0, vk::IndexType::eUint32);

    if (vk::DescriptorSet whiteSet = textures.AcquireMaterialSet(
            textures.GetWhiteTexture(), textures.GetFlatNormalTexture())) {
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         transparentPipeline.GetLayout(),
                                         VulkanPipeline::kMaterialSet, 1, &whiteSet, 0, nullptr);
    }

    for (const auto& draw : particles) {
        PushConstantData push{};
        push.model = glm::scale(glm::translate(glm::mat4(1.0f), draw.position), glm::vec3(draw.size));
        push.albedoColor = draw.color;
        push.material = glm::vec4(1.0f, 0.0f, 1.0f, 0.0f);

        commandBuffer.pushConstants(
            transparentPipeline.GetLayout(),
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, sizeof(PushConstantData), &push);

        commandBuffer.drawIndexed(particleMesh->indexCount, 1, 0, 0, 0);
    }
}

} // namespace Supersonic
