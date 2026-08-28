#include "core/RenderSystem.hpp"

#include <algorithm>

#include <limits>
#include "core/TransformSystem.hpp"
#include "core/MaterialSystem.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Supersonic {

namespace {

// `surface` is the mesh section being drawn, or nullptr for a mesh that has
// only one - in which case the entity's own material describes it, exactly as
// it did before sections existed.
//
// WHAT A SURFACE OVERRIDES AND WHAT IT DOES NOT is the whole question here, and
// it is decided per field rather than wholesale:
//
//   albedo    MULTIPLIED by the entity's, because the entity's default is white
//             and white is the identity. That is what keeps a script tinting a
//             unit red, or flashing it past white on a hit, working across a
//             model with eight surfaces - it dims or brightens all of them
//             instead of replacing one.
//   roughness,
//   metallic  REPLACED, because the entity's defaults are 0.4 and 0.1 and
//             neither is an identity for a multiply. Multiplying would make
//             every surface of every imported model rougher and less metallic
//             than the file said, which reads as the lighting being wrong.
//   emissive  REPLACED, so a surface a file marked as glowing glows and its
//             neighbours do not.
//   cutoff    the SURFACE's when it names one, the entity's otherwise. It was
//             the entity's always, and the entity's is copied from the first
//             surface on import - so a MASK leaf packed behind an opaque
//             surface was cut at zero and drew as a solid rectangle. A widening
//             rather than a replacement, so the inspector's Alpha Cutoff slider
//             still reaches a surface that says nothing about alpha.
PushConstantData buildPushConstants(const entt::registry& registry, entt::entity entity,
                                    const glm::mat4& model,
                                    const MeshMaterial* surface = nullptr) {
    PushConstantData push{};
    push.model = model;

    // Which environment lights this draw. Resolved once per frame by the
    // renderer; zero - the scene-wide environment - when nothing resolved it,
    // which is also what PushConstantData defaults to.
    if (const auto* renderable = registry.try_get<RenderableComponent>(entity)) {
        push.probeIndex = renderable->probeSlot;
    }

    if (const auto* material = registry.try_get<MaterialComponent>(entity)) {
        if (material->unlit) push.flags |= PushConstantData::kUnlit;
        push.albedoColor = material->albedoColor;
        // w is the alpha cutoff. Zero is no cutout, which is also what every
        // caller that builds a push constant by hand leaves it at.
        push.material = glm::vec4(material->roughness, material->metallic, material->ao,
                                  material->alphaCutoff);
        push.emissive = glm::vec4(material->emissiveColor * material->emissiveStrength,
                                  material->occlusionStrength);

        // Which texture coordinate transform this draw samples through.
        // Resolved once per frame by MaterialSystem::GatherUvTransforms, and
        // zero - the identity slot - when nothing resolved it, which is also
        // what an ungathered material and a hand-built push constant both
        // leave it at.
        push.SetUvSlot(material->uvSlot);
    } else {
        push.albedoColor = glm::vec4(1.0f);
        push.material = glm::vec4(0.4f, 0.1f, 1.0f, 0.0f);
        // No material means no map either, and the neutral map's red is 1 -
        // so a strength of 1 over it still resolves to no occlusion at all.
        push.emissive = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // AFTER both branches, deliberately. An entity that has never been given a
    // MaterialComponent is exactly the case a freshly imported multi-surface
    // model is in, and folding this into the branch above would leave those
    // drawing every surface in the same default grey - which is the bug this
    // whole change exists to fix, reintroduced one level down.
    if (surface != nullptr && surface->present) {
        // Re-materialised first, if the entity named this surface. HUSK tells
        // its teams apart exactly this way: one model, BODY and DARK swapped
        // for a teal set or a rust set depending on who owns the unit.
        const MeshMaterial resolved =
            RenderSystem::ResolveSurface(*surface,
                                         registry.try_get<SurfaceOverridesComponent>(entity));

        // MULTIPLIED, not replaced, and this is the line that keeps a hit flash
        // working: the entity's albedo defaults to white, so an un-tinted unit
        // shows its team colour exactly, and one flashed past white blooms in
        // its team colour rather than losing it.
        push.albedoColor *= resolved.baseColor;
        push.material.x = resolved.roughness;
        push.material.y = resolved.metallic;
        push.emissive = glm::vec4(resolved.emissiveColor * resolved.emissiveStrength,
                                  resolved.occlusionStrength);

        // The cutoff, WHEN THIS SURFACE NAMES ONE. It used to be the entity's
        // always, and the entity's is copied from the FIRST surface on import -
        // so a leaf card packed as the fourth surface of a model whose first is
        // opaque was cut at zero, which is not cut at all. A fence, a grate or a
        // leaf behind any opaque surface drew as a solid rectangle, and there
        // was no authoring mistake to find: the file said MASK and the engine
        // read it and dropped it one surface later.
        //
        // Only when it names one, so this is a widening rather than a
        // replacement. A surface that says nothing about alpha leaves the
        // entity's cutoff standing, which is what keeps the inspector's Alpha
        // Cutoff slider working on an imported model - a straight replacement
        // would make that control silently inert on everything multi-material.
        if (resolved.alphaCutoff > 0.0f) {
            push.material.w = resolved.alphaCutoff;
        }
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
        signature = MixSignature(signature, &present, 1);
        signature = MixSignature(signature, material->ormTexturePath.data(),
                                 material->ormTexturePath.size());
    } else {
        signature = MixSignature(signature, &absent, 1);
    }

    // Zero is what "never resolved" means on the component, so it must not be
    // an answer. Folding it onto one rather than reserving a bit: the collision
    // this introduces is between two inputs one of which hashes to zero, and
    // costs a redundant re-resolve rather than a stale one.
    return signature == 0 ? 1ull : signature;
}

MeshMaterial RenderSystem::ResolveSurface(const MeshMaterial& fromFile,
                                          const SurfaceOverridesComponent* overrides) {
    if (overrides == nullptr || !fromFile.present) return fromFile;

    const SurfaceOverride* entry = overrides->Find(fromFile.name);
    if (entry == nullptr) return fromFile;

    // The MAPS are kept, the numbers are replaced. An override says what colour
    // a surface is, not what shape it is - so a model whose BODY has a normal
    // map keeps that map when the team colour lands on it, and only the values
    // that were being replaced are replaced.
    MeshMaterial out = fromFile;
    out.baseColor = entry->albedoColor;
    out.roughness = entry->roughness;
    out.metallic = entry->metallic;
    out.emissiveColor = entry->emissiveColor;
    out.emissiveStrength = entry->emissiveStrength;
    return out;
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

        // Every SURFACE of the mesh, resolved once per mesh rather than once per
        // entity. A model is authored as several named surfaces and they used
        // to be merged down to one material, first wins - a monument of stone
        // and gold arrived entirely stone, and 48 of HUSK's 58 models arrived
        // in a single flat colour for the same reason.
        //
        // Guarded on the texture generation and not on `resolve`, because the
        // answer belongs to the MESH: eighty units sharing one model resolve it
        // once between them, and a reloaded texture bumps the generation so
        // nothing is left pointing at the image it replaced.
        if (GpuMesh* gpuMesh = meshes.GetMutable(renderable.meshID);
            gpuMesh && gpuMesh->sectionTextureGeneration != textureGeneration) {
            gpuMesh->sectionTextureGeneration = textureGeneration;

            for (MeshSection& section : gpuMesh->sections) {
                const MeshMaterial& surface = section.material;

                // The same three fallbacks the entity path uses, and the same
                // reasons: white multiplies to nothing, a flat normal points
                // straight out, and a neutral ORM is 1 in every channel.
                section.albedoTextureID = surface.albedoTexturePath.empty()
                                        ? textures.GetWhiteTexture()
                                        : textures.Acquire(surface.albedoTexturePath, true,
                                                           textures.GetCheckerTexture());
                section.normalTextureID = surface.normalTexturePath.empty()
                                        ? textures.GetFlatNormalTexture()
                                        : textures.Acquire(surface.normalTexturePath, false,
                                                           textures.GetFlatNormalTexture());
                section.ormTextureID = surface.ormTexturePath.empty()
                                     ? textures.GetNeutralOrmTexture()
                                     : textures.Acquire(surface.ormTexturePath, false,
                                                        textures.GetNeutralOrmTexture());
            }
        }

        if (!resolve) continue;

        // Both texture paths used to be fields nothing read.
        if (const auto* material = materialComponent) {
            renderable.albedoTextureID = material->albedoTexturePath.empty()
                                       ? textures.GetWhiteTexture()
                                       : textures.Acquire(material->albedoTexturePath, true,
                                                          textures.GetCheckerTexture());
            // srgb=false: a normal map holds directions, not colour, so it must
            // not be gamma-decoded on read.
            renderable.normalTextureID = material->normalTexturePath.empty()
                                       ? textures.GetFlatNormalTexture()
                                       : textures.Acquire(material->normalTexturePath, false,
                                                          textures.GetFlatNormalTexture());
            // srgb=false for the same reason as the normal map, and it matters
            // more here: these are three numbers per texel the shader
            // multiplies straight into roughness, metallic and occlusion, so a
            // transfer function applied on read bends all three at once.
            renderable.ormTextureID = material->ormTexturePath.empty()
                                    ? textures.GetNeutralOrmTexture()
                                    : textures.Acquire(material->ormTexturePath, false,
                                                       textures.GetNeutralOrmTexture());
        } else {
            renderable.albedoTextureID = textures.GetWhiteTexture();
            renderable.normalTextureID = textures.GetFlatNormalTexture();
            renderable.ormTextureID = textures.GetNeutralOrmTexture();
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
        const auto* material = registry.try_get<MaterialComponent>(entity);
        const ShadowAlpha alpha = ShadowAlphaFor(material);
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
                                                renderable.normalTextureID,
                                                renderable.ormTextureID)) {
                caster.alphaCutoff = alpha.cutoff;
                caster.baseAlpha = alpha.baseAlpha;
                caster.materialSet = set;

                // Only on this branch. An opaque caster never samples its
                // albedo, so gathering a transform for one would be work that
                // reaches nothing - and it would put a number into the pass
                // signature that cannot change what the pass draws.
                if (material) {
                    caster.uvTransform = MakeUvTransform(material->uvScale,
                                                         material->uvRotation,
                                                         material->uvOffset);
                }
            }
        }

