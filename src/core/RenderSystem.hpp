#pragma once

#include <entt/entt.hpp>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>

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

        // Of `drawn`, the ones diverted into the blended pass. Opaque and
        // blended cost different things and are fixed by different work, and
        // until now they were one number.
        uint32_t transparentDrawn{0};

        // DRAW CALLS, which is not the same number as `drawn` and never was.
        //
        // `drawn` counts ENTITIES that survived the frustum. A multi-material
        // model is one of those and issues one call per surface, the sky is a
        // call belonging to no entity, and a particle system is a call per
        // particle counted nowhere at all. So "one draw call per drawable" was
        // a property this engine did not have and nothing could see it.
        //
        // The scene pass only. The shadow pass has `shadowDrawn`, which is
        // already one-to-one with its own submissions.
        //
        // This is the number instancing moves. It is here so that when
        // instancing lands the drop is recorded rather than asserted.
        uint32_t drawCalls{0};

        // Particles submitted. NOT a call each any more - every particle in a
        // frame shares one mesh and one material set, so the whole sorted list
        // goes down as a single instanced draw. Counted separately because a
        // particle is not a drawable, and lumping the two together would make
        // `drawCalls` move for a reason `drawn` cannot explain.
        uint32_t particlesDrawn{0};
        // Summed across every cascade, so with four cascades an object visible
        // in two of them counts twice - which is what it costs.
        uint32_t shadowDrawn{0};
        uint32_t shadowCulled{0};

        // Joint matrices uploaded this frame, across every skinned entity.
        uint32_t skinnedMatrices{0};

        // Depth passes that were not recorded because their shadow map would
        // have come out identical. Out of eighteen.
        uint32_t shadowPassesSkipped{0};

        // ---- State changes, which are what batching actually removes ------
        //
        // The draw-call counter records that batching happened; these record
        // what it was FOR. A batch closes because something has to be bound,
        // so a frame's binds are the frame's batches minus the ones that
        // continued - and until now the only evidence that ten thousand cubes
        // bind one mesh once was that the draw count fell.
        //
        // THREE NUMBERS RATHER THAN ONE. A fused `stateChanges` hides which
        // bind moved, and the three cost different things and are fixed by
        // different work: a pipeline bind is the expensive one and happens
        // three times a frame by construction, a mesh bind is two buffer
        // binds, and a material bind is a descriptor set. Same argument the
        // struct already makes for splitting `drawn` from `drawCalls`.
        //
        // THE SCENE PASS ONLY, exactly as `drawCalls` is. The eighteen depth
        // passes bind their own pipeline once each and are counted by
        // `shadowDrawn` and `shadowPassesSkipped`; folding them in here would
        // make a number that moves when a light is added and cannot say why.
        uint32_t pipelineBinds{0};
        uint32_t meshBinds{0};
        uint32_t materialBinds{0};

        // Draws the frame REFUSED because its instance buffer was full, summed
        // across the passes.
        //
        // PassPlan has counted this since planning was split out, and nothing
        // ever read it: the number was computed, used to skip the draw, and
        // dropped on the floor. So a frame that silently declined to draw part
        // of the scene was indistinguishable, in every counter the engine has,
        // from a frame that drew all of it. That is precisely the shape of
        // "things disappear sometimes", and it should never have been the one
        // outcome nothing could see.
        uint32_t dropped{0};
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
        // The blended pipeline that adds rather than mixes, for materials
        // whose blend is Additive. Bound per run - see BlendRun.
        VulkanPipeline& additivePipeline,
        // And the one that composites a colour already multiplied by its
        // alpha, for materials whose blend is Premultiplied.
        VulkanPipeline& premultipliedPipeline,
        VulkanPipeline* skyPipeline,
        MeshRegistry& meshes,
        TextureRegistry& textures,
        vk::CommandBuffer commandBuffer,
        vk::DescriptorSet sceneSet,
        const Frustum& frustum,
        const glm::vec3& viewPosition,
        Stats& stats,

        // Where the per-draw records go. FILLED here and uploaded by the caller
        // after recording finishes, which is in time: recording writes draw
        // commands, and the buffer those commands read is not touched until the
        // GPU runs them.
        //
        // A vector rather than a mapped pointer, so the count is the vector's
        // own size and cannot disagree with what was written.
        std::vector<PushConstantData>& instances,
        uint32_t maxInstances
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
        uint32_t overlayTextureID{0};

        int32_t sortKey{0};

        // The gloss map's, LAST: the gather builds this positionally, and a
        // field added in the middle would shift every value after it into its
        // neighbour's place without a word from the compiler.
        uint32_t glossTextureID{0};
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

    // ---- The plan: what a pass will bind and draw, decided before any of it
    // is recorded ----------------------------------------------------------
    //
    // Three passes in this file batch consecutive draws, and until now each
    // wrote the rule out for itself: the opaque pass keyed on mesh id, index
    // range and material set; the blended pass on a mesh POINTER and a
    // material set; the particles on nothing at all, because everything they
    // bind is bound once outside their loop. Three copies of one decision,
    // and not one of them was reachable by a test - the whole thing needs a
    // command buffer, a device and two registries to run.
    //
    // What that costs is written in the plan this engine already has: "assert
    // counts, never milliseconds", against sixty-five suites and not one
    // asserting on any cost quantity. "One draw call per drawable" was false
    // for a year and invisible, and the counter that found it can only be
    // read by launching the editor and looking at a panel.
    //
    // So the decision is separated from the recording. PlanPass takes the
    // draws a pass is about to submit and answers what it will cost: which
    // batches, how many binds, how many records, what is dropped. Pure,
    // static, device-free, and the recorder does what the plan says rather
    // than deciding again - so the counter and the loop cannot come to
    // disagree, which is the same argument DrawCallsForMesh already makes one
    // level down.

    // One draw a pass is about to submit, reduced to what decides whether it
    // can join the batch in front of it.
    //
    // Keyed by NUMBER rather than by Vulkan handle so the decision is
    // testable: a descriptor set and a mesh id are both just identities to
    // the batcher, and a suite cannot make a descriptor set. The caller
    // supplies whatever identity its pass batches on - a mesh id, a pointer,
    // a handle - and only equality is ever asked of it.
    struct PassDraw {
        uint64_t meshKey{0};
        uint32_t firstIndex{0};
        uint32_t indexCount{0};

        // ZERO MEANS NO MATERIAL SET, and therefore no bind - which is what a
        // null descriptor set already meant to the loops this replaces.
        uint64_t materialKey{0};
    };

    // One drawIndexed, and what has to be bound before it.
    struct PassBatch {
        uint64_t meshKey{0};
        uint32_t firstIndex{0};
        uint32_t indexCount{0};
        uint64_t materialKey{0};

        // Where this batch's instance records start and how many there are.
        // Contiguous by construction, which is what lets one call name them.
        uint32_t firstInstance{0};
        uint32_t instanceCount{0};

        // Whether the recorder must bind before issuing this batch. False
        // when the previous batch left the right thing bound - which is the
        // whole point of the run-length grouping and the number the state
        // counters report.
        bool bindsMesh{false};
        bool bindsMaterial{false};
    };

    struct PassPlan {
        std::vector<PassBatch> batches;

        uint32_t meshBinds{0};
        uint32_t materialBinds{0};

        // Instance records the recorder must write, in draw order.
        uint32_t instances{0};

        // Draws refused because the frame's instance buffer is full. Planned
        // rather than discovered, so the capacity a pass runs against is a
        // number a test can set to three.
        uint32_t dropped{0};

        uint32_t DrawCalls() const { return static_cast<uint32_t>(batches.size()); }
    };

    // No mesh and no material set bound yet. Not a valid key for either: a
    // mesh id is a vector index and a descriptor handle is a pointer.
    static constexpr uint64_t kNothingBound = 0xFFFFFFFFFFFFFFFFull;

    // Groups consecutive draws into batches, CONSECUTIVE ONLY AND NEVER A
    // RE-SORT.
    //
    // The order of `draws` is load-bearing in all three callers and for two
    // different reasons: the opaque pass carries an authored sortKey, and the
    // blended and particle passes carry a back-to-front depth order that is
    // the difference between a correct frame and a wrong one. A batch closes
    // the moment the mesh, the index range or the material set changes.
    //
    // `firstInstance` is where this pass's records start in the frame's
    // shared instance buffer, and `capacity` is that buffer's size: the
    // passes run one after another into one buffer, so a pass cannot decide
    // its own capacity without knowing what came before it.
    //
    // `boundMesh` and `boundMaterial` are what the pass starts with already
    // bound. Every caller today starts with nothing, and passing that in
    // rather than assuming it is what lets a test ask whether the first draw
    // of a pass rebinds something it already has.
    static PassPlan PlanPass(const std::vector<PassDraw>& draws,
                             uint32_t firstInstance,
                             uint32_t capacity,
                             uint64_t boundMesh = kNothingBound,
                             uint64_t boundMaterial = kNothingBound);

    // How many draw calls one drawable costs in the opaque pass.
    //
    // ONE PER SURFACE for a multi-material model, one for everything else. A
    // single-section mesh takes the same path as a sectionless one on purpose -
    // see GpuMesh::sections - so the loop has no special case, and neither does
    // this.
    //
    // Split out for the same reason the sorts are: it is the DECISION the draw
    // loop bounds itself by, it needs no device, and it is the only place the
    // gap between "drawables" and "draw calls" is decided. The loop uses it, so
    // the counter and the loop cannot come to disagree about what a draw is.
    //
    // A null mesh is one call, matching the loop: a drawable whose mesh has
    // gone is still bound and pushed, it simply submits nothing.
    static uint32_t DrawCallsForMesh(const GpuMesh* mesh);

    // One blended draw, gathered before the pass is recorded.
    //
    // Out here beside OpaqueDraw rather than local to Render for the reason
    // SortOpaqueDraws is static: the ordering is the part with a decision in
    // it, and a decision that cannot be reached without a device is a decision
    // nothing tests.
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
        // And the black overlay's, for the same reason.
        uint32_t overlayTextureID{4};

        // ALONG THE VIEW DIRECTION, not the distance to the camera.
        //
        // This was the squared distance to the camera position, which orders
        // correctly only when everything is roughly ahead of the view. Under an
        // ORTHOGRAPHIC projection it is wrong outright: what decides occlusion
        // is depth along the view axis alone, so two quads at the same depth
        // sorted by how far SIDEWAYS they were, and one off to the edge drew
        // behind one straight ahead. A 2D scene is entirely made of quads at a
        // few depths spread across the screen, which is the case this breaks
        // hardest and the reason it was found.
        //
        // Signed, and not squared: a square throws away the sign, so anything
        // behind the camera sorted as though it were the same distance in
        // front. Perspective culling hid that; an ortho box does not.
        float viewDepth{0.0f};

        // The tie-break, so the order is TOTAL.
        //
        // Coplanar transparent quads are the normal case in 2D, and their depth
        // is not merely close but equal - at which point std::sort is free to
        // order them however it likes, and did. That is a flicker whose cause
        // is the standard library, and it is the same defect the broadphase had
        // until its comparator was made total.
        //
        // sortKey is what the engine already offers for exactly this, and the
        // opaque pass has honoured it since sortKey existed. The blended pass
        // ignored it, so the one place a 2D game most needs explicit layering
        // was the one place it did not work.
        int32_t sortKey{0};

        // Last resort, and it is the gather order. Two draws agreeing on depth
        // AND on sortKey are genuinely unordered by anything the scene said, so
        // this pins them to the order they were found in rather than leaving it
        // to the sort. Stable input, stable frame.
        uint32_t gathered{0};

        // How it composites with what is behind it (MaterialComponent::blend):
        // mixed, added, or premultiplied. Not a sort key: see BlendRun.
        BlendEquation blend{BlendEquation::Mix};

        // The gloss map's, last for the reason OpaqueDraw gives. 0 is the
        // white texture, which is also the gloss map's neutral.
        uint32_t glossTextureID{0};
    };

    // Orders blended draws back to front, then by sort key, then by gather
    // order. A TOTAL order on purpose - see TransparentDraw::sortKey.
    //
    // Static and pure, so a test can reach it with no device, no registry and
    // no mesh registry. The same reason SortOpaqueDraws and ShadowAlphaFor are
    // exposed.
    static void SortTransparentDraws(std::vector<TransparentDraw>& draws);

    // A run of consecutive blended draws, in the SORTED order, sharing a blend.
    //
    // The blend is pipeline state, so each run is one pipeline bind. The runs
    // are cut from the sorted list rather than the list being grouped by blend
    // first: grouping would save binds and draw every glow after every pane, or
    // before it, whatever their depths - right in most frames, and wrong in
    // exactly the one where a halo sits between two sprites.
    struct BlendRun {
        uint32_t first{0};
        uint32_t count{0};
        BlendEquation blend{BlendEquation::Mix};
    };

    // The runs of `sorted`, in order. None for an empty list, and one for a
    // frame in which every blended surface blends alike - which records what
    // the pass recorded before there was more than one blend.
    static std::vector<BlendRun> BlendRuns(const std::vector<TransparentDraw>& sorted);

    // The pipeline equation a transparent material's blend asks for. Alpha is
    // Mix, Additive is Add, Premultiplied is Premultiplied: one table, so the
    // gather and a suite cannot come to disagree about it.
    static BlendEquation EquationFor(MaterialComponent::BlendMode blend);

    // What a material writes into its draw's record AFTER everything else has
    // had its say - the material branch and a mesh surface's override both
    // write albedoColor, material and emissive, and a 2D sprite gives those
    // fields a meaning of their own (PushConstantData::kSprite2D).
    //
    //   - unlit with sprite2D.enabled: kSprite2D, kNormalYDown when asked,
    //     the light mask, albedoColor.rgb = tint x ambient, emissive =
    //     (tint, height), material = (overlayStrength, 0, 0, alphaCutoff).
    //   - transparent with blend Premultiplied: kPremultiplied.
    //
    // Anything else is left exactly as it was, so every existing draw writes
    // the record it wrote before. Static and pure so a suite can read the
    // packing; the draw loop's buildPushConstants calls it last.
    static void ApplySprite2D(const MaterialComponent& material, PushConstantData& push);

    // One live particle, gathered from every emitter before any of them is
    // recorded. Out here beside TransparentDraw for the same reason: the
    // ordering is the part with a decision in it.
    struct ParticleDraw {
        glm::vec3 position{0.0f};
        glm::vec4 color{1.0f};
        float size{1.0f};

        // Along the view direction and signed, for the reason
        // TransparentDraw::viewDepth gives.
        float viewDepth{0.0f};

        // THE TIE-BREAK, and particles need it more than meshes do.
        //
        // Every particle an emitter spawns on one frame starts at the emitter's
        // own position, so their depths are not merely close but BIT-EQUAL -
        // and the comparator was a single float run through std::sort, which is
        // free to order equal elements however it likes and is not even
        // required to do it the same way twice. That is the defect
        // TransparentDraw::sortKey was added to close, in the one pass where
        // equal keys are the common case rather than the coincidence.
        //
        // There is no authored key to break the tie with: a particle is not an
        // entity and nothing can give it one. Gather order is what there is,
        // and it is a real order - emitters in registry order, particles in
        // pool order - so the frame is reproducible even though the tie is
        // arbitrary.
        uint32_t gathered{0};
    };

    // Orders particles back to front, then by gather order. Total, static and
    // pure, for the reason SortTransparentDraws is.
    static void SortParticleDraws(std::vector<ParticleDraw>& draws);

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

        // WHICH RUN OF INDICES. A caster used to be a whole mesh, because the
        // depth pass had one material for it - and that made a cut-out on any
        // surface but the first impossible to express: the holes are in that
        // surface's texture, and one draw can bind only one.
        //
        // A mesh whose surfaces all cut at zero is still ONE caster covering
        // every index, which is the overwhelmingly common case and every model
        // in HUSK. Splitting only happens where a surface actually cuts, so
        // nothing that did one draw now does eight.
        uint32_t firstIndex{0};
        uint32_t indexCount{0};
        uint32_t meshID{0};

        uint32_t skinPaletteBase{0};
        int32_t skinJointCount{0};

        // How this caster's albedo is sampled. Identity for almost every
        // caster; the exception is a cut-out surface whose material scrolls,
        // where the holes are somewhere other than where the texture puts them.
        UvTransform uvTransform{};

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
    //
    // Every path that selects a texture is in it: albedo, normal, ORM and the
    // overlay.
    //
    // `decodesColourTextures` is RenderSettings::decodesColourTextures() for the
    // scene: the same albedo path is a different upload in each colour space,
    // so a scene switching its encoding must re-resolve every entity even
    // though no path on any of them changed. Not defaulted, so a caller cannot
    // forget the scene has a say.
    static uint64_t ResourceSignature(const MeshComponent* mesh,
                                      const MaterialComponent* material,
                                      uint64_t meshGeneration,
                                      uint64_t textureGeneration,
                                      bool decodesColourTextures);

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

    // What a surface actually looks like once the entity has had its say.
    //
    // The file describes the model; an override re-materialises one named
    // surface of it. Returned by value rather than applied in place because the
    // file's material belongs to the MESH and is shared by every entity drawing
    // it - writing an entity's team colour into it would colour every other
    // entity's copy, which is the kind of bug that looks like a race.
    //
    // A free function for the same reason MaterialSystem::ApplyImportedMaterial
    // is one: the draw loop needs a Vulkan device to reach and this rule does
    // not, so it can be tested against numbers instead of against a picture.
    static MeshMaterial ResolveSurface(const MeshMaterial& fromFile,
                                       const SurfaceOverridesComponent* overrides);
};

} // namespace Supersonic
