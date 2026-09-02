// What a pass costs, decided before any of it is recorded.
//
// This engine's own plan says "assert counts, never milliseconds", against a
// suite count that has never asserted on any cost quantity at all. The reason
// was structural rather than an oversight: every number the renderer produces
// came out of a five-hundred-line function needing a command buffer, a device
// and two registries, so the only way to read one was to launch the editor and
// look at a panel. "One draw call per drawable" was false for a year and
// nothing could have said so.
//
// PlanPass is the decision pulled out of that function - which draws become one
// call, and what has to be bound between them. It is pure, so the properties
// below are asserted rather than argued:
//
//   a run of identical draws is ONE call, which is the whole of what batching
//   buys and the number instancing moved;
//   a batch closes on every change that has to be bound, and on nothing else,
//   because a batch that closes too eagerly costs calls and one that closes too
//   late draws the wrong thing with the wrong state;
//   the order is never re-sorted, because the opaque pass carries an authored
//   sortKey and the blended pass carries a back-to-front depth order that is
//   the difference between a correct frame and a wrong one;
//   a full instance buffer drops draws and says how many, which is a path no
//   fixture can reach at sixty-five thousand drawables and every test here can
//   reach with a capacity of three.

#include "TestHarness.hpp"

#include "core/RenderSystem.hpp"
#include "renderer/MeshRegistry.hpp"

#include <string>
#include <vector>

using namespace Supersonic;

namespace {

using PassDraw = RenderSystem::PassDraw;
using PassBatch = RenderSystem::PassBatch;
using PassPlan = RenderSystem::PassPlan;

PassDraw draw(uint64_t mesh, uint64_t material, uint32_t firstIndex = 0,
              uint32_t indexCount = 36) {
    return PassDraw{ mesh, firstIndex, indexCount, material };
}

// Every draw in one call, so a test names only what it is varying.
constexpr uint32_t kPlenty = 1024;

PassPlan plan(const std::vector<PassDraw>& draws, uint32_t firstInstance = 0,
              uint32_t capacity = kPlenty) {
    return RenderSystem::PlanPass(draws, firstInstance, capacity);
}

std::string describe(const PassPlan& p) {
    std::string out = std::to_string(p.DrawCalls()) + " call(s):";
    for (const PassBatch& batch : p.batches) {
        out += " [mesh " + std::to_string(batch.meshKey) + " mat " +
               std::to_string(batch.materialKey) + " x" + std::to_string(batch.instanceCount) +
               " @" + std::to_string(batch.firstInstance) + "]";
    }
    return out;
}

} // namespace

// --- the run-length rule ---------------------------------------------------

static void testNothingIsNoCallsAndNoBinds() {
    const PassPlan p = plan({});
    CHECK_EQ(p.DrawCalls(), 0u);
    CHECK_EQ(p.meshBinds, 0u);
    CHECK_EQ(p.materialBinds, 0u);
    CHECK_EQ(p.instances, 0u);
    CHECK_MSG(p.dropped == 0u, "an empty pass has dropped nothing, not everything");
}

static void testTenThousandIdenticalDrawsAreOneCallAndTwoBinds() {
    // The property instancing exists for, and the one the draw-call counter was
    // added to make observable. Ten thousand cubes sharing a mesh and a material
    // are one call, one mesh bind and one material bind - and before instancing
    // they were ten thousand calls, which is the measurement that moved.
    std::vector<PassDraw> draws;
    for (int i = 0; i < 10000; ++i) draws.push_back(draw(7, 3));

    // The real buffer holds 65,536. Named here rather than taken from kPlenty,
    // whose whole job is to be bigger than the handful of draws every other
    // case uses - the first version of this test took it and silently measured
    // the capacity limit instead of the batching.
    const PassPlan p = plan(draws, 0, 65536);
    CHECK_EQ(p.DrawCalls(), 1u);
    CHECK_EQ(p.meshBinds, 1u);
    CHECK_EQ(p.materialBinds, 1u);
    CHECK_EQ(p.instances, 10000u);
    CHECK_EQ(p.batches[0].instanceCount, 10000u);
    CHECK_EQ(p.batches[0].firstInstance, 0u);
    CHECK_MSG(p.batches[0].bindsMesh && p.batches[0].bindsMaterial,
              "the first batch of a pass binds both, because nothing was bound before it");
}

