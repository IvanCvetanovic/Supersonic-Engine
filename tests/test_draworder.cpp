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
}

} // namespace

TEST_MAIN("test_draworder", 11)
