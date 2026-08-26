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

void runTests() {
    testNoKeysMeansNoReorderAtAll();
    testAHigherKeyIsSubmittedLater();
    testEqualKeysKeepTheOrderTheyArrivedIn();
    testNegativeKeysSortBehindZero();
    testAnEmptyListIsNotAReorder();
}

} // namespace

TEST_MAIN("test_draworder", 11)