static void testAMeshChangeClosesTheBatch() {
    const PassPlan p = plan({ draw(1, 5), draw(1, 5), draw(2, 5), draw(2, 5) });
    CHECK_MSG(p.DrawCalls() == 2u, describe(p));
    CHECK_EQ(p.meshBinds, 2u);
    CHECK_MSG(p.materialBinds == 1u, "the material never changed, so it is bound once");
    CHECK_EQ(p.batches[0].instanceCount, 2u);
    CHECK_EQ(p.batches[1].instanceCount, 2u);
    CHECK_MSG(p.batches[1].bindsMesh, "the second batch rebinds the mesh");
    CHECK_MSG(!p.batches[1].bindsMaterial, "and does not rebind the material");
}

static void testAMaterialChangeClosesTheBatch() {
    const PassPlan p = plan({ draw(1, 5), draw(1, 6), draw(1, 6) });
    CHECK_MSG(p.DrawCalls() == 2u, describe(p));
    CHECK_MSG(p.meshBinds == 1u, "one mesh throughout");
    CHECK_EQ(p.materialBinds, 2u);
    CHECK_MSG(!p.batches[1].bindsMesh && p.batches[1].bindsMaterial,
              "only what changed is rebound");
}

static void testAnIndexRangeChangeClosesTheBatchWithNoBindAtAll() {
    // The multi-surface case: one mesh, one material, two ranges. Nothing is
    // rebound and it is still two calls, because the range is what the call
    // NAMES rather than something bound before it. A batcher keyed only on
    // binds would merge these and draw one surface twice.
    const PassPlan p = plan({ draw(1, 5, 0, 12), draw(1, 5, 12, 24) });
    CHECK_MSG(p.DrawCalls() == 2u, describe(p));
    CHECK_EQ(p.meshBinds, 1u);
    CHECK_EQ(p.materialBinds, 1u);
    CHECK_MSG(!p.batches[1].bindsMesh && !p.batches[1].bindsMaterial,
              "a range change costs a call and no bind");
    CHECK_EQ(p.batches[1].firstIndex, 12u);
    CHECK_EQ(p.batches[1].indexCount, 24u);
}

static void testAlternatingDrawsNeverBatchAndSayHowMuchThatCost() {
    // The pathological order, and the reason the opaque sort is by material
    // rather than by entity: interleaved materials cost a call and a bind each.
    std::vector<PassDraw> draws;
    for (int i = 0; i < 8; ++i) draws.push_back(draw(1, i % 2 == 0 ? 5 : 6));

    const PassPlan p = plan(draws);
    CHECK_EQ(p.DrawCalls(), 8u);
    CHECK_EQ(p.materialBinds, 8u);
    CHECK_MSG(p.meshBinds == 1u, "the mesh is the same one throughout");
    CHECK_EQ(p.instances, 8u);
}

static void testTheOrderIsNeverRearranged() {
    // CONSECUTIVE ONLY. Sorting these two draws together would make one call
    // out of three - and in the blended pass, where the order IS the depth
    // order, would composite the middle draw on the wrong side of them.
    const PassPlan p = plan({ draw(1, 5), draw(2, 6), draw(1, 5) });
    CHECK_MSG(p.DrawCalls() == 3u, describe(p));
    CHECK_EQ(p.batches[0].meshKey, 1u);
    CHECK_EQ(p.batches[1].meshKey, 2u);
    CHECK_EQ(p.batches[2].meshKey, 1u);
    CHECK_MSG(p.batches[2].bindsMesh,
              "coming back to a mesh binds it again; nothing is remembered but the last one");
}

// --- what the pass starts with ---------------------------------------------

