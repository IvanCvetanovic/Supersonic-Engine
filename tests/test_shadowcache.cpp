// Tests for the decision that skips a shadow pass.
//
// A frame records eighteen depth passes - four cascades, six faces for each
// point-light slot, one for each spot slot - and in a scene where nothing has
// moved, every one of them produces the image it produced last frame.
//
// The reason this is tested here rather than through a rendered frame: a cache
// that never invalidates renders a perfectly plausible picture. It is simply
// last frame's, and it looks entirely correct in a screenshot of a scene that
// happens to be still. An end-to-end pixel comparison would also need play mode
// to be reproducible, and it is not - three runs of the same binary on the same
// scene give three different images, because the simulation clock is the
// wall-clock frame delta. So the decision is separated from the recording and
// the decision is what gets tested.

#include "TestHarness.hpp"
#include "core/RenderSystem.hpp"
#include "renderer/ShadowCache.hpp"
#include "renderer/Frustum.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <vector>

using namespace Supersonic;

namespace {

RenderSystem::ShadowCaster makeCaster(const glm::vec3& position, uint32_t meshID = 7) {
    RenderSystem::ShadowCaster caster;
    caster.model = glm::translate(glm::mat4(1.0f), position);
    caster.worldMin = position - glm::vec3(0.5f);
    caster.worldMax = position + glm::vec3(0.5f);
    caster.meshID = meshID;
    // Left default: the buffer handles and index count are part of the
    // signature, and there is no device here to make real ones with. Every
    // other input is exercised, and they are plain values now rather than a
    // pointer that had to be null.
    return caster;
}

// A descriptor set handle that is merely DIFFERENT. Nothing here dereferences
// it - the signature hashes the handle, exactly as it hashes the vertex buffer
// handle - and there is no device to allocate a real one from.
vk::DescriptorSet fakeDescriptorSet(uintptr_t value) {
    return vk::DescriptorSet(reinterpret_cast<VkDescriptorSet>(value));
}

// A light looking down the -z axis from the origin, seeing roughly x,y in
// [-4, 4] out to z = -50.
glm::mat4 lightMatrix(const glm::vec3& eye) {
    const glm::mat4 proj = glm::ortho(-4.0f, 4.0f, -4.0f, 4.0f, 0.1f, 50.0f);
    const glm::mat4 view = glm::lookAt(eye, eye + glm::vec3(0.0f, 0.0f, -1.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    return proj * view;
}

uint64_t signatureOf(const std::vector<RenderSystem::ShadowCaster>& casters,
                     const glm::mat4& light, uint64_t seed = 1469598103934665603ull) {
    return RenderSystem::ShadowPassSignature(casters, light, Frustum::FromMatrix(light), seed);
}

} // namespace

// --- the cache's state machine ----------------------------------------------

static void testAPassIsAlwaysRecordedTheFirstTime() {
    // Not defensiveness. A shadow image is created in an undefined layout and
    // only reaches a readable one by being rendered through the pass, so a slot
    // that has never been recorded is not stale, it is unusable - and the cube
    // maps are one descriptor array, so a single untouched slot invalidates all
    // of them.
    ShadowCache cache;
    CHECK_MSG(cache.NeedsRender(0, 12345ull), "the first frame must record");
    CHECK_MSG(!cache.NeedsRender(0, 12345ull), "the second must not");

    // And with a signature of ZERO, which is the case the separate flag exists
    // for. Comparing against a stored signature alone would treat a fresh entry
    // as already holding zero and skip the one pass that must never be skipped.
    // Zero is not a reserved value; it is what an empty pass under a zero light
    // matrix legitimately hashes to.
    ShadowCache fresh;
    CHECK_MSG(fresh.NeedsRender(0, 0ull), "a first signature of zero must still record");
    CHECK_MSG(!fresh.NeedsRender(0, 0ull), "and then settle like any other");
}

static void testAChangedSignatureRecordsAgain() {
    ShadowCache cache;
    cache.NeedsRender(3, 100ull);
    CHECK_MSG(cache.NeedsRender(3, 101ull), "a different signature must record");
    CHECK_MSG(!cache.NeedsRender(3, 101ull), "and then settle");

    // Back to a signature seen before, but not the LAST one seen. It must still
    // record: the image currently in that slot is the newer one.
    CHECK_MSG(cache.NeedsRender(3, 100ull), "returning to an older signature must record");
}

static void testPassesDoNotShareState() {
    // Eighteen passes in one cache. A slot index used as a key that collides -
    // a cascade with a cube face, say - would leave one of them permanently
    // showing the other's shadows.
    ShadowCache cache;
    for (std::size_t pass = 0; pass < 18; ++pass) {
        CHECK(cache.NeedsRender(pass, 500ull + pass));
    }
    for (std::size_t pass = 0; pass < 18; ++pass) {
        CHECK_MSG(!cache.NeedsRender(pass, 500ull + pass),
                  "pass " + std::to_string(pass) + " must have its own entry");
    }

    // Dirtying one must not dirty its neighbours.
    CHECK(cache.NeedsRender(9, 999ull));
    CHECK_MSG(!cache.NeedsRender(8, 508ull), "the pass before it must be untouched");
    CHECK_MSG(!cache.NeedsRender(10, 510ull), "and the one after");
}

static void testInvalidateForgetsEverything() {
    // For a resize or a shadow map rebuilt at a new resolution: the images
    // themselves are new and undefined again, so remembering their old
    // signatures would skip straight past them.
    ShadowCache cache;
    cache.NeedsRender(0, 42ull);
    cache.NeedsRender(1, 43ull);

    cache.Invalidate();

    CHECK_MSG(cache.NeedsRender(0, 42ull), "the same signature must record after an invalidate");
    CHECK_MSG(cache.NeedsRender(1, 43ull), "for every pass");
}

static void testAnOutOfRangePassIsAlwaysRecorded() {
    // Wrong in the safe direction. If the pass count ever outgrows the cache,
    // the extra passes are recorded every frame rather than silently sharing
    // an entry with pass zero.
    ShadowCache cache;
    CHECK(cache.NeedsRender(ShadowCache::kMaxPasses, 1ull));
    CHECK(cache.NeedsRender(ShadowCache::kMaxPasses, 1ull));
}

// --- what goes into a signature ---------------------------------------------

static void testMovingAVisibleCasterChangesTheSignature() {
    // The whole point. If this does not hold, a shadow freezes where it was and
    // the object walks out from under it.
    const glm::mat4 light = lightMatrix(glm::vec3(0.0f, 0.0f, 10.0f));

    std::vector<RenderSystem::ShadowCaster> before{ makeCaster(glm::vec3(0.0f, 0.0f, 0.0f)) };
    std::vector<RenderSystem::ShadowCaster> after{ makeCaster(glm::vec3(1.0f, 0.0f, 0.0f)) };

    CHECK_MSG(signatureOf(before, light) != signatureOf(after, light),
              "moving a caster the light can see must dirty its shadow map");

    // And a move too small to see is still a move. There is no threshold here
    // on purpose: a threshold is a decision about how wrong a shadow may be,
    // and nobody has the information to make it.
    std::vector<RenderSystem::ShadowCaster> nudged{ makeCaster(glm::vec3(0.001f, 0.0f, 0.0f)) };
    CHECK_MSG(signatureOf(before, light) != signatureOf(nudged, light),
              "a small move is a move");
}

static void testMovingACasterTheLightCannotSeeChangesNothing() {
    // The reason the signature is taken AFTER culling rather than over every
    // caster in the level. A signature over everything is dirtied by anything
    // moving anywhere, which in a scene where anything moves means never
    // skipping a pass at all - a cache that is correct and never hits.
    const glm::mat4 light = lightMatrix(glm::vec3(0.0f, 0.0f, 10.0f));

    std::vector<RenderSystem::ShadowCaster> before{
        makeCaster(glm::vec3(0.0f, 0.0f, 0.0f)),
        makeCaster(glm::vec3(400.0f, 0.0f, 0.0f)),
    };
    std::vector<RenderSystem::ShadowCaster> after{
        makeCaster(glm::vec3(0.0f, 0.0f, 0.0f)),
        makeCaster(glm::vec3(430.0f, 60.0f, 0.0f)),
    };

    CHECK_MSG(signatureOf(before, light) == signatureOf(after, light),
              "a crate moving at the far end of the level must leave this lamp alone");

    // The control: the same test is worthless if the far caster was never
    // outside the frustum to begin with.
    const Frustum frustum = Frustum::FromMatrix(light);
    CHECK_MSG(frustum.IntersectsAABB(before[0].worldMin, before[0].worldMax),
              "the near caster must be visible");
    CHECK_MSG(!frustum.IntersectsAABB(before[1].worldMin, before[1].worldMax),
              "and the far one must not be");
}

static void testMovingTheLightChangesTheSignature() {
    std::vector<RenderSystem::ShadowCaster> casters{ makeCaster(glm::vec3(0.0f, 0.0f, 0.0f)) };

    CHECK_MSG(signatureOf(casters, lightMatrix(glm::vec3(0.0f, 0.0f, 10.0f))) !=
                  signatureOf(casters, lightMatrix(glm::vec3(0.5f, 0.0f, 10.0f))),
              "moving the light must dirty its own shadow map");
}

static void testAppearingAndDisappearingCastersChangeTheSignature() {
    const glm::mat4 light = lightMatrix(glm::vec3(0.0f, 0.0f, 10.0f));

    const std::vector<RenderSystem::ShadowCaster> none;
    std::vector<RenderSystem::ShadowCaster> one{ makeCaster(glm::vec3(0.0f, 0.0f, 0.0f)) };
    std::vector<RenderSystem::ShadowCaster> two{
        makeCaster(glm::vec3(0.0f, 0.0f, 0.0f)),
        makeCaster(glm::vec3(1.5f, 0.0f, 0.0f)),
    };

    CHECK_MSG(signatureOf(none, light) != signatureOf(one, light),
              "the first caster to arrive must dirty the pass");
    CHECK_MSG(signatureOf(one, light) != signatureOf(two, light),
              "and so must the second");

    // A slot that has just been vacated must clear, or it keeps casting the
    // shadow of a light that has stopped casting.
    CHECK_MSG(signatureOf(two, light) != signatureOf(none, light),
              "an emptied pass must record its clear");
}

static void testTwoDifferentShapesTradingPlacesChangesTheSignature() {
    // Two casters with DIFFERENT meshes, each moved to where the other was.
    // Every world matrix in the scene is still one of the two it was before, so
    // a signature that looked only at the set of transforms would call this
    // unchanged - and it is not, because the shapes standing on them swapped.
    //
    // Note what is deliberately NOT claimed here: two IDENTICAL casters trading
    // places do produce the same shadow map, and re-recording that pass is
    // waste rather than error. The requirement is only that a visible
    // difference dirties the pass, never the reverse.
    const glm::mat4 light = lightMatrix(glm::vec3(0.0f, 0.0f, 10.0f));

    std::vector<RenderSystem::ShadowCaster> before{
        makeCaster(glm::vec3(-1.0f, 0.0f, 0.0f), 1),
        makeCaster(glm::vec3(1.0f, 0.0f, 0.0f), 2),
    };
    std::vector<RenderSystem::ShadowCaster> after{
        makeCaster(glm::vec3(1.0f, 0.0f, 0.0f), 1),
        makeCaster(glm::vec3(-1.0f, 0.0f, 0.0f), 2),
    };

    CHECK_MSG(signatureOf(before, light) != signatureOf(after, light),
              "two different shapes trading places is a different picture");
}

static void testTheSkinningInputsAreInTheSignature() {
    // An animating character moves nothing the caster gather can see - same
    // entity, same transform, same bounds - while its shadow changes every
    // frame. The joint palette arrives as the seed and the per-entity offsets
    // sit on the caster.
    const glm::mat4 light = lightMatrix(glm::vec3(0.0f, 0.0f, 10.0f));
    std::vector<RenderSystem::ShadowCaster> casters{ makeCaster(glm::vec3(0.0f, 0.0f, 0.0f)) };

    const uint64_t plain = signatureOf(casters, light, 1ull);
    CHECK_MSG(plain != signatureOf(casters, light, 2ull),
              "a changed joint palette must dirty every pass that could show it");

    casters[0].skinJointCount = 12;
    CHECK_MSG(plain != signatureOf(casters, light, 1ull),
              "and so must a change to which joints an entity uses");

    casters[0].skinJointCount = 0;
    casters[0].skinPaletteBase = 64;
    CHECK_MSG(plain != signatureOf(casters, light, 1ull),
              "or to where in the palette it reads from");
}

static void testTheSameInputsGiveTheSameSignature() {
    // The half that makes the cache hit at all. Two identically built frames
    // must agree, or nothing is ever skipped and the whole thing is cost with
    // no benefit.
    const glm::mat4 light = lightMatrix(glm::vec3(2.0f, 3.0f, 10.0f));
    std::vector<RenderSystem::ShadowCaster> a{
        makeCaster(glm::vec3(0.0f, 0.0f, 0.0f), 3),
        makeCaster(glm::vec3(1.0f, 0.5f, -2.0f), 4),
    };
    std::vector<RenderSystem::ShadowCaster> b = a;

    CHECK_MSG(signatureOf(a, light) == signatureOf(b, light),
              "an unchanged frame must produce an unchanged signature");
}

// --- what a cut-out caster puts into the signature --------------------------

static void testTheCutoutInputsAreInTheSignature() {
    // A leaf whose cutoff or whose texture changes casts a different SHAPE, and
    // the gather sees nothing move: same entity, same transform, same bounds,
    // same mesh. Miss any of these three and up to eighteen cached passes keep
    // the silhouette the material used to have - and it looks entirely
    // plausible, which is the failure this whole suite exists for.
    const glm::mat4 light = lightMatrix(glm::vec3(0.0f, 0.0f, 10.0f));
    std::vector<RenderSystem::ShadowCaster> casters{ makeCaster(glm::vec3(0.0f, 0.0f, 0.0f)) };
    casters[0].alphaCutoff = 0.5f;
    const uint64_t base = signatureOf(casters, light);

    casters[0].alphaCutoff = 0.6f;
    CHECK_MSG(base != signatureOf(casters, light),
              "dragging the cutoff must dirty the passes that show it");

    casters[0].alphaCutoff = 0.5f;
    casters[0].baseAlpha = 0.9f;
    CHECK_MSG(base != signatureOf(casters, light),
              "and so must the material's alpha factor, which the test multiplies by");

    casters[0].baseAlpha = 1.0f;
    // A reloaded texture is the case the mesh path already learned: the id
    // survives and the contents do not, so the id is not a statement about the
    // pixels. TextureRegistry hands back a set it has not handed back at this
    // texture generation. NOT "never before": dropped sets now go back to the
    // pool and the driver may reuse a handle, which is why the renderer mixes
    // the generation into the seed every pass signature starts from.
    casters[0].materialSet = fakeDescriptorSet(0x1234);
    CHECK_MSG(base != signatureOf(casters, light),
              "a reloaded texture is a new descriptor set, and a new silhouette");

    // And the fourth way: the texture stands still and the COORDINATES move.
    // A flipbooked or scrolled cut-out has its holes somewhere else on every
    // frame, and everything the gather looks at is unchanged.
    casters[0] = makeCaster(glm::vec3(0.0f, 0.0f, 0.0f));
    casters[0].alphaCutoff = 0.5f;
    CHECK_MSG(base == signatureOf(casters, light),
              "putting every input back must give the baseline back");

    casters[0].uvTransform = MakeUvTransform(glm::vec2(1.0f, 1.0f), 0.0f,
                                             glm::vec2(0.5f, 0.0f));
    CHECK_MSG(base != signatureOf(casters, light),
              "scrolling the cut moves the holes, so the cached pass must record again");
}

static void testACutoutTheLightCannotSeeChangesNothing() {
    // The same control as the far crate above, for the fields this commit adds.
    // The cut-out mixes have to live INSIDE the frustum cull; hoisted above it,
    // every lamp in the level would re-record whenever anyone edited a material
    // on the far side of the map, the cache would quietly stop saving anything,
    // and no test would fail.
    const glm::mat4 light = lightMatrix(glm::vec3(0.0f, 0.0f, 10.0f));
    std::vector<RenderSystem::ShadowCaster> before{
        makeCaster(glm::vec3(0.0f, 0.0f, 0.0f)),
        makeCaster(glm::vec3(400.0f, 0.0f, 0.0f), 9),
    };
    before[1].alphaCutoff = 0.5f;
    std::vector<RenderSystem::ShadowCaster> after = before;
    after[1].alphaCutoff = 0.9f;
    after[1].materialSet = fakeDescriptorSet(0x4321);

    CHECK_MSG(signatureOf(before, light) == signatureOf(after, light),
              "a leaf recut at the far end of the level must leave this lamp alone");

    // The control on the control: worthless unless the far caster really was
    // outside the frustum and the near one really was inside it.
    const Frustum frustum = Frustum::FromMatrix(light);
    CHECK_MSG(frustum.IntersectsAABB(before[0].worldMin, before[0].worldMax),
              "the near caster must be visible for this test to mean anything");
    CHECK_MSG(!frustum.IntersectsAABB(before[1].worldMin, before[1].worldMax),
              "and the far one must not be");
}

// --- what each kind of material asks of the depth pass ----------------------

static void testWhatEachKindOfMaterialCastsIntoTheDepthPass() {
    // The decision itself, which GatherShadowCasters cannot be tested through:
    // that walk needs a MeshRegistry holding real GPU buffers, and there is no
    // device here. The policy is split out so it does not need one.
    MaterialComponent opaque;
    const RenderSystem::ShadowAlpha solid = RenderSystem::ShadowAlphaFor(&opaque);
    CHECK_MSG(solid.casts, "an ordinary material casts");
    CHECK_MSG(solid.cutoff == 0.0f,
              "and casts solid, down the pipeline it has always used");

    MaterialComponent leaf;
    leaf.alphaCutoff = 0.5f;
    leaf.albedoColor.a = 0.8f;
    const RenderSystem::ShadowAlpha cut = RenderSystem::ShadowAlphaFor(&leaf);
    CHECK_MSG(cut.casts && cut.cutoff == 0.5f, "a cutout material casts, cut to its own cutoff");
    CHECK_NEAR(cut.baseAlpha, 0.8f);

    MaterialComponent pane;
    pane.transparent = true;
    CHECK_MSG(!RenderSystem::ShadowAlphaFor(&pane).casts,
              "a blended surface writes no depth for the camera and casts none for the light");

    // Both flags together is documented as meaning something - blend what
    // survives the cut - so the cut is what casts. Reading `transparent` first
    // would overrule an authored cutoff and this is the assertion that says so.
    MaterialComponent both;
    both.transparent = true;
    both.alphaCutoff = 0.25f;
    const RenderSystem::ShadowAlpha survives = RenderSystem::ShadowAlphaFor(&both);
    CHECK_MSG(survives.casts && survives.cutoff == 0.25f,
              "a cutoff decides even on a material that is also blended");

    // An entity with no material at all still casts: every procedural primitive
    // in the engine goes through here.
    CHECK_MSG(RenderSystem::ShadowAlphaFor(nullptr).casts, "no material means an opaque caster");
}

static void testAnOpaqueMaterialsAlphaNeverReachesTheDepthPass() {
    // The other half of the conditional mix above, and the reason the signature
    // can hash baseAlpha unconditionally without dirtying eighteen passes every
    // time somebody nudges an opaque material's alpha: that number never gets
    // onto the caster in the first place. An opaque surface occludes whatever
    // its albedo alpha says, so a shadow that re-recorded for it would be
    // re-recording for an image that cannot change.
    MaterialComponent opaque;
    opaque.albedoColor.a = 0.25f;
    CHECK_MSG(RenderSystem::ShadowAlphaFor(&opaque).baseAlpha == 1.0f,
              "an opaque caster's alpha factor is normalised, not carried");
}

static void testTwoSurfacesOfOneMeshDoNotShareASignature() {
    // A caster is a RUN OF INDICES now, not always a whole mesh, so that a
    // cut-out surface can be cut against its own texture rather than the first
    // surface's. That makes firstIndex part of what the pass draws, and
    // anything the pass draws has to be in the signature or the cache serves a
    // shadow map recorded before it changed.
    //
    // The two casters carry the SAME index count on purpose. A mesh of equal
    // quads is the ordinary case, and makeCaster leaves indexCount at zero, so
    // a version of this test that varied only the count would hash 0 against 0
    // and pass green with firstIndex left out of the signature entirely.
    const glm::mat4 lightViewProj = lightMatrix(glm::vec3(0.0f, 0.0f, 10.0f));
    const Frustum frustum = Frustum::FromMatrix(lightViewProj);

    RenderSystem::ShadowCaster leaf = makeCaster(glm::vec3(0.0f));
    leaf.indexCount = 36;
    leaf.firstIndex = 0;

    RenderSystem::ShadowCaster panel = makeCaster(glm::vec3(0.0f));
    panel.indexCount = 36;
    panel.firstIndex = 36;

    const uint64_t first = RenderSystem::ShadowPassSignature({ leaf }, lightViewProj, frustum, 0);
    const uint64_t second = RenderSystem::ShadowPassSignature({ panel }, lightViewProj, frustum, 0);

    CHECK_MSG(first != second,
              "two runs of the same length at different offsets are different draws");

    // And the same offset really is the same signature, or the cache would
    // never hit and the whole thing would be a slow no-op.
    RenderSystem::ShadowCaster again = makeCaster(glm::vec3(0.0f));
    again.indexCount = 36;
    again.firstIndex = 36;
    CHECK_MSG(RenderSystem::ShadowPassSignature({ again }, lightViewProj, frustum, 0) == second,
              "the same run hashes the same, or nothing is ever cached");
}

static void runTests() {
    testTwoSurfacesOfOneMeshDoNotShareASignature();
    testAPassIsAlwaysRecordedTheFirstTime();
    testAChangedSignatureRecordsAgain();
    testPassesDoNotShareState();
    testInvalidateForgetsEverything();
    testAnOutOfRangePassIsAlwaysRecorded();

    testMovingAVisibleCasterChangesTheSignature();
    testMovingACasterTheLightCannotSeeChangesNothing();
    testMovingTheLightChangesTheSignature();
    testAppearingAndDisappearingCastersChangeTheSignature();
    testTwoDifferentShapesTradingPlacesChangesTheSignature();
    testTheSkinningInputsAreInTheSignature();
    testTheCutoutInputsAreInTheSignature();
    testACutoutTheLightCannotSeeChangesNothing();
    testTheSameInputsGiveTheSameSignature();

    testWhatEachKindOfMaterialCastsIntoTheDepthPass();
    testAnOpaqueMaterialsAlphaNeverReachesTheDepthPass();
}

TEST_MAIN("test_shadowcache", 70)
