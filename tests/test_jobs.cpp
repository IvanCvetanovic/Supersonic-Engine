// Regression tests for the job system.
//
// Everything ran on one thread before this. The failure modes of the fix are all
// silent: a fence that returns before the work is done produces a wrong result
// once in a hundred runs, an item skipped at the tail of a dispatch loses a
// vertex, and a leaked counter hangs the engine rather than crashing it.
//
// These run the pool for real rather than mocking it, and each concurrency check
// repeats enough times that a race has to be very lucky to hide.

#include "TestHarness.hpp"
#include "core/JobSystem.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace Supersonic;

static void testDispatchRunsInlineWithoutAPool() {
    // Every caller relies on this: no pool means the work still happens, so a
    // headless test or a packaged tool needs no special case.
    JobSystem::Shutdown();
    CHECK_MSG(!JobSystem::IsInitialized(), "no pool should be running here");

    std::vector<int> values(100, 0);
    JobSystem::Dispatch(100, 8, [&values](JobSystem::JobArgs args) {
        values[args.jobIndex] = 1;
    });

    CHECK_EQ(static_cast<size_t>(std::count(values.begin(), values.end(), 1)), size_t{100});
}

static void testEveryItemRunsExactlyOnce() {
    JobSystem::Initialize(4);
    CHECK(JobSystem::IsInitialized());
    CHECK_EQ(JobSystem::ThreadCount(), 4u);

    // A count that is not a multiple of the group size, which is where an
    // off-by-one at the tail hides.
    constexpr uint32_t kCount = 10'001;
    std::vector<std::atomic<int>> hits(kCount);
    for (auto& hit : hits) hit.store(0);

    JobSystem::Dispatch(kCount, 128, [&hits](JobSystem::JobArgs args) {
        hits[args.jobIndex].fetch_add(1);
    });
    JobSystem::Wait();

    size_t missing = 0;
    size_t duplicated = 0;
    for (const auto& hit : hits) {
        const int n = hit.load();
        if (n == 0) ++missing;
        if (n > 1) ++duplicated;
    }
    CHECK_MSG(missing == 0, "no item may be skipped");
    CHECK_MSG(duplicated == 0, "and none may run twice");
}

static void testWaitIsARealFence() {
    // Repeated, because a fence that is merely usually correct passes once.
    for (int attempt = 0; attempt < 20; ++attempt) {
        constexpr uint32_t kCount = 4096;
        std::vector<uint32_t> values(kCount, 0);

        JobSystem::Dispatch(kCount, 64, [&values](JobSystem::JobArgs args) {
            values[args.jobIndex] = args.jobIndex + 1;
        });
        JobSystem::Wait();

        CHECK_MSG(!JobSystem::IsBusy(), "Wait must leave nothing outstanding");

        // Read immediately after the fence with no further synchronisation: if
        // Wait returned early, some entry is still zero.
        const uint64_t sum = std::accumulate(values.begin(), values.end(), uint64_t{0});
        const uint64_t expected = uint64_t{kCount} * (kCount + 1) / 2;
        if (sum != expected) {
            CHECK_MSG(false, "Wait returned before every job had finished");
            break;
        }
    }
}

static void testGroupsCoverContiguousRanges() {
    constexpr uint32_t kCount = 1000;
    constexpr uint32_t kGroup = 100;
    std::vector<uint32_t> groupOf(kCount, 0xFFFFFFFFu);

    JobSystem::Dispatch(kCount, kGroup, [&groupOf](JobSystem::JobArgs args) {
        groupOf[args.jobIndex] = args.groupIndex;
    });
    JobSystem::Wait();

    // Item i must be in group i / kGroup, so per-group scratch space is a valid
    // thing for a caller to key on.
    for (uint32_t i = 0; i < kCount; ++i) {
        if (groupOf[i] != i / kGroup) {
            CHECK_MSG(false, "item " + std::to_string(i) + " landed in the wrong group");
            return;
        }
    }
    CHECK(true);
}