        // ONE CASTER PER CUT-OUT SURFACE, and one for the whole mesh otherwise.
        //
        // Everything above resolved a single material set from the ENTITY's
        // texture ids, which are the first surface's. That is right for a
        // single-material model and wrong for every other kind: a model whose
        // holes are on its fourth surface cast the silhouette of its first, and
        // a model whose FIRST surface is the cut-out one cut every other
        // surface against that surface's albedo at its own UVs - a chassis with
        // leaf-shaped holes in it, in all eighteen depth passes, cached by the
        // shadow cache so it looked stable and deliberate.
        //
        // Split only where a surface actually names a cutoff. A mesh whose
        // surfaces all cut at zero keeps the single whole-mesh caster it always
        // had, so the common case - and every one of HUSK's 166 materials,
        // which are all OPAQUE - does exactly the work it did before.
        const bool anySurfaceCuts =
            std::any_of(mesh->sections.begin(), mesh->sections.end(),
                        [](const MeshSection& section) {
                            return section.material.present && section.material.alphaCutoff > 0.0f;
                        });

        if (!anySurfaceCuts || mesh->sections.size() < 2) {
            out.push_back(caster);
            continue;
        }

        for (const MeshSection& section : mesh->sections) {
            if (section.indexCount == 0) continue;

            ShadowCaster surfaceCaster = caster;
            surfaceCaster.firstIndex = section.firstIndex;
            surfaceCaster.indexCount = section.indexCount;

            const MeshMaterial resolved =
                ResolveSurface(section.material,
                               registry.try_get<SurfaceOverridesComponent>(entity));

            if (resolved.alphaCutoff > 0.0f) {
                // This surface's own texture, so the holes are the ones this
                // surface has. Resolved here for the same reason the entity's
                // was: the depth pass runs with a render pass open and cannot
                // afford a call that allocates.
                if (const vk::DescriptorSet set =
                        textures.AcquireMaterialSet(section.albedoTextureID,
                                                    section.normalTextureID,
                                                    section.ormTextureID)) {
                    surfaceCaster.alphaCutoff = resolved.alphaCutoff;
                    surfaceCaster.baseAlpha = alpha.baseAlpha * resolved.baseColor.a;
                    surfaceCaster.materialSet = set;
                }
            } else {
                // An opaque surface of a model that has a cut-out one somewhere
                // else. It occludes everywhere and must not inherit the
                // entity-level cut resolved above, or a solid chassis would
                // develop the holes of the leaf beside it.
                surfaceCaster.alphaCutoff = 0.0f;
                surfaceCaster.baseAlpha = 1.0f;
                surfaceCaster.materialSet = vk::DescriptorSet{};
                surfaceCaster.uvTransform = UvTransform{};
            }

            out.push_back(surfaceCaster);
        }
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