static void testADrawThatIsAlreadyBoundBindsNothing() {
    // What lets a pass say it has bound its own mesh outside the loop, which
    // is exactly what the particle pass does.
    const PassPlan p = RenderSystem::PlanPass({ draw(9, 4), draw(9, 4) }, 0, kPlenty, 9, 4);
    CHECK_EQ(p.DrawCalls(), 1u);
    CHECK_MSG(p.meshBinds == 0u && p.materialBinds == 0u, "both were already bound");
    CHECK_MSG(!p.batches[0].bindsMesh && !p.batches[0].bindsMaterial,
              "and the batch says so, so the recorder does not bind them again");
}

static void testAPassWithNoMaterialSetBindsNoMaterial() {
    // Zero is what a null descriptor set meant to the loops this replaces: no
    // set to bind, and no batch closed for the lack of one.
    const PassPlan p = plan({ draw(1, 0), draw(1, 0), draw(1, 0) });
    CHECK_MSG(p.DrawCalls() == 1u, describe(p));
    CHECK_EQ(p.materialBinds, 0u);
    CHECK_MSG(p.meshBinds == 1u, "the mesh still binds");
}

// --- the instance buffer ---------------------------------------------------

static void testAPassStartsWhereTheOneBeforeItStopped() {
    // Three passes share one instance buffer, so a pass cannot number its own
    // records from zero - the second pass's first instance is the first pass's
    // count, and a firstInstance of zero here would make the blended pass draw
    // the opaque pass's transforms.
    const PassPlan p = plan({ draw(1, 5), draw(1, 5) }, 40);
    CHECK_EQ(p.batches[0].firstInstance, 40u);
    CHECK_EQ(p.batches[0].instanceCount, 2u);
    CHECK_EQ(p.instances, 2u);
}

static void testAFullBufferDropsTheRestAndCountsThem() {
    // Unreachable in a fixture: the buffer holds sixty-five thousand records
    // and the engine misses sixty hertz for other reasons long before that. A
    // capacity of three makes it a unit test.
    const PassPlan p = plan({ draw(1, 5), draw(1, 5), draw(1, 5), draw(1, 5), draw(1, 5) }, 0, 3);
    CHECK_EQ(p.instances, 3u);
    CHECK_EQ(p.dropped, 2u);
    CHECK_MSG(p.DrawCalls() == 1u, describe(p));
    CHECK_MSG(p.batches[0].instanceCount == 3u,
              "the batch names what was recorded, never what was asked for");
}

static void testAPassThatStartsFullDrawsNothingAndDropsEverything() {
    const PassPlan p = plan({ draw(1, 5), draw(2, 6) }, 3, 3);
    CHECK_EQ(p.instances, 0u);
    CHECK_EQ(p.dropped, 2u);
    CHECK_EQ(p.DrawCalls(), 0u);
    CHECK_MSG(p.meshBinds == 2u && p.materialBinds == 2u,
              "the binds still happened: the loop binds before it records, and a count "
              "of what the frame cost must not report a bind it made as one it did not");
}

static void testADropDoesNotWeldTheDrawsAroundIt() {
    // The subtle one. With the middle draw dropped, the two survivors are no
    // longer adjacent in the buffer - and a batcher that merged them would
    // issue one call for three instances, two of which belong to somebody else.
    const PassPlan p = plan({ draw(1, 5), draw(2, 6), draw(1, 5) }, 0, 2);
    CHECK_EQ(p.instances, 2u);
    CHECK_EQ(p.dropped, 1u);
    CHECK_MSG(p.DrawCalls() == 2u, describe(p));
    CHECK_EQ(p.batches[0].firstInstance, 0u);
    CHECK_EQ(p.batches[1].firstInstance, 1u);

    uint32_t total = 0;
    for (const PassBatch& batch : p.batches) total += batch.instanceCount;
    CHECK_MSG(total == p.instances,
              "every record a batch names was actually written, and every record written is "
              "named by exactly one batch");
}