static void testWorkActuallySpreadsAcrossThreads() {
    // Not a timing test - it counts distinct thread ids, so it cannot fail
    // spuriously on a slow machine, only on a machine with one core.
    if (std::thread::hardware_concurrency() < 2) {
        CHECK_MSG(true, "single-core host, nothing to spread");
        return;
    }

    std::vector<std::atomic<size_t>> ids(64);
    for (auto& id : ids) id.store(0);

    JobSystem::Dispatch(2048, 32, [&ids](JobSystem::JobArgs args) {
        const size_t hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
        ids[args.groupIndex % ids.size()].store(hash);
        // Enough work that groups genuinely overlap in time.
        volatile double sink = 0.0;
        for (int k = 0; k < 200; ++k) sink += static_cast<double>(k);
        (void)sink;
    });
    JobSystem::Wait();

    std::vector<size_t> distinct;
    for (const auto& id : ids) {
        const size_t value = id.load();
        if (value == 0) continue;
        if (std::find(distinct.begin(), distinct.end(), value) == distinct.end()) {
            distinct.push_back(value);
        }
    }
    CHECK_MSG(distinct.size() >= 2, "the dispatch must have run on more than one thread");
}

static void testExecuteRunsSingleTasks() {
    std::atomic<int> counter{0};
    for (int i = 0; i < 50; ++i) {
        JobSystem::Execute([&counter] { counter.fetch_add(1); });
    }
    JobSystem::Wait();
    CHECK_EQ(counter.load(), 50);
}

static void testAThrowingJobDoesNotHangTheFence() {
    // A leaked pending counter is the worst outcome here: every later Wait
    // blocks forever, which presents as a frozen engine rather than as the
    // exception that caused it.
    JobSystem::Dispatch(16, 4, [](JobSystem::JobArgs args) {
        if (args.jobIndex == 7) throw std::runtime_error("deliberate");
    });
    JobSystem::Wait();
    CHECK_MSG(!JobSystem::IsBusy(), "the counter must come down even when a job throws");

    // And the pool must still be usable afterwards.
    std::atomic<int> counter{0};
    JobSystem::Dispatch(100, 10, [&counter](JobSystem::JobArgs) { counter.fetch_add(1); });
    JobSystem::Wait();
    CHECK_EQ(counter.load(), 100);
}

static void testDegenerateArgumentsAreHarmless() {
    std::atomic<int> counter{0};

    JobSystem::Dispatch(0, 16, [&counter](JobSystem::JobArgs) { counter.fetch_add(1); });
    JobSystem::Wait();
    CHECK_EQ(counter.load(), 0);

    // A zero group size would divide by zero if it were not clamped.
    JobSystem::Dispatch(10, 0, [&counter](JobSystem::JobArgs) { counter.fetch_add(1); });
    JobSystem::Wait();
    CHECK_EQ(counter.load(), 10);

    JobSystem::Dispatch(5, 16, nullptr);
    JobSystem::Wait();
    CHECK(true);
}

static void testInitializeIsIdempotentAndShutdownIsSafe() {
    const unsigned int before = JobSystem::ThreadCount();
    JobSystem::Initialize(8);
    CHECK_MSG(JobSystem::ThreadCount() == before,
              "a second Initialize must not spin up another pool");

    JobSystem::Shutdown();
    CHECK(!JobSystem::IsInitialized());
    JobSystem::Shutdown(); // must not crash or hang
    CHECK(!JobSystem::IsInitialized());

    // And it must come back up cleanly.
    JobSystem::Initialize(2);
    CHECK_EQ(JobSystem::ThreadCount(), 2u);
    std::atomic<int> counter{0};
    JobSystem::Dispatch(64, 8, [&counter](JobSystem::JobArgs) { counter.fetch_add(1); });
    JobSystem::Wait();
    CHECK_EQ(counter.load(), 64);
    JobSystem::Shutdown();
}

static void runTests() {
    testDispatchRunsInlineWithoutAPool();
    testEveryItemRunsExactlyOnce();
    testWaitIsARealFence();
    testGroupsCoverContiguousRanges();
    testWorkActuallySpreadsAcrossThreads();
    testExecuteRunsSingleTasks();
    testAThrowingJobDoesNotHangTheFence();
    testDegenerateArgumentsAreHarmless();
    testInitializeIsIdempotentAndShutdownIsSafe();
}

TEST_MAIN("test_jobs", 27)
