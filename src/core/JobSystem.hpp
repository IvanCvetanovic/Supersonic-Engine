#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

namespace Supersonic {

// Worker-thread pool with a counter-based fence.
//
// Everything in this engine ran on one thread. That is the right default - most
// of the frame is either Vulkan command recording or ECS iteration that mutates
// shared state, and neither is safe to spread across threads without care - but
// it left the genuinely independent per-item work (particle integration,
// heightfield generation, per-vertex mesh post-processing) serialised for no
// reason.
//
// Deliberately small: a queue, N workers, and a pending counter. There is no
// work stealing and no job graph, because nothing here needs one, and a
// scheduler nobody can reason about is worse than a loop.
class JobSystem {
public:
    struct JobArgs {
        // Index of the item within the dispatch.
        uint32_t jobIndex{0};
        // Index of the group this job belongs to, for per-group scratch space.
        uint32_t groupIndex{0};
    };

    // Starts the workers. threadCount of 0 asks for hardware_concurrency minus
    // one, leaving a core for the thread that submits the work. Calling this
    // twice is a no-op, so a system can initialise it lazily without
    // coordinating with anyone else.
    static void Initialize(unsigned int threadCount = 0);

    // Drains outstanding work and joins. Safe to call without Initialize.
    static void Shutdown();

    static bool IsInitialized();
    static unsigned int ThreadCount();

    // Runs one task on a worker.
    static void Execute(const std::function<void()>& task);

    // Parallel-for over jobCount items, handed out in groups of groupSize. A
    // group is the unit of scheduling: one item per job would spend more time in
    // the queue than in the work.
    //
    // Falls back to running inline on the calling thread when the pool is not
    // running, so a caller never has to check first and headless tests need no
    // special case.
    static void Dispatch(uint32_t jobCount, uint32_t groupSize,
                         const std::function<void(JobArgs)>& task);

    // True while any submitted work is outstanding.
    static bool IsBusy();

    // Blocks until everything submitted so far has finished. The calling thread
    // helps out rather than sleeping, so a Wait from the main thread does not
    // waste a core.
    //
    // Not to be called from inside a job: it would wait on a counter that its
    // own unfinished job is holding up.
    static void Wait();

private:
    static bool tryRunOneJob();
};

} // namespace Supersonic
