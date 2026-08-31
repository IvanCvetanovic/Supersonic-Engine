// Explicit draw order for surfaces the depth buffer cannot separate.
//
// The renderer submitted opaque geometry in whatever order the registry handed
// it over and let depth decide the rest. That is correct for solids and decides
// NOTHING for coplanar quads - which is what a 2D game is made of, and which
// Godot orders by child index.
//
// Two properties matter and they pull against each other. A scene that sets no
// key must record in exactly the order it always did, or every existing scene
// is subtly re-ordered by a feature it does not use. And a scene that does set
// keys must be re-ordered by them. The first is asserted here rather than
// argued, because "a stable sort of equal keys is a no-op" is true of this
// implementation and is not a promise worth resting a hundred scenes on.
//
// Device-free by construction: OpaqueDraw carries buffer HANDLES by value, so a
// test can build one with no Vulkan device, no registry and no mesh registry.

#include "TestHarness.hpp"
#include "core/RenderSystem.hpp"
#include "renderer/MeshRegistry.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace Supersonic;

namespace {

RenderSystem::OpaqueDraw draw(uint32_t meshID, int32_t sortKey) {
    RenderSystem::OpaqueDraw d;
    d.meshID = meshID;
    d.sortKey = sortKey;
    return d;
}

std::vector<uint32_t> meshOrder(const std::vector<RenderSystem::OpaqueDraw>& draws) {
    std::vector<uint32_t> order;
    order.reserve(draws.size());
    for (const auto& d : draws) order.push_back(d.meshID);
    return order;
}

void testNoKeysMeansNoReorderAtAll() {
    // The property that let this land without re-checking every scene.
    std::vector<RenderSystem::OpaqueDraw> draws;
    for (uint32_t i = 0; i < 64; ++i) draws.push_back(draw(i, 0));

    const std::vector<uint32_t> before = meshOrder(draws);
    const bool sorted = RenderSystem::SortOpaqueDraws(draws);

    CHECK_MSG(!sorted, "with every key equal the sort must report that it did nothing");
    CHECK_MSG(meshOrder(draws) == before,
              "and must leave the list element for element as it was");
}

void testAHigherKeyIsSubmittedLater() {
    // Ascending, because the depth compare is lessOrEqual: at equal depth the
    // LATER fragment replaces the earlier one, so the higher key ends on top.
    // Getting this backwards draws the selection ring over the body, which
    // looks deliberate and is not.
    std::vector<RenderSystem::OpaqueDraw> draws{draw(10, 5), draw(20, 1), draw(30, 3)};

    CHECK_MSG(RenderSystem::SortOpaqueDraws(draws), "keys present means a reorder");

    const std::vector<uint32_t> order = meshOrder(draws);
    CHECK_EQ(order.size(), size_t{3});
    CHECK_MSG(order[0] == 20 && order[1] == 30 && order[2] == 10,
              "draws must come out in ascending key order");
}

void testEqualKeysKeepTheOrderTheyArrivedIn() {
    // Stability is what preserves run-length grouping by mesh and material.
    // The transparent pass gave that up for a distance sort and had to bind its
    // buffers unconditionally on every draw as a result; this must not.
    std::vector<RenderSystem::OpaqueDraw> draws{
        draw(1, 2), draw(2, 2), draw(3, 1), draw(4, 1), draw(5, 2),
    };

    CHECK(RenderSystem::SortOpaqueDraws(draws));

    const std::vector<uint32_t> order = meshOrder(draws);
    CHECK_MSG(order[0] == 3 && order[1] == 4, "the key-1 pair keeps its arrival order");
    CHECK_MSG(order[2] == 1 && order[3] == 2 && order[4] == 5,
              "and so does the key-2 run");
}

void testNegativeKeysSortBehindZero() {
    // Zero is "no opinion", not "the bottom". A background authored at -1 has
    // to land behind everything that never said anything.
    std::vector<RenderSystem::OpaqueDraw> draws{draw(1, 0), draw(2, -1), draw(3, 1)};

    CHECK(RenderSystem::SortOpaqueDraws(draws));

    const std::vector<uint32_t> order = meshOrder(draws);
    CHECK_MSG(order[0] == 2 && order[1] == 1 && order[2] == 3,
              "negative keys must sort behind the ones that hold no opinion");
}

void testAnEmptyListIsNotAReorder() {
    std::vector<RenderSystem::OpaqueDraw> draws;
    CHECK_MSG(!RenderSystem::SortOpaqueDraws(draws), "nothing to sort is not a sort");
}

// --- the blended pass ------------------------------------------------------
//
// The opaque pass above orders by an authored key. The blended one orders by
// DEPTH, and got it wrong in a way only an orthographic camera shows.

RenderSystem::TransparentDraw blended(float viewDepth, int32_t sortKey, uint32_t gathered) {
    RenderSystem::TransparentDraw draw;
    draw.viewDepth = viewDepth;
    draw.sortKey = sortKey;
    draw.gathered = gathered;
    return draw;
}

void testBlendedDrawsGoBackToFront() {
    std::vector<RenderSystem::TransparentDraw> draws{
        blended(2.0f, 0, 0), blended(9.0f, 0, 1), blended(5.0f, 0, 2)};

    RenderSystem::SortTransparentDraws(draws);

    CHECK_NEAR(draws[0].viewDepth, 9.0f);
    CHECK_NEAR(draws[1].viewDepth, 5.0f);
    CHECK_MSG(draws[2].viewDepth == 2.0f,
              "the nearest surface is drawn last, so it composites over the rest");
}

void testSomethingBehindTheCameraSortsBehind() {
    // What the squared distance lost. A square has no sign, so a quad four
    // units BEHIND the view sorted as though it were four in front - and under
    // a perspective camera the culler hid that, while an orthographic box does
    // not.
    std::vector<RenderSystem::TransparentDraw> draws{
        blended(4.0f, 0, 0), blended(-4.0f, 0, 1)};

    RenderSystem::SortTransparentDraws(draws);

    CHECK_MSG(draws[0].viewDepth == 4.0f, "the one in front of the camera is farther along the view");
    CHECK_MSG(draws[1].viewDepth == -4.0f, "and the one behind it sorts nearest, not equal to it");
}

void testCoplanarQuadsAreOrderedByTheirSortKey() {
    // THE 2D CASE, and the one the blended pass could not express. A lane of
    // quads at one depth has nothing to sort by, and the pass ignored sortKey
    // entirely - so the layering a 2D game is built out of was decided by
    // whatever std::sort did with equal elements.
    std::vector<RenderSystem::TransparentDraw> draws{
        blended(3.0f, 2, 0), blended(3.0f, 0, 1), blended(3.0f, 1, 2)};

    RenderSystem::SortTransparentDraws(draws);

    CHECK_EQ(draws[0].sortKey, 0);
    CHECK_EQ(draws[1].sortKey, 1);
    CHECK_MSG(draws[2].sortKey == 2, "a higher key draws later, which is to say on top");
}

void testDepthStillBeatsTheSortKey() {
    // The key breaks ties; it does not overrule depth. A background quad with a
    // high key must not jump in front of something genuinely nearer, or the
    // key would silently become a second, worse depth buffer.
    std::vector<RenderSystem::TransparentDraw> draws{
        blended(1.0f, 0, 0), blended(8.0f, 99, 1)};

    RenderSystem::SortTransparentDraws(draws);

    CHECK_MSG(draws[0].viewDepth == 8.0f, "the far one is still drawn first despite its key");
}

void testTwoDrawsAgreeingOnEverythingKeepGatherOrder() {
    // The order is TOTAL, so nothing is left to the standard library. Equal
    // depth and equal key is the ordinary case for a HUD built out of one
    // material, and introsort is free to permute equal elements - a flicker
    // with no cause in the scene.
    std::vector<RenderSystem::TransparentDraw> draws{
        blended(0.0f, 0, 0), blended(0.0f, 0, 1), blended(0.0f, 0, 2), blended(0.0f, 0, 3)};

    // Handed to the sort back to front, so a comparator that merely preserved
    // its input would not pass this.
    std::reverse(draws.begin(), draws.end());
    RenderSystem::SortTransparentDraws(draws);

    for (uint32_t i = 0; i < draws.size(); ++i) {
        CHECK_MSG(draws[i].gathered == i,
                  "draw " + std::to_string(i) + " is where it was gathered");
    }
}

// --- particles -------------------------------------------------------------
//
// The same order the blended pass needs, in the pass that needs it most. A
// particle has no authored sortKey and nothing can give it one, so there are
// two keys rather than three - and the tie is not a coincidence here but the
// ordinary case, because every particle a burst spawns leaves from the
// emitter's own position and their depths come out BIT-EQUAL.

RenderSystem::ParticleDraw puff(float viewDepth, uint32_t gathered) {
    RenderSystem::ParticleDraw draw;
    draw.viewDepth = viewDepth;
    draw.gathered = gathered;
    return draw;
}

void testParticlesGoBackToFront() {
    std::vector<RenderSystem::ParticleDraw> draws{puff(2.0f, 0), puff(9.0f, 1), puff(5.0f, 2)};

    RenderSystem::SortParticleDraws(draws);

    CHECK_NEAR(draws[0].viewDepth, 9.0f);
    CHECK_NEAR(draws[1].viewDepth, 5.0f);
    CHECK_MSG(draws[2].viewDepth == 2.0f,
              "the nearest particle is drawn last, so it composites over the rest");
}

void testAParticleBehindTheCameraSortsBehind() {
    // Signed depth, for the reason TransparentDraw gives. An emitter at the
    // camera throws particles both ways.
    std::vector<RenderSystem::ParticleDraw> draws{puff(4.0f, 0), puff(-4.0f, 1)};

    RenderSystem::SortParticleDraws(draws);

    CHECK_MSG(draws[0].viewDepth == 4.0f, "the one in front is farther along the view");
    CHECK_MSG(draws[1].viewDepth == -4.0f, "and the one behind sorts nearest, not equal to it");
}

void testABurstAtOneDepthKeepsGatherOrder() {
    // THE CASE THAT WAS BROKEN, and the reason this sort is now total. Twelve
    // particles spawned on one frame share a position exactly, so the old
    // single-float comparator handed twelve equal elements to std::sort - which
    // is permitted to order them however it likes, and is not required to do it
    // the same way twice. Two runs of one recording could differ, and the only
    // evidence would be the picture.
    std::vector<RenderSystem::ParticleDraw> draws;
    for (uint32_t i = 0; i < 12; ++i) draws.push_back(puff(3.0f, i));

    // Handed in backwards, so a comparator that merely preserved its input
    // would not pass this.
    std::reverse(draws.begin(), draws.end());
    RenderSystem::SortParticleDraws(draws);

    for (uint32_t i = 0; i < draws.size(); ++i) {
        CHECK_MSG(draws[i].gathered == i,
                  "particle " + std::to_string(i) + " is where it was gathered");
    }
}

void testTheParticleSortIsAPermutationToo() {
    // Size, not just order. A comparator that is not a strict weak ordering is
    // undefined behaviour rather than a wrong answer, and the shape that
    // usually takes is elements going missing.
    std::vector<RenderSystem::ParticleDraw> draws{
        puff(1.0f, 0), puff(1.0f, 1), puff(-2.0f, 2), puff(7.5f, 3), puff(1.0f, 4)};

    std::vector<uint32_t> before;
    for (const auto& d : draws) before.push_back(d.gathered);

    RenderSystem::SortParticleDraws(draws);

    std::vector<uint32_t> after;
    for (const auto& d : draws) after.push_back(d.gathered);

    CHECK_EQ(after.size(), before.size());
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    CHECK_MSG(before == after, "every particle survives the sort");
}

// There is deliberately no unit test here for the batching itself. What a
// batch costs is a count of vkCmdDrawIndexed calls against a device, and a
// test that restated the loop's own run-detection would be a copy of it rather
// than a check on it - the trap DrawCallsForMesh exists to avoid. It is
// verified where it can be: the draw-call counter reports 2 for a
// twenty-thousand-particle scene that reported 20,001 before, and the frame is
// pixel-identical.

// --- What a frame COSTS, in quantities ------------------------------------
//
// The engine measured itself only in milliseconds. A millisecond threshold is
// flaky across machines and gets deleted within a month; "one draw call per
// drawable" is a property, and it is one this engine does not actually have -
// which nothing could see, because the only counter was of ENTITIES.

void testAModelWithOneSurfaceCostsOneDrawCall() {
    GpuMesh plain;
    plain.sections.resize(1);
    CHECK_EQ(RenderSystem::DrawCallsForMesh(&plain), uint32_t{1});

    // A mesh with NO sections takes the same path, which is the reason
    // GpuMesh::sections is documented as always having at least one: the draw
    // loop has no special case, so neither does this.
    GpuMesh sectionless;
    CHECK_EQ(RenderSystem::DrawCallsForMesh(&sectionless), uint32_t{1});

    // And a drawable whose mesh has gone is still bound and pushed - it simply
    // submits nothing. One call's worth of loop, as the loop does.
    CHECK_EQ(RenderSystem::DrawCallsForMesh(nullptr), uint32_t{1});
}

void testAMultiSurfaceModelCostsOneCallPerSurface() {
    // THE GAP BETWEEN DRAWABLES AND DRAW CALLS, which is what the counter
    // exists to make visible. One entity, one `drawn`, four submissions.
    //
    // Not hypothetical: the demo scene draws eight drawables in ten calls, and
    // HUSK has forty-eight multi-material models out of fifty-eight.
    GpuMesh model;
    model.sections.resize(4);
    CHECK_EQ(RenderSystem::DrawCallsForMesh(&model), uint32_t{4});

    GpuMesh pair;
    pair.sections.resize(2);
    CHECK_EQ(RenderSystem::DrawCallsForMesh(&pair), uint32_t{2});
}

void testSortingIsAPermutationAndLosesNothing() {
    // The suite asserted ORDER and never SIZE. A sort that dropped a draw -
    // a partition that forgot its tail, a unique that should not have been
    // there - reorders the survivors perfectly and every existing case passes.
    std::vector<RenderSystem::OpaqueDraw> draws;
    for (uint32_t i = 0; i < 64; ++i) draws.push_back(draw(i, static_cast<int32_t>(i % 7)));

    std::vector<uint32_t> before = meshOrder(draws);
    RenderSystem::SortOpaqueDraws(draws);
    std::vector<uint32_t> after = meshOrder(draws);

    CHECK_EQ(after.size(), before.size());

    // The same multiset, not merely the same count. Sorting both copies turns
    // "is a permutation" into one comparison.
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    CHECK_MSG(before == after, "every draw that went in comes out, exactly once");
}

void testTheBlendedSortIsAPermutationToo() {
    // Same claim for the pass where losing one is least visible: a blended
    // draw that vanished leaves the scene behind it looking correct.
    std::vector<RenderSystem::TransparentDraw> draws;
    for (uint32_t i = 0; i < 32; ++i) {
        RenderSystem::TransparentDraw d;
        d.viewDepth = static_cast<float>((i * 7919u) % 100u);
        d.sortKey = static_cast<int32_t>(i % 5);
        d.gathered = i;
        draws.push_back(d);
    }

    std::vector<uint32_t> before;
    for (const auto& d : draws) before.push_back(d.gathered);

    RenderSystem::SortTransparentDraws(draws);

    std::vector<uint32_t> after;
    for (const auto& d : draws) after.push_back(d.gathered);

    CHECK_EQ(after.size(), before.size());
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    CHECK_MSG(before == after, "every blended draw survives the sort");
}

void testAnUntouchedStatsBlockIsAllZeroes() {
    // The class of bug the renderer documents having shipped: a counter nobody
    // initialised read zero for every frame the engine ever rendered. Cheap to
    // state, and it is the check a new field is most likely to fail.
    const RenderSystem::Stats stats;
    CHECK_EQ(stats.drawn, uint32_t{0});
    CHECK_EQ(stats.culled, uint32_t{0});
    CHECK_EQ(stats.transparentDrawn, uint32_t{0});
    CHECK_EQ(stats.drawCalls, uint32_t{0});
    CHECK_EQ(stats.particlesDrawn, uint32_t{0});
    CHECK_EQ(stats.shadowDrawn, uint32_t{0});
    CHECK_EQ(stats.shadowCulled, uint32_t{0});
    CHECK_EQ(stats.skinnedMatrices, uint32_t{0});
    CHECK_EQ(stats.shadowPassesSkipped, uint32_t{0});
}

void runTests() {
    testBlendedDrawsGoBackToFront();
    testSomethingBehindTheCameraSortsBehind();
    testCoplanarQuadsAreOrderedByTheirSortKey();
    testDepthStillBeatsTheSortKey();
    testTwoDrawsAgreeingOnEverythingKeepGatherOrder();
    testNoKeysMeansNoReorderAtAll();
    testAHigherKeyIsSubmittedLater();
    testEqualKeysKeepTheOrderTheyArrivedIn();
    testNegativeKeysSortBehindZero();
    testAnEmptyListIsNotAReorder();

    testParticlesGoBackToFront();
    testAParticleBehindTheCameraSortsBehind();
    testABurstAtOneDepthKeepsGatherOrder();
    testTheParticleSortIsAPermutationToo();

    testAModelWithOneSurfaceCostsOneDrawCall();
    testAMultiSurfaceModelCostsOneCallPerSurface();
    testSortingIsAPermutationAndLosesNothing();
    testTheBlendedSortIsAPermutationToo();
    testAnUntouchedStatsBlockIsAllZeroes();
}

} // namespace

TEST_MAIN("test_draworder", 61)
