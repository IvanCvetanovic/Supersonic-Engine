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
    // Null on purpose: the mesh contributes its buffer handle and index count
    // when there is one, and there is no device here to make one with. Every
    // other input is exercised.
    caster.mesh = nullptr;
    return caster;
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

static void runTests() {
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
    testTheSameInputsGiveTheSameSignature();
}

TEST_MAIN("test_shadowcache", 30)
