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

        // Depth passes that were not recorded because their shadow map would
        // have come out identical. Out of eighteen.
        uint32_t shadowPassesSkipped{0};
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
    //
    // The sky is drawn by this function rather than after it, and the reason is
    // the transparent pass. A blended surface does not write depth, so a pane
    // with nothing but sky behind it leaves the depth buffer at the clear value
    // - and a sky drawn afterwards at z = 1.0 with a lessOrEqual compare passes
    // that test and paints over the pane. Transparency worked indoors, where
    // opaque geometry had claimed the depth, and vanished against the horizon.
    //
    // Drawn between the two passes rather than before both: the depth
    // rejection that makes the sky nearly free still works, because the opaque
    // pass has already run.
    static void Render(
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
        Stats& stats
    );

    // One entity, resolved down to what a depth pass actually needs.
    //
    // A frame runs EIGHTEEN depth passes - four cascades, six faces for each
    // point-light slot, one for each spot slot - and every one of them used to
    // walk the registry from scratch: two component lookups, a mesh-registry
    // lookup, a try_get for the skin, and eight corner transforms to build the
    // world bounds. All of that is identical in all eighteen. The only thing
    // that differs between passes is which frustum the bounds are tested
    // against.
    //
    // So it is gathered once and the passes read it. That is the difference
    // between eighteen registry traversals per frame and one.
    // One opaque draw, gathered before any of them is recorded.
    //
    // The opaque pass used to record each draw the instant it found it, so its
    // order WAS the registry's and there was no moment at which an order could
    // be chosen. Gathering first is what makes sortKey possible; the transparent
    // pass has always worked this way, because a back-to-front sort needs the
    // same thing.
    struct OpaqueDraw {
        glm::mat4 matrix{1.0f};

        // BY VALUE, for the reason spelled out on ShadowCaster below: Get
        // returns a pointer INTO a vector that Upload push_backs onto, so one
        // upload mid-frame would dangle every pointer gathered before it.
        vk::Buffer vertexBuffer{};
        vk::Buffer indexBuffer{};

        entt::entity entity{entt::null};
        uint32_t indexCount{0};
        uint32_t meshID{0};
        uint32_t albedoTextureID{0};
        uint32_t normalTextureID{0};
        uint32_t ormTextureID{0};
        int32_t sortKey{0};
    };

    // Orders a gathered opaque list by sortKey, ascending and STABLY.
    //
    // Returns whether it reordered anything. False means every key was equal,
    // the list was left untouched, and the caller records exactly the sequence
    // it gathered - which is what makes a scene that sets no key render as it
    // always did, rather than as whatever std::sort felt like.
    //
    // Stable, so equal keys keep gather order. That order is the registry's and
    // is not a contract, but it is TODAY's, and preserving it is the difference
    // between "no scene changed" and "no scene changed much".
    //
    // Static and pure so a test can reach it with no device, no registry and no
    // mesh registry - the same reason ShadowAlphaFor is exposed.
    static bool SortOpaqueDraws(std::vector<OpaqueDraw>& draws);

    struct ShadowCaster {
        glm::mat4 model{1.0f};

        // WORLD bounds, already transformed. This is the eight-corner
        // transform that was being redone per pass.
        glm::vec3 worldMin{0.0f};
        glm::vec3 worldMax{0.0f};

        // BY VALUE, not a GpuMesh*.
        //
        // This held a pointer, justified by the mesh registry being write-once.
        // It is not: Replace moves new buffers over an existing id and
        // Invalidate guts one, which is why the class keeps a generation
        // counter. And Get returns a pointer INTO a std::vector that Upload
        // push_backs onto, so one upload during a frame would dangle every
        // pointer gathered before it.
        //
        // Nothing acquires a mesh inside DrawFrame today, so the pointer never
        // actually dangled - but the only thing standing between that and a use
        // after free was a call ordering, defended by a comment that was wrong
        // about why. Three values weigh less than a pointer plus that argument.
        vk::Buffer vertexBuffer{};
        vk::Buffer indexBuffer{};
        uint32_t indexCount{0};
        uint32_t meshID{0};

        uint32_t skinPaletteBase{0};
        int32_t skinJointCount{0};

        // Above zero means this caster occludes only where its albedo is
        // opaque enough, and it is drawn by the cut-out depth pipeline instead
        // of the plain one. Zero - the overwhelmingly common case - means the
        // depth pass never looks at a texture, exactly as before.
        float alphaCutoff{0.0f};

        // The material's own alpha factor, the second half of the same test
        // the scene pass makes: texture alpha times factor, against the cutoff.
        float baseAlpha{1.0f};

        // The albedo this caster is cut against, resolved once for the frame.
        //
        // A HANDLE rather than the texture id, for the reason vertexBuffer
        // above is one: TextureRegistry::ReplaceRGBA swaps the image under a
        // stable id and drops every material set naming it, so the id alone
        // would leave a leaf cut to the texture it replaced - and the shadow
        // cache, which hashes this struct, would never notice.
        //
        // Null on an opaque caster, which is what alphaCutoff == 0 means.
        vk::DescriptorSet materialSet{};
    };

    // What a material asks of the depth pass, reduced to the two numbers the
    // pass can act on. Split out from the gather so the policy is testable
    // without a device: the gather itself needs a MeshRegistry with real GPU
    // buffers in it, and this does not.
    //
    // `casts` is false only for a blended surface that named no cutoff: it
    // occludes nowhere, and never reaches the depth pass at all.
    struct ShadowAlpha {
        bool casts{true};
        float cutoff{0.0f};
        float baseAlpha{1.0f};
    };
    static ShadowAlpha ShadowAlphaFor(const MaterialComponent* material);

    // Everything visible that casts a shadow. Clears `out` and refills it, so a
    // caller can keep one vector for the life of the renderer and never
    // allocate again after the first frame.
    //
    // PARTITIONED, not in registry order: every opaque caster first, then every
    // cut-out one. The depth pass then switches pipeline once per pass instead
    // of once per run of casters, and an opaque caster never pays for the
    // cut-out pipeline's descriptor bind.
    static void GatherShadowCasters(entt::registry& registry, MeshRegistry& meshes,
                                    TextureRegistry& textures,
                                    std::vector<ShadowCaster>& out);

    // What an entity's mesh and texture ids depend on, reduced to a number.
    //
    // Never zero, so a component that has never been resolved is distinguishable
    // from one whose inputs happen to hash to nothing. Either component may be
    // absent, and absent is a different answer from present-and-empty: an entity
    // with no MeshComponent falls back to the cube, and one with an empty path
    // asks the registry for the default primitive.
    static uint64_t ResourceSignature(const MeshComponent* mesh,
                                      const MaterialComponent* material,
                                      uint64_t meshGeneration,
                                      uint64_t textureGeneration);

    // Everything one depth pass would draw, reduced to a number.
    //
    // Two shadow maps rendered from the same signature are the same image, so a
    // pass whose signature has not changed since it was last recorded can be
    // skipped entirely - and eighteen render passes per frame is most of what
    // the shadow half of a small scene costs.
    //
    // Culled first, deliberately. A signature over EVERY caster would be dirtied
    // by anything moving anywhere in the level, which in a scene where anything
    // moves means never skipping a pass. Taking only what survives this light's
    // frustum means a crate moving at the far end of the level leaves the lamp
    // over here alone. The cull is the cheap half of the pass, so paying for it
    // twice on the frames that do render is a good trade against paying for the
    // whole pass on the frames that need not.
    //
    // What goes into it is everything that can change the image: the light's
    // transform, and per visible caster its world matrix, its bounds, its mesh,
    // that mesh's buffer handle and index count - so an asset reloaded in place
    // is a different signature - and its skinning offsets. The joint palette
    // itself is mixed in by the caller, since it lives in the renderer.
    static uint64_t ShadowPassSignature(const std::vector<ShadowCaster>& casters,
                                        const glm::mat4& lightViewProj,
                                        const Frustum& lightFrustum,
                                        uint64_t seed);

    // Mixes arbitrary bytes into a signature. FNV-1a: not a hash anyone should
    // rely on for anything but change detection, which is all this is - a
    // collision means a stale shadow, and at 64 bits that is not a risk worth
    // a stronger hash.
    static uint64_t MixSignature(uint64_t signature, const void* data, size_t bytes);

    // Shadow pass for one cascade, cube face or spot.
    //
    // Two pipelines, because the casters want two different things. Almost all
    // of them are solid: only positions matter, no material is bound, and the
    // fragment stage writes nothing. The rest are cut-out, and occlude only
    // where their albedo is opaque - those need the texture, the cutoff, and a
    // cull mode that does not swap which face of a card is doing the casting.
    //
    // The list arrives partitioned, so this switches between the two once.
    static void RenderDepthOnly(
        const std::vector<ShadowCaster>& casters,
        VulkanPipeline& pipeline,
        VulkanPipeline& cutoutPipeline,
        vk::CommandBuffer commandBuffer,
        vk::DescriptorSet sceneSet,
        const glm::mat4& lightViewProj,
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
