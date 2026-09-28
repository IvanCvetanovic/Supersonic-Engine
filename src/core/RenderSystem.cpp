#include "core/Log.hpp"
#include "core/RenderSystem.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

#include <limits>
#include "core/TransformSystem.hpp"
#include "core/MaterialSystem.hpp"
#include "core/RenderSettings.hpp"
#include "core/TilemapSystem.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Supersonic {

namespace {

// A Vulkan handle as the plain number the batcher compares.
//
// PlanPass asks only equality of its keys, and it takes numbers rather than
// handles so a suite with no device can build one. Copied through memcpy
// rather than cast, because a non-dispatchable handle is a pointer on a
// 64-bit build and an integer on a 32-bit one, and a reinterpret_cast is
// ill-formed for one of those two.
template <class Handle>
uint64_t HandleKey(Handle handle) {
    static_assert(sizeof(Handle) == sizeof(uint64_t),
                  "a Vulkan handle is expected to be 64 bits wide here");
    uint64_t key = 0;
    std::memcpy(&key, &handle, sizeof(key));
    return key;
}

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

    // LAST of the material's say, after the surface branch too: a 2D sprite
    // repurposes albedoColor, material and emissive, and either branch above
    // writing one of them afterwards would undo its packing. A sprite is a
    // single-surface quad, so today the surface branch never runs for one; the
    // order is what keeps that from mattering.
    if (const auto* material = registry.try_get<MaterialComponent>(entity)) {
        RenderSystem::ApplySprite2D(*material, push);
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

void RenderSystem::SortTransparentDraws(std::vector<TransparentDraw>& draws) {
    std::sort(draws.begin(), draws.end(),
              [](const TransparentDraw& lhs, const TransparentDraw& rhs) {
                  // Back to front: the farthest along the view direction is
                  // drawn first, so nearer surfaces composite over it.
                  if (lhs.viewDepth != rhs.viewDepth) return lhs.viewDepth > rhs.viewDepth;

                  // Ascending: a HIGHER sort key is drawn later, which is to say
                  // on top - matching what the opaque pass means by it and what
                  // a person authoring a HUD expects.
                  if (lhs.sortKey != rhs.sortKey) return lhs.sortKey < rhs.sortKey;

                  // Gather order, which makes this a TOTAL order. Without it
                  // two draws agreeing on both keys are left to std::sort, and
                  // introsort is free to order equal elements however it
                  // pleases - a flicker whose cause is the standard library.
                  return lhs.gathered < rhs.gathered;
              });
}

std::vector<RenderSystem::BlendRun> RenderSystem::BlendRuns(const std::vector<TransparentDraw>& sorted) {
    std::vector<BlendRun> runs;
    for (uint32_t i = 0; i < static_cast<uint32_t>(sorted.size()); ++i) {
        if (runs.empty() || runs.back().blend != sorted[i].blend) {
            runs.push_back(BlendRun{i, 0, sorted[i].blend});
        }
        ++runs.back().count;
    }
    return runs;
}

BlendEquation RenderSystem::EquationFor(MaterialComponent::BlendMode blend) {
    switch (blend) {
    case MaterialComponent::BlendMode::Additive: return BlendEquation::Add;
    case MaterialComponent::BlendMode::Premultiplied: return BlendEquation::Premultiplied;
    case MaterialComponent::BlendMode::Alpha:
    default: return BlendEquation::Mix;
    }
}

void RenderSystem::ApplySprite2D(const MaterialComponent& material, PushConstantData& push) {
    if (material.unlit && material.sprite2D.enabled) {
        const MaterialComponent::Sprite2DLight& sprite = material.sprite2D;
        const glm::vec3 tint(material.albedoColor);

        push.flags |= PushConstantData::kSprite2D;
        if (sprite.normalYDown) push.flags |= PushConstantData::kNormalYDown;
        push.flags = PackLightMask(push.flags, sprite.lightMask);

        // The ambient multiplies here, on the CPU, so the shader's base is the
        // same one multiply the plain unlit path already does.
        push.albedoColor = glm::vec4(tint * sprite.ambient, material.albedoColor.a);

        // The tint again WITHOUT the ambient, for the light term: a lamp is
        // not dimmed by the room it shines in. And the lighting height, which
        // is not the transform's z - in a 2D scene that is draw order.
        push.emissive = glm::vec4(tint, sprite.height);

        // x is the overlay's strength; w stays the cutoff, which the discard
        // above every exit of shader.frag reads for every path alike.
        push.material = glm::vec4(sprite.overlayStrength, 0.0f, 0.0f, material.alphaCutoff);

        // Standing up: the switch, and the base line in y, which the flat
        // sprite left at zero. Without the switch y stays zero, so a sprite
        // that never asked writes the bytes it always wrote.
        if (sprite.vertical) {
            push.flags |= PushConstantData::kVertical2D;
            push.material.y = sprite.verticalBaseY;
        }

        // The highlight: its strength in z, where zero is the off switch the
        // shader tests, and its power in probeIndex - an int only the PBR exit
        // reads, which a sprite never reaches - as the float's own bits, so
        // nothing is rounded. Only when it is on, so probeIndex is otherwise
        // what the gather put there.
        if (sprite.specularStrength > 0.0f) {
            push.material.z = sprite.specularStrength;
            push.probeIndex = std::bit_cast<int32_t>(sprite.specularPower);

            // A baked light's eye: the switch, and the eye's y in
            // skinJointCount - which only the skinned vertex path reads, and
            // only while skinPaletteBase is not -1. A skinned draw has both
            // overwritten by the skin block after this, and the shader takes
            // the switch only from a draw whose skinPaletteBase is still -1, so
            // it keeps its joints and goes without. Only with a highlight, the
            // one thing the eye enters.
            if (sprite.bakedEye) {
                push.flags |= PushConstantData::kBakedEye2D;
                push.skinJointCount = std::bit_cast<int32_t>(sprite.bakedEyeY);
            }
        }

        // The light pass's alpha test: a switch alone; the intensity it needs
        // is the frame's (Light2DAlphaTest, the 2D light header).
        if (sprite.lightAlphaTest) push.flags |= PushConstantData::kLightAlphaTest2D;

        // A light's own shadows: a switch alone; the strips are the frame's
        // (scene binding 13).
        if (sprite.lightShadows) push.flags |= PushConstantData::kLightShadows2D;
    }

    // Any path, not only a sprite's: every exit of shader.frag honours it. And
    // only on a blended material, because a premultiplied colour composited by
    // an opaque pipeline would simply be a darker colour.
    if (material.transparent && material.blend == MaterialComponent::BlendMode::Premultiplied) {
        push.flags |= PushConstantData::kPremultiplied;
    }
}

void RenderSystem::SortParticleDraws(std::vector<ParticleDraw>& draws) {
    std::sort(draws.begin(), draws.end(),
              [](const ParticleDraw& lhs, const ParticleDraw& rhs) {
                  // Back to front, exactly as the blended pass above.
                  if (lhs.viewDepth != rhs.viewDepth) return lhs.viewDepth > rhs.viewDepth;

                  // And gather order, which is what makes it total. See
                  // ParticleDraw::gathered: a burst of particles shares one
                  // spawn position, so equal depths here are the rule.
                  return lhs.gathered < rhs.gathered;
              });
}

uint64_t RenderSystem::ResourceSignature(const MeshComponent* mesh,
                                         const MaterialComponent* material,
                                         uint64_t meshGeneration,
                                         uint64_t textureGeneration,
                                         bool decodesColourTextures) {
    uint64_t signature = MixSignature(1469598103934665603ull, &meshGeneration,
                                      sizeof(meshGeneration));
    signature = MixSignature(signature, &textureGeneration, sizeof(textureGeneration));

    // The colour space the albedo is uploaded in. As a byte of its own, not
    // folded into a generation, so a mode switch and a reload cannot cancel.
    const unsigned char colourSpace = decodesColourTextures ? 1 : 0;
    signature = MixSignature(signature, &colourSpace, 1);

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
        // The game's own geometry selects a mesh as surely as a path does, so
        // it belongs here: without it, a component switched from one uploaded
        // key to another would keep drawing the first one for ever, because
        // nothing else about the entity changed.
        signature = MixSignature(signature, &present, 1);
        signature = MixSignature(signature, mesh->meshKey.data(), mesh->meshKey.size());
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
        // The overlay selects a texture as surely as the other three. Its colour
        // space is the albedo's, already mixed above.
        signature = MixSignature(signature, &present, 1);
        signature = MixSignature(signature, material->overlayTexturePath.data(),
                                 material->overlayTexturePath.size());
        // And the gloss map, for the same reason: it is a fifth binding.
        signature = MixSignature(signature, &present, 1);
        signature = MixSignature(signature, material->glossTexturePath.data(),
                                 material->glossTexturePath.size());
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
    // Tilemaps first, because a map's mesh is written onto its renderable here
    // and the loop below reads it. Before the generation is sampled, too: a
    // map that was replaced this frame has bumped it, and sampling afterwards
    // is what makes every other entity re-resolve against the new value now
    // rather than one frame late.
    TilemapSystem::Sync(registry, meshes);

    const uint64_t meshGeneration = meshes.Generation();
    const uint64_t textureGeneration = textures.Generation();

    // Whether a colour texture is decoded from sRGB on read, for the whole
    // scene (RenderSettings::SceneEncoding). A scene that never said is linear,
    // which decodes, as every scene always has. Normal and ORM maps are data
    // in every mode and never ask.
    const RenderSettings* renderSettings = registry.ctx().find<RenderSettings>();
    const bool decodeColour = renderSettings == nullptr || renderSettings->decodesColourTextures();

    auto view = registry.view<RenderableComponent>();
    for (auto entity : view) {
        auto& renderable = view.get<RenderableComponent>(entity);

        auto* meshComponent = registry.try_get<MeshComponent>(entity);
        const auto* materialComponent = registry.try_get<MaterialComponent>(entity);

        // Nothing that decides these ids has changed, so neither have they.
        // Only the RESOLUTION is skipped - three hash-map lookups, each building
        // its key by concatenating strings.
        const uint64_t signature = ResourceSignature(meshComponent, materialComponent,
                                                     meshGeneration, textureGeneration, decodeColour);
        const bool resolve = signature != renderable.resourceSignature;

        if (resolve) {
            renderable.resourceSignature = signature;

            // A tilemap's mesh is the sync above's to decide, and BOTH halves
            // of this are skipped for one. The second half is the one that
            // would bite: an empty map is handed kInvalidMesh so that it draws
            // nothing, and the fallback below would hand it the cube instead.
            if (registry.all_of<TilemapComponent>(entity)) {
                // Owned above.
            } else if (meshComponent != nullptr && !meshComponent->meshKey.empty()) {
                // GEOMETRY THE GAME BUILT. Found, never acquired: it exists
                // because something uploaded it, and there is no file or
                // primitive to build it from if it does not.
                //
                // A key that names nothing yet leaves the id ALONE. Falling
                // back to the cube here would put a cube on screen for every
                // frame between an entity being created and its geometry being
                // uploaded - which for anything built during the frame is at
                // least one, and is a cube nobody asked for in a game that may
                // not own a single cube.
                //
                // NOT COVERED BY A TEST, and said here rather than assumed:
                // MeshRegistry needs a device and a command pool, so no suite
                // can construct one - test_tilemap and test_shadowcache both
                // record the same limitation for their own walks. What IS
                // tested is that the key reaches the signature
                // (test_resourcesync), which is the half that decides whether
                // this branch runs at all. The branch itself is proved by the
                // game drawing generated geometry, which is a weaker guarantee
                // and worth knowing about before trusting it.
                const uint32_t found = meshes.Find(meshComponent->meshKey);
                if (found != MeshRegistry::kInvalidMesh) renderable.meshID = found;
            } else if (const auto* mesh = meshComponent) {
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
        //
        // And on the colour space, for the reason the entity signature mixes
        // it: a scene switching its encoding moves no generation, and without
        // this every model would keep the albedo upload of the mode before.
        if (GpuMesh* gpuMesh = meshes.GetMutable(renderable.meshID);
            gpuMesh && (gpuMesh->sectionTextureGeneration != textureGeneration ||
                        gpuMesh->sectionDecodesColour != decodeColour)) {
            gpuMesh->sectionTextureGeneration = textureGeneration;
            gpuMesh->sectionDecodesColour = decodeColour;

            for (MeshSection& section : gpuMesh->sections) {
                const MeshMaterial& surface = section.material;

                // The same three fallbacks the entity path uses, and the same
                // reasons: white multiplies to nothing, a flat normal points
                // straight out, and a neutral ORM is 1 in every channel.
                section.albedoTextureID = surface.albedoTexturePath.empty()
                                        ? textures.GetWhiteTexture()
                                        : textures.Acquire(surface.albedoTexturePath, decodeColour,
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
                                       : textures.Acquire(material->albedoTexturePath, decodeColour,
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
            // COLOUR, like the albedo, and decoded exactly when the albedo is:
            // a baked light term is authored in the same space as the picture
            // it lights. Black when unnamed or unreadable, which adds nothing -
            // a checkerboard added over a sprite would be a worse way to say a
            // file is missing than the sprite simply unlit.
            renderable.overlayTextureID = material->overlayTexturePath.empty()
                                        ? textures.GetBlackTexture()
                                        : textures.Acquire(material->overlayTexturePath, decodeColour,
                                                           textures.GetBlackTexture());
            // DATA, like the ORM map: a gloss is a multiplier, and a transfer
            // function applied on read would bend it. White when unnamed, so a
            // strength alone is a uniform gloss; black when named and
            // unreadable, so a missing file adds no highlight it never had,
            // rather than a full one.
            renderable.glossTextureID = material->glossTexturePath.empty()
                                      ? textures.GetWhiteTexture()
                                      : textures.Acquire(material->glossTexturePath, false,
                                                         textures.GetBlackTexture());
        } else {
            renderable.albedoTextureID = textures.GetWhiteTexture();
            renderable.normalTextureID = textures.GetFlatNormalTexture();
            renderable.ormTextureID = textures.GetNeutralOrmTexture();
            renderable.overlayTextureID = textures.GetBlackTexture();
            renderable.glossTextureID = textures.GetWhiteTexture();
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
                                                renderable.ormTextureID,
                                                renderable.overlayTextureID,
                                                renderable.glossTextureID)) {
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
                                                    section.ormTextureID,
                                                    textures.GetBlackTexture(),
                                                    textures.GetWhiteTexture())) {
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

uint32_t RenderSystem::DrawCallsForMesh(const GpuMesh* mesh) {
    return (mesh && mesh->sections.size() > 1)
               ? static_cast<uint32_t>(mesh->sections.size())
               : 1u;
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

RenderSystem::PassPlan RenderSystem::PlanPass(const std::vector<PassDraw>& draws,
                                              uint32_t firstInstance,
                                              uint32_t capacity,
                                              uint64_t boundMesh,
                                              uint64_t boundMaterial) {
    PassPlan plan;
    plan.batches.reserve(draws.size());

    // What the NEXT batch to open must bind. Set when a bind is decided and
    // cleared onto the batch that opens after it, because a bind and the
    // batch it serves are decided one draw apart: the bind closes whatever
    // was open, and the thing it was bound for goes in what opens next.
    bool pendingMeshBind = false;
    bool pendingMaterialBind = false;

    for (const PassDraw& draw : draws) {
        // A range with nothing in it draws nothing, and nothing is bound for
        // it. Unreachable today - BuildSections keeps only non-empty runs and
        // the gather skips a mesh with no indices - and kept because the
        // alternative is a drawIndexed of zero indices inside a batch whose
        // other members are real.
        if (draw.indexCount == 0) continue;

        // FULL, and checked BEFORE the binds rather than after. The opaque
        // loop this replaces bound first and recorded second, so once the
        // frame's instance buffer filled it went on binding meshes and
        // descriptors for draws it had already decided not to draw. That is
        // waste in the one frame that can least afford it, and it is also the
        // one thing a plan cannot honestly describe - a batch names the binds
        // it needs, and a bind belonging to no batch belongs nowhere.
        if (firstInstance + plan.instances >= capacity) {
            ++plan.dropped;
            continue;
        }

        // A REBIND CLOSES THE BATCH, because the next draw would read
        // different buffers or a different descriptor than the call already
        // named.
        if (draw.meshKey != boundMesh) {
            boundMesh = draw.meshKey;
            ++plan.meshBinds;
            pendingMeshBind = true;
        }

        // Zero is "this pass has no material set", which is what a null
        // descriptor set meant: no bind, and no batch closed for one.
        if (draw.materialKey != 0 && draw.materialKey != boundMaterial) {
            boundMaterial = draw.materialKey;
            ++plan.materialBinds;
            pendingMaterialBind = true;
        }

        const uint32_t slot = firstInstance + plan.instances;
        ++plan.instances;

        // JOIN THE OPEN BATCH, OR OPEN ANOTHER. A record is only ever
        // appended, so a batch's instances are contiguous by construction,
        // which is what lets one call name them with a first and a count.
        PassBatch* open = plan.batches.empty() ? nullptr : &plan.batches.back();
        // Contiguity is not among the conditions, because it cannot fail. A
        // slot is handed out only here and only in ascending order, and the
        // two ways a draw leaves without one - an empty index range and a full
        // buffer - both take no slot at all, so the run a batch names has no
        // gap in it to check for. A version of this asked anyway; a mutation
        // that deleted the question changed no answer, which is how a
        // condition that cannot be false announces itself.
        const bool joins = open != nullptr && !pendingMeshBind && !pendingMaterialBind &&
                           open->meshKey == draw.meshKey &&
                           open->firstIndex == draw.firstIndex &&
                           open->indexCount == draw.indexCount &&
                           open->materialKey == draw.materialKey;

        if (joins) {
            ++open->instanceCount;
            continue;
        }

        PassBatch batch;
        batch.meshKey = draw.meshKey;
        batch.firstIndex = draw.firstIndex;
        batch.indexCount = draw.indexCount;
        batch.materialKey = draw.materialKey;
        batch.firstInstance = slot;
        batch.instanceCount = 1;
        batch.bindsMesh = pendingMeshBind;
        batch.bindsMaterial = pendingMaterialBind;
        pendingMeshBind = false;
        pendingMaterialBind = false;
        plan.batches.push_back(batch);
    }

    return plan;
}

void RenderSystem::Render(
    entt::registry& registry,
    VulkanPipeline& pipeline,
    VulkanPipeline& transparentPipeline,
    VulkanPipeline& additivePipeline,
    VulkanPipeline& premultipliedPipeline,
    VulkanPipeline* skyPipeline,
    MeshRegistry& meshes,
    TextureRegistry& textures,
    vk::CommandBuffer commandBuffer,
    vk::DescriptorSet sceneSet,
    const Frustum& frustum,
                          const glm::vec3& viewPosition,
                          Stats& stats,
                          std::vector<PushConstantData>& instances,
                          uint32_t maxInstances) {
    instances.clear();

    // Appends one per-draw record and returns where it landed, or -1 when the
    // frame has produced more drawables than the buffer can hold.
    //
    // A refusal DROPS THE DRAW rather than writing past the end, and says so
    // once. Sixty-five thousand records is three times the count at which this
    // engine already misses 60 Hz for other reasons, so this is a backstop
    // rather than a limit anybody should meet.
    static bool warnedInstanceOverflow = false;
    auto record = [&instances, maxInstances](const PushConstantData& data) -> int32_t {
        if (instances.size() >= maxInstances) {
            if (!warnedInstanceOverflow) {
                warnedInstanceOverflow = true;
                SUPERSONIC_LOG_ERROR("RenderSystem")
                    << "more than " << maxInstances
                    << " drawables in one frame; the rest are not drawn." << std::endl;
            }
            return -1;
        }
        instances.push_back(data);
        return static_cast<int32_t>(instances.size()) - 1;
    };

    // Transparent entities are collected during the opaque walk and drawn after
    // it, sorted back to front. Blending is order-dependent: two overlapping
    // panes drawn in the wrong order composite in the wrong order, and the
    // result is wrong rather than merely differently wrong.
    // One derivation for both blended sorts below, from the same matrix the
    // culling used - see Frustum::ViewDirection for why it is taken from the
    // frustum rather than passed in beside it.
    const glm::vec3 viewDirection = frustum.ViewDirection();

    std::vector<TransparentDraw> transparent;

    // ONE ITEM PER DRAW A PASS WILL SUBMIT, gathered before any of it is
    // recorded.
    //
    // The key is what the planner batches on; the rest is what the recorder
    // needs once the planner has answered - the handles those keys stand for,
    // and what a push constant is built from. Splitting them is what lets the
    // decision be a pure function over numbers while the recording keeps its
    // Vulkan handles.
    struct PassItem {
        PassDraw key{};

        // BY VALUE, for the reason OpaqueDraw gives: Get returns a pointer
        // INTO a vector that Upload push_backs onto, so one upload mid-frame
        // would dangle every handle gathered before it.
        vk::Buffer vertexBuffer{};
        vk::Buffer indexBuffer{};
        vk::DescriptorSet materialSet{};

        entt::entity entity{entt::null};

        // THE TWO POINTERS, AND WHAT WOULD INVALIDATE THEM. `matrix` points
        // into the gathered draw list and `surface` two vectors deep into the
        // mesh registry, and both are now dereferenced in the recording below
        // rather than at the moment they are taken - which is a window the
        // code they were copied from did not have.
        //
        // Safe because nothing between the gather and the record touches
        // either: Render only ever calls MeshRegistry::Get, which is a bounds
        // check and an index, and both draw lists are complete and sorted
        // before the first pointer into them is taken. An Upload, a Replace,
        // or a push onto `opaque` or `transparent` in between would break
        // that, silently, as a wrong material rather than an error - and a
        // tilemap rebake is exactly such a call, which is why it runs in
        // SyncResources, before any of this.
        //
        // Copied rather than pointed to would cost four std::string copies
        // per draw per frame, which is worse than the hazard it removes.
        const glm::mat4* matrix{nullptr};

        // The mesh section this draw is, or null for a mesh with one surface -
        // in which case the entity's own material describes it.
        const MeshMaterial* surface{nullptr};
    };

    // Issues a planned pass: binds what each batch says has to be bound,
    // writes one instance record per draw, and submits one call per batch.
    //
    // The plan decides and this records; there is no second opinion here
    // about what a batch is. That is the whole point of the split - a counter
    // and a loop that each decide separately can come to disagree, and this
    // engine has already shipped that once, with a draw-call count that was
    // never the number of draw calls.
    const auto recordPass = [&](const std::vector<PassItem>& items, const PassPlan& plan,
                                VulkanPipeline& passPipeline) {
        size_t batchIndex = 0;
        uint32_t inBatch = 0;

        for (const PassItem& item : items) {
            if (item.key.indexCount == 0) continue;

            // Every batch issued means everything left was refused by a full
            // instance buffer - the plan drops a suffix, never a hole.
            if (batchIndex >= plan.batches.size()) break;

            const PassBatch& batch = plan.batches[batchIndex];

            if (inBatch == 0) {
                if (batch.bindsMesh) {
                    const vk::Buffer buffers[] = { item.vertexBuffer };
                    const vk::DeviceSize offsets[] = { 0 };
                    commandBuffer.bindVertexBuffers(0, 1, buffers, offsets);
                    commandBuffer.bindIndexBuffer(item.indexBuffer, 0, vk::IndexType::eUint32);
                }
                if (batch.bindsMaterial) {
                    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                     passPipeline.GetLayout(),
                                                     VulkanPipeline::kMaterialSet, 1,
                                                     &item.materialSet, 0, nullptr);
                }
            }

            const int32_t slot =
                record(buildPushConstants(registry, item.entity, *item.matrix, item.surface));

            // The plan stopped at the same capacity `record` enforces, so a
            // refusal here means the two disagree about how full the buffer
            // is - which is a defect rather than a full frame, and stopping
            // is what keeps a batch from naming records that were not written.
            if (slot < 0) break;

            ++inBatch;
            if (inBatch < batch.instanceCount) continue;

            commandBuffer.drawIndexed(batch.indexCount, batch.instanceCount, batch.firstIndex, 0,
                                      batch.firstInstance);
            ++stats.drawCalls;
            ++batchIndex;
            inBatch = 0;
        }

        stats.meshBinds += plan.meshBinds;
        stats.materialBinds += plan.materialBinds;
    };

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.GetPipeline());
    ++stats.pipelineBinds;

    // Set 0 is per-frame and bound once.
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.GetLayout(),
                                     VulkanPipeline::kSceneSet, 1, &sceneSet, 0, nullptr);

    // What used to be tracked here - the last mesh and material set bound, so
    // consecutive draws sharing either did not rebind - is PlanPass's now, and
    // the batches it returns say which binds a pass actually needs. There were
    // three copies of that bookkeeping and each carried its own chance of
    // getting the reset wrong: the blended pass had to remember to clear both
    // after binding its own pipeline, which is the sort of thing that is
    // invisible when it is missing until two passes disagree about what is
    // bound.
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
            ++stats.transparentDrawn;
            transparent.push_back(TransparentDraw{
                entity, mesh, world.matrix,
                renderable.albedoTextureID, renderable.normalTextureID,
                renderable.ormTextureID, renderable.overlayTextureID,
                glm::dot(centre - viewPosition, frustum.ViewDirection()),
                renderable.sortKey,
                static_cast<uint32_t>(transparent.size()),
                EquationFor(material->blend),
                renderable.glossTextureID});
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
            renderable.overlayTextureID,
            renderable.sortKey,
            renderable.glossTextureID});
    }

    // Only when somebody has an opinion. With every key equal this returns
    // false having touched nothing, and the loop below records exactly the
    // sequence the view produced - which is what it recorded before this
    // existed.
    SortOpaqueDraws(opaque);

    // WHAT MAKES TWO DRAWS ONE DRAW is decided by PlanPass now, over the
    // items gathered here: everything that is not per-instance has to match -
    // the mesh, because its vertex and index buffers are bound once; the index
    // range, because that is what the draw call names; and the material set,
    // because it is a descriptor bound once. Everything else - the transform,
    // the colour, the roughness, the skin range - travels in the instance
    // record and is free to differ.
    std::vector<PassItem> opaqueItems;
    opaqueItems.reserve(opaque.size());

    for (const OpaqueDraw& draw : opaque) {
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

        // The same decision the counter uses, so the two cannot come to
        // disagree about what a draw is.
        const size_t sectionCount = DrawCallsForMesh(gpuMesh);

        for (size_t s = 0; s < sectionCount; ++s) {
            uint32_t albedo = draw.albedoTextureID;
            uint32_t normal = draw.normalTextureID;
            uint32_t orm = draw.ormTextureID;
            uint32_t overlay = draw.overlayTextureID;
            uint32_t gloss = draw.glossTextureID;
            uint32_t firstIndex = 0;
            uint32_t indexCount = draw.indexCount;

            if (multiSurface) {
                const MeshSection& section = gpuMesh->sections[s];
                firstIndex = section.firstIndex;
                indexCount = section.indexCount;
                albedo = section.albedoTextureID;
                normal = section.normalTextureID;
                orm = section.ormTextureID;
                // A file's surface carries no overlay (MeshMaterial has none),
                // and the entity's belongs to the entity's maps, which a
                // multi-surface mesh does not draw with. So black: it adds nothing.
                // Nor a gloss, and only a 2D sprite reads one: the neutral white.
                overlay = textures.GetBlackTexture();
                gloss = textures.GetWhiteTexture();
            }

            // One set per combination of maps, cached, so surfaces and entities
            // sharing a material do not rebind.
            //
            // Note the interaction with sorting: run-length grouping by mesh and
            // material survives only while equal keys keep their gather order,
            // which is why the sort is stable. A key per entity would destroy
            // it, exactly as distance ordering destroyed it for the transparent
            // pass.
            const vk::DescriptorSet materialSet =
                textures.AcquireMaterialSet(albedo, normal, orm, overlay, gloss);

            PassItem item;
            item.key = PassDraw{ static_cast<uint64_t>(draw.meshID), firstIndex, indexCount,
                                 HandleKey(static_cast<VkDescriptorSet>(materialSet)) };
            item.vertexBuffer = draw.vertexBuffer;
            item.indexBuffer = draw.indexBuffer;
            item.materialSet = materialSet;
            item.entity = draw.entity;
            item.matrix = &draw.matrix;

            // Written per section, not once for the mesh. HUSK's models carry
            // NO TEXTURES AT ALL - eight surfaces distinguished purely by
            // base colour, metallic and roughness - so a version of this that
            // only varied the maps would have looked finished and changed
            // nothing anyone could see.
            item.surface = multiSurface ? &gpuMesh->sections[s].material : nullptr;
            opaqueItems.push_back(item);
        }
    }

    // WHAT THE PASS WILL COST, answered before a single call is recorded, and
    // then done. There is no second opinion below about what a batch is: the
    // loop that used to decide as it went is gone, and with it the chance for
    // a counter and a submission to drift apart.
    {
        std::vector<PassDraw> keys;
        keys.reserve(opaqueItems.size());
        for (const PassItem& item : opaqueItems) keys.push_back(item.key);

        // Nothing is bound yet but the pipeline and set 0: the pass has just
        // bound its own pipeline, and a pipeline bind is what invalidates the
        // rest. Said rather than assumed, because it is the one thing the
        // planner cannot see for itself.
        const PassPlan plan = PlanPass(keys, 0, maxInstances);
        stats.dropped += plan.dropped;
        recordPass(opaqueItems, plan, pipeline);
    }

    // ---- Sky -------------------------------------------------------------
    //
    // After the opaque pass so it only shades pixels nothing claimed, and
    // before the transparent one so it cannot paint over a blended surface
    // that deliberately left the depth buffer alone. A fullscreen triangle,
    // needing no vertex input and no material set.
    if (skyPipeline) {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, skyPipeline->GetPipeline());
        ++stats.pipelineBinds;
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, skyPipeline->GetLayout(),
                                         VulkanPipeline::kSceneSet, 1, &sceneSet, 0, nullptr);
        commandBuffer.draw(3, 1, 0, 0);

        // A call belonging to no entity, which is why `drawn` could never have
        // accounted for it - and, for the same reason, one belonging to no
        // plan. It binds no mesh and no material set: a fullscreen triangle
        // needs no vertex input. So the frame's draw calls are the passes'
        // batches PLUS this, and a count taken only from the plans would
        // silently report one fewer on every scene with a sky.
        ++stats.drawCalls;

        // Nothing is rebound here on purpose: both blocks below bind their own
        // pipeline and reset the rebind-avoidance state, and the opaque pass
        // above has already finished with it.
    }

    // ---- Transparent pass ------------------------------------------------
    //
    // Back to front by depth ALONG THE VIEW DIRECTION, then by the sort key the
    // scene authored, then by the order they were gathered in. Three keys, so
    // the comparator is a total order and the frame a scene produces does not
    // depend on what std::sort felt like doing with equal elements.
    if (!transparent.empty()) {
        SortTransparentDraws(transparent);

        // ONE PIPELINE PER RUN of equal blend, taken in the sorted order - see
        // BlendRun for why the list is not grouped by blend instead. A frame
        // with no additive surface is one run, and records what it did before
        // there was a second blend.
        for (const BlendRun& run : BlendRuns(transparent)) {
            VulkanPipeline& runPipeline = run.blend == BlendEquation::Add ? additivePipeline
                                        : run.blend == BlendEquation::Premultiplied ? premultipliedPipeline
                                        : transparentPipeline;

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                       runPipeline.GetPipeline());
            ++stats.pipelineBinds;
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             runPipeline.GetLayout(),
                                             VulkanPipeline::kSceneSet, 1, &sceneSet, 0, nullptr);

            // BATCHED, CONSECUTIVELY, and the back-to-front order survives it -
            // see the particle pass below for the clause of Vulkan's primitive
            // order that says so. This pass refused batching on the grounds
            // that an instanced draw has no internal order; it has one, by
            // instance index, and the records are appended in the sorted order
            // already.
            //
            // Consecutive only, and for a stricter reason than the opaque pass
            // has: there, re-sorting would lose an authored sortKey. Here it
            // would lose the depth order itself, which is not a preference but
            // the difference between a correct frame and a wrong one. That rule
            // is PlanPass's now, and it is the same rule - which is the point of
            // there being one.
            std::vector<PassItem> blendedItems;
            blendedItems.reserve(run.count);

            for (uint32_t i = run.first; i < run.first + run.count; ++i) {
                const TransparentDraw& draw = transparent[i];
                if (draw.mesh->indexCount == 0) continue;

                const vk::DescriptorSet blendedSet =
                    textures.AcquireMaterialSet(draw.albedoTextureID, draw.normalTextureID,
                                                draw.ormTextureID, draw.overlayTextureID,
                                                draw.glossTextureID);

                PassItem item;

                // KEYED ON THE MESH POINTER, which is what this pass batches
                // on - the opaque pass has an id and this one does not, and the
                // two are equally good identities inside one frame because a
                // GpuMesh does not move while a frame is being recorded.
                //
                // The index range is the WHOLE mesh, not a section: this pass
                // has never walked sections, so a multi-surface blended mesh
                // draws every index under one material. Reproducing that is the
                // point; giving it sections here would be a behaviour change
                // wearing a refactor's clothes.
                item.key = PassDraw{ HandleKey(draw.mesh), 0, draw.mesh->indexCount,
                                     HandleKey(static_cast<VkDescriptorSet>(blendedSet)) };
                item.vertexBuffer = draw.mesh->vertexBuffer->GetBuffer();
                item.indexBuffer = draw.mesh->indexBuffer->GetBuffer();
                item.materialSet = blendedSet;
                item.entity = draw.entity;
                item.matrix = &draw.matrix;
                blendedItems.push_back(item);
            }

            std::vector<PassDraw> keys;
            keys.reserve(blendedItems.size());
            for (const PassItem& item : blendedItems) keys.push_back(item.key);

            // Starting where the pass before stopped, in the buffer they all
            // share. Nothing is bound: the pipeline bind above invalidates
            // whatever the last pass or run left, which is why this pass used
            // to reset its own rebind-avoidance state by hand.
            const PassPlan plan =
                PlanPass(keys, static_cast<uint32_t>(instances.size()), maxInstances);
            stats.dropped += plan.dropped;
            recordPass(blendedItems, plan, runPipeline);
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

    std::vector<ParticleDraw> particles;

    // Reserved against every emitter's POOL, not against the live count. A pool
    // is what the emitter has already paid for, so the upper bound is free to
    // read and the alternative is a second pass over the same memory. Twenty
    // thousand of these is 640 KB that would otherwise be reallocated and
    // recopied about fifteen times on the way up.
    {
        size_t pooled = 0;
        for (auto entity : registry.view<ParticleEmitterComponent>()) {
            pooled += registry.get<ParticleEmitterComponent>(entity).particles.size();
        }
        particles.reserve(pooled);
    }

    for (auto entity : registry.view<ParticleEmitterComponent>()) {
        const auto& emitter = registry.get<ParticleEmitterComponent>(entity);

        for (const auto& particle : emitter.particles) {
            if (!particle.active) continue;

            const float age = particle.maxLifetime > 0.0f ? particle.lifetime / particle.maxLifetime : 0.0f;

            particles.push_back(ParticleDraw{
                particle.position,
                particle.color,
                emitter.particleSize * glm::clamp(age, 0.15f, 1.0f),
                glm::dot(particle.position - viewPosition, viewDirection),
                static_cast<uint32_t>(particles.size())});
        }
    }

    if (particles.empty()) return;

    SortParticleDraws(particles);

    // THIS PASS DOES NOT GO THROUGH PlanPass, and the reason is that it has
    // no decision to make. Every particle in the frame shares one mesh, one
    // pipeline and one material set - all three bound below, once, outside
    // the loop - so no key can change inside the pass and the plan would be
    // one batch of every particle, which is what the loop already builds. A
    // list of twenty thousand identical planner inputs to be told that would
    // be a megabyte of scratch a frame to reach a conclusion known in advance.
    //
    // If a particle ever varies its material, this is where the rule has to
    // come from PlanPass rather than from here.
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                               transparentPipeline.GetPipeline());
    ++stats.pipelineBinds;
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     transparentPipeline.GetLayout(),
                                     VulkanPipeline::kSceneSet, 1, &sceneSet, 0, nullptr);

    const vk::Buffer buffers[] = { particleMesh->vertexBuffer->GetBuffer() };
    const vk::DeviceSize offsets[] = { 0 };
    commandBuffer.bindVertexBuffers(0, 1, buffers, offsets);
    commandBuffer.bindIndexBuffer(particleMesh->indexBuffer->GetBuffer(), 0, vk::IndexType::eUint32);
    ++stats.meshBinds;

    // UNCONDITIONAL, both of them, because this pass rebinds after whatever
    // the blended pass left behind rather than comparing against it. That is
    // one bind of each that a run-length rule would sometimes save, and it is
    // what the pass does today - so the plan is told it starts with nothing
    // bound, which is exactly what the two binds above make true.
    vk::DescriptorSet whiteSet = textures.AcquireMaterialSet(
        textures.GetWhiteTexture(), textures.GetFlatNormalTexture(),
        textures.GetNeutralOrmTexture(), textures.GetBlackTexture(),
        textures.GetWhiteTexture());
    if (whiteSet) {
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         transparentPipeline.GetLayout(),
                                         VulkanPipeline::kMaterialSet, 1, &whiteSet, 0, nullptr);
        ++stats.materialBinds;
    }

    // ONE DRAW CALL, AND THE BACK-TO-FRONT ORDER SURVIVES IT.
    //
    // This used to be a call per particle, refused batching on the grounds that
    // "two of them submitted as one instanced draw have no defined order
    // between themselves". That is not true, and it is the only thing that was
    // keeping the pass with the most submissions in the frame off the mechanism
    // built to remove them. Vulkan defines primitive order, and one of the four
    // clauses that make it up is "if a drawing command includes multiple
    // instances, the order in which instances are executed, from lower numbered
    // instances to higher" - primitive order is what rasterization order is
    // derived from, and rasterization order is what decides the sequence
    // fragments blend into the framebuffer in.
    //
    // So an instanced draw is ordered, by instance index, and the records were
    // already being appended in the sorted order. The only thing needed was to
    // stop cutting the run into pieces.
    //
    // Every particle in the engine shares one mesh, one pipeline and one
    // material set - all three are bound above, once, outside this loop - so
    // there is no key to compare and the whole sorted list is one batch. That
    // is the difference between a thousand-particle emitter costing a thousand
    // draw calls and costing one.
    int32_t batchFirst = -1;
    uint32_t batchCount = 0;

    for (const auto& draw : particles) {
        PushConstantData push{};
        push.model = glm::scale(glm::translate(glm::mat4(1.0f), draw.position), glm::vec3(draw.size));
        push.albedoColor = draw.color;
        push.material = glm::vec4(1.0f, 0.0f, 1.0f, 0.0f);

        const int32_t slot = record(push);

        // BREAK, not continue. `record` refuses only when the frame's instance
        // buffer is full, and it never empties again inside a frame - so every
        // later particle would be refused too, and carrying on would build the
        // same batch out of the same nothing while pretending to try.
        if (slot < 0) break;

        // Contiguity is the invariant this batch rests on, and it holds because
        // `record` appends and nothing else records between two particles.
        if (batchCount == 0) batchFirst = slot;
        ++batchCount;
        ++stats.particlesDrawn;
    }

    if (batchCount > 0) {
        commandBuffer.drawIndexed(particleMesh->indexCount, batchCount, 0, 0,
                                  static_cast<uint32_t>(batchFirst));
        ++stats.drawCalls;
    }
}

} // namespace Supersonic