        // WHICH run of indices, not only how many. Two surfaces of one mesh can
        // have the same index count - a cube of six quads is the ordinary case -
        // so without this the cache cannot tell "the leaf casts" from "the panel
        // beside it casts", and a change that swapped which surface cuts would
        // serve the shadow map recorded before it.
        signature = MixSignature(signature, &caster.firstIndex, sizeof(caster.firstIndex));

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

        // A scrolling cut-out is the fourth way the silhouette changes while
        // the gather sees nothing move: same entity, same transform, same
        // mesh, same texture, different holes. Miss it and a flipbooked or
        // scrolled caster keeps the shadow of whichever frame was cached.
        signature = MixSignature(signature, &caster.uvTransform, sizeof(caster.uvTransform));
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
        push.uvTransform = caster.uvTransform;
        // Both stages, because the range declares both, and vkCmdPushConstants
        // requires the mask given here to name every stage the range does.
        // Pushing a vertex-only range against a layout claiming two is a
        // validation error on every shadow draw - the comment beside
        // pushConstantStages in the renderer records that being learned once.
        commandBuffer.pushConstants(
            active.GetLayout(),
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, sizeof(ShadowPushConstantData), &push);

        // firstIndex, not zero. A caster is one run of indices now rather than
        // always a whole mesh, which is what lets a cut-out surface be cut
        // against its OWN texture instead of the first surface's.
        commandBuffer.drawIndexed(caster.indexCount, 1, caster.firstIndex, 0, 0);
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

bool RenderSystem::SortOpaqueDraws(std::vector<OpaqueDraw>& draws) {
    // The early out is the feature, not an optimisation.
    //
    // Every scene on disk holds no opinion about draw order, and for those the
    // list must be recorded in exactly the sequence the registry produced it -
    // not in whatever sequence a stable sort of all-equal keys happens to
    // produce, which would be the same today and is not a promise the standard
    // makes about a list this code did not build.
    bool anyKey = false;
    for (const OpaqueDraw& draw : draws) {
        if (draw.sortKey != 0) {
            anyKey = true;
            break;
        }
    }
    if (!anyKey) return false;

    std::stable_sort(draws.begin(), draws.end(),
                     [](const OpaqueDraw& left, const OpaqueDraw& right) {
                         return left.sortKey < right.sortKey;
                     });
    return true;
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
        // Matches RenderableComponent's default, which is the neutral ORM and
        // not id 0 - that one is the sRGB white ALBEDO. Unreachable, because
        // the single construction site sets it, and wrong on the day it is not.
        uint32_t ormTextureID{2};
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

    std::vector<OpaqueDraw> opaque;

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
                renderable.ormTextureID,
                glm::dot(toView, toView)});
            continue;
        }

        // Gathered, not recorded. An order cannot be chosen for draws that
        // have already been submitted.
        opaque.push_back(OpaqueDraw{
            world.matrix,
            mesh->vertexBuffer->GetBuffer(),
            mesh->indexBuffer->GetBuffer(),
            entity,
            mesh->indexCount,
            renderable.meshID,
            renderable.albedoTextureID,
            renderable.normalTextureID,
            renderable.ormTextureID,
            renderable.sortKey});
    }

    // Only when somebody has an opinion. With every key equal this returns
    // false having touched nothing, and the loop below records exactly the
    // sequence the view produced - which is what it recorded before this
    // existed.
    SortOpaqueDraws(opaque);

    for (const OpaqueDraw& draw : opaque) {
        if (draw.meshID != boundMesh) {
            const vk::Buffer buffers[] = { draw.vertexBuffer };
            const vk::DeviceSize offsets[] = { 0 };
            commandBuffer.bindVertexBuffers(0, 1, buffers, offsets);
            commandBuffer.bindIndexBuffer(draw.indexBuffer, 0, vk::IndexType::eUint32);
            boundMesh = draw.meshID;
        }

        // ONE RANGE PER SURFACE. A model authored as several named materials -
        // BODY, DARK, GLASS, EMISSIVE is a real example - used to be merged
        // down to whichever came first, and arrived in a single flat colour.
        //
        // The buffers are bound once above and the push constant written once
        // here; only the material set changes between ranges, so a model with
        // eight surfaces costs eight draws and no extra bind of anything else.
        //
        // THE ENTITY STILL WINS where it has said anything. A MaterialComponent
        // is what a person edits and what a script tints, so a material
        // authored on the entity overrides every surface; the file's own
        // materials are what an unedited import draws with. Section 0's ids are
        // what the entity path already resolved for it, so the two agree on a
        // single-material model rather than fighting.
        // WHERE THE TEXTURES COME FROM, and the rule is decided by how many
        // surfaces the mesh actually has.
        //
        // One surface: the entity, exactly as before. Its MaterialComponent was
        // either authored or copied off the file at import, so nothing changes
        // for every model that already worked.
        //
        // More than one: the sections, because a MaterialComponent is ONE
        // material and cannot describe eight surfaces. Asking it to would mean
        // picking one of them to be the real one, which is the "first wins"
        // this replaces.
        //
        // Only the MAPS are decided this way. albedoColor, roughness, metallic,
        // emissive and unlit ride in the push constant and are written once for
        // the whole mesh, so tinting an entity red still turns the whole model
        // red across every surface - which is what a script flashing a unit on
        // hit is doing, and it goes on working here.
        const GpuMesh* gpuMesh = meshes.Get(draw.meshID);
        const bool multiSurface = gpuMesh && gpuMesh->sections.size() > 1;
        const size_t sectionCount = multiSurface ? gpuMesh->sections.size() : 1u;

        for (size_t s = 0; s < sectionCount; ++s) {
            uint32_t albedo = draw.albedoTextureID;
            uint32_t normal = draw.normalTextureID;
            uint32_t orm = draw.ormTextureID;
            uint32_t firstIndex = 0;
            uint32_t indexCount = draw.indexCount;

            if (multiSurface) {
                const MeshSection& section = gpuMesh->sections[s];
                firstIndex = section.firstIndex;
                indexCount = section.indexCount;
                albedo = section.albedoTextureID;
                normal = section.normalTextureID;
                orm = section.ormTextureID;
            }

            // One set per combination of maps, cached, so surfaces and entities
            // sharing a material do not rebind.
            //
            // Note the interaction with sorting: run-length grouping by mesh and
            // material survives only while equal keys keep their gather order,
            // which is why the sort is stable. A key per entity would destroy
            // it, exactly as distance ordering destroyed it for the transparent
            // pass.
            if (vk::DescriptorSet materialSet =
                    textures.AcquireMaterialSet(albedo, normal, orm);
                materialSet && materialSet != boundMaterialSet) {
                commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                 pipeline.GetLayout(),
                                                 VulkanPipeline::kMaterialSet, 1, &materialSet,
                                                 0, nullptr);
                boundMaterialSet = materialSet;
            }

            // Written per section, not once for the mesh. HUSK's models carry
            // NO TEXTURES AT ALL - eight surfaces distinguished purely by
            // base colour, metallic and roughness - so a version of this that
            // only varied the maps would have looked finished and changed
            // nothing anyone could see.
            const PushConstantData push =
                buildPushConstants(registry, draw.entity, draw.matrix,
                                   multiSurface ? &gpuMesh->sections[s].material : nullptr);
            commandBuffer.pushConstants(
                pipeline.GetLayout(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, sizeof(PushConstantData), &push);

            if (indexCount > 0) {
                commandBuffer.drawIndexed(indexCount, 1, firstIndex, 0, 0);
            }
        }
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
                    textures.AcquireMaterialSet(draw.albedoTextureID, draw.normalTextureID,
                                                draw.ormTextureID);
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
            textures.GetWhiteTexture(), textures.GetFlatNormalTexture(),
            textures.GetNeutralOrmTexture())) {
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