// --- degenerate ranges ------------------------------------------------------

static void testARangeWithNoIndicesDrawsNothingAndStillBinds() {
    // Unreachable today - BuildSections keeps only non-empty runs - and the
    // alternative if it ever happens is a drawIndexed of zero indices sitting
    // inside a batch whose other members are real.
    const PassPlan p = plan({ draw(1, 5, 0, 0), draw(1, 5) });
    CHECK_MSG(p.DrawCalls() == 1u, describe(p));
    CHECK_EQ(p.instances, 1u);
    CHECK_MSG(p.meshBinds == 1u, "the bind happened before the range was looked at");
    CHECK_MSG(p.batches[0].indexCount == 36u, "the batch is the draw that had indices");
}

// --- the invariants every plan holds ---------------------------------------

static void testEveryPlanIsContiguousAndAccountedFor() {
    // Run over a deliberately awkward sequence: repeats, changes of each kind,
    // a range change, an empty range and a drop.
    const std::vector<PassDraw> draws = {
        draw(1, 5), draw(1, 5), draw(1, 6), draw(2, 6), draw(2, 6, 12, 24),
        draw(2, 6, 12, 24), draw(1, 5, 0, 0), draw(1, 5), draw(3, 7), draw(3, 7),
    };

    for (uint32_t capacity = 0; capacity <= 12; ++capacity) {
        const PassPlan p = plan(draws, 0, capacity);

        uint32_t expected = 0;
        uint32_t total = 0;
        bool contiguous = true;
        for (const PassBatch& batch : p.batches) {
            if (batch.firstInstance != expected) contiguous = false;
            if (batch.instanceCount == 0) contiguous = false;
            expected = batch.firstInstance + batch.instanceCount;
            total += batch.instanceCount;
        }

        CHECK_MSG(contiguous, "batches name one contiguous run each, in order, at capacity " +
                                  std::to_string(capacity));
        CHECK_MSG(total == p.instances, "every record belongs to exactly one batch at capacity " +
                                            std::to_string(capacity));
        CHECK_MSG(p.instances <= capacity, "never more records than the buffer holds");
        CHECK_MSG(p.instances + p.dropped == 9u,
                  "nine of the ten draws have indices, and each was either recorded or "
                  "dropped, at capacity " + std::to_string(capacity));
    }
}

static void testMoreCapacityIsNeverFewerRecords() {
    const std::vector<PassDraw> draws = {
        draw(1, 5), draw(2, 5), draw(2, 5), draw(3, 9), draw(3, 9), draw(3, 9),
    };

    uint32_t previous = 0;
    for (uint32_t capacity = 0; capacity <= 8; ++capacity) {
        const PassPlan p = plan(draws, 0, capacity);
        CHECK_MSG(p.instances >= previous, "a bigger buffer never draws less");
        previous = p.instances;
    }
    CHECK_MSG(previous == 6u, "and at the top every draw is recorded");
}

int main() {
    testNothingIsNoCallsAndNoBinds();
    testTenThousandIdenticalDrawsAreOneCallAndTwoBinds();
    testAMeshChangeClosesTheBatch();
    testAMaterialChangeClosesTheBatch();
    testAnIndexRangeChangeClosesTheBatchWithNoBindAtAll();
    testAlternatingDrawsNeverBatchAndSayHowMuchThatCost();
    testTheOrderIsNeverRearranged();

    testADrawThatIsAlreadyBoundBindsNothing();
    testAPassWithNoMaterialSetBindsNoMaterial();

    testAPassStartsWhereTheOneBeforeItStopped();
    testAFullBufferDropsTheRestAndCountsThem();
    testAPassThatStartsFullDrawsNothingAndDropsEverything();
    testADropDoesNotWeldTheDrawsAroundIt();

    testARangeWithNoIndicesDrawsNothingAndStillBinds();

    testEveryPlanIsContiguousAndAccountedFor();
    testMoreCapacityIsNeverFewerRecords();

    return test::summary("test_renderplan", 70);
}
