#include "core/JobSystem.hpp"
#include "core/Log.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace Supersonic {

namespace {

struct Job {
    std::function<void(JobSystem::JobArgs)> task;
    uint32_t groupIndex{0};
    uint32_t begin{0};
    uint32_t end{0};
};

struct Pool {
    std::vector<std::thread> workers;
    std::deque<Job> queue;

    std::mutex mutex;
    std::condition_variable wake;

    // Counts groups submitted but not yet finished. A counter rather than a
    // per-dispatch future, because the fence needed here is "everything is done",
    // and that is one atomic instead of an allocation per dispatch.
    std::atomic<uint32_t> pending{0};
    std::atomic<bool> running{false};
};

Pool& pool() {
    static Pool instance;
    return instance;
}

} // namespace

bool JobSystem::IsInitialized() {
    return pool().running.load(std::memory_order_acquire);
}

unsigned int JobSystem::ThreadCount() {
    return static_cast<unsigned int>(pool().workers.size());
}

bool JobSystem::tryRunOneJob() {
    Pool& p = pool();

    Job job;
    {
        std::lock_guard<std::mutex> lock(p.mutex);
        if (p.queue.empty()) return false;
        job = std::move(p.queue.front());
        p.queue.pop_front();
    }

    // The counter must come down even if the job throws. Leaking it once hangs
    // every future Wait() forever, which presents as a frozen engine rather than
    // as the exception that caused it.
    try {
        for (uint32_t i = job.begin; i < job.end; ++i) {
            job.task(JobArgs{i, job.groupIndex});
        }
    } catch (const std::exception& e) {
        SUPERSONIC_LOG_ERROR("JobSystem") << "Job threw: " << e.what() << std::endl;
    } catch (...) {
        SUPERSONIC_LOG_ERROR("JobSystem") << "Job threw a non-std exception." << std::endl;
    }

    p.pending.fetch_sub(1, std::memory_order_release);
    return true;
}

void JobSystem::Initialize(unsigned int threadCount) {
    Pool& p = pool();
    if (p.running.load(std::memory_order_acquire)) return;

    if (threadCount == 0) {
        const unsigned int hardware = std::max(1u, std::thread::hardware_concurrency());
        // One core is left for the thread that submits the work, which is also
        // the thread that records Vulkan commands.
        threadCount = hardware > 1 ? hardware - 1 : 1;
    }

    p.running.store(true, std::memory_order_release);
    p.workers.reserve(threadCount);

    for (unsigned int i = 0; i < threadCount; ++i) {
        p.workers.emplace_back([] {
            Pool& self = pool();
            while (true) {
                if (tryRunOneJob()) continue;

                std::unique_lock<std::mutex> lock(self.mutex);
                // The predicate is re-checked under the lock, so a job queued
                // between the failed pop above and this wait is not missed.
                self.wake.wait(lock, [&self] {
                    return !self.queue.empty() || !self.running.load(std::memory_order_acquire);
                });
                if (!self.running.load(std::memory_order_acquire) && self.queue.empty()) return;
            }
        });
    }

    SUPERSONIC_LOG_INFO("JobSystem") << threadCount << " worker thread(s) started ("
              << std::thread::hardware_concurrency() << " hardware threads reported)." << std::endl;
}

void JobSystem::Shutdown() {
    Pool& p = pool();
    if (!p.running.load(std::memory_order_acquire)) return;

    // Finish what was submitted before tearing down, so a job holding a
    // reference to something the caller is about to destroy has already run.
    Wait();

    p.running.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(p.mutex);
        p.wake.notify_all();
    }

    for (auto& worker : p.workers) {
        if (worker.joinable()) worker.join();
    }
    p.workers.clear();

    SUPERSONIC_LOG_INFO("JobSystem") << "Workers joined." << std::endl;
}

void JobSystem::Execute(const std::function<void()>& task) {
    if (!task) return;

    if (!IsInitialized()) {
        task();
        return;
    }

    Pool& p = pool();
    p.pending.fetch_add(1, std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(p.mutex);
        Job job;
        job.task = [task](JobArgs) { task(); };
        job.begin = 0;
        job.end = 1;
        p.queue.push_back(std::move(job));
    }
    p.wake.notify_one();
}

void JobSystem::Dispatch(uint32_t jobCount, uint32_t groupSize,
                         const std::function<void(JobArgs)>& task) {
    if (jobCount == 0 || !task) return;
    if (groupSize == 0) groupSize = 1;

    // Inline when there is no pool, or when the whole dispatch is one group -
    // handing a single group to a worker and then waiting for it costs more than
    // doing it here.
    const uint32_t groupCount = (jobCount + groupSize - 1) / groupSize;
    if (!IsInitialized() || groupCount == 1) {
        for (uint32_t i = 0; i < jobCount; ++i) {
            task(JobArgs{i, 0});
        }
        return;
    }

    Pool& p = pool();
    p.pending.fetch_add(groupCount, std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(p.mutex);
        for (uint32_t group = 0; group < groupCount; ++group) {
            Job job;
            job.task = task;
            job.groupIndex = group;
            job.begin = group * groupSize;
            job.end = std::min(job.begin + groupSize, jobCount);
            p.queue.push_back(std::move(job));
        }
    }
    p.wake.notify_all();
}

bool JobSystem::IsBusy() {
    return pool().pending.load(std::memory_order_acquire) > 0;
}

void JobSystem::Wait() {
    // The waiting thread runs jobs instead of blocking. Otherwise the main
    // thread idles through every dispatch it issues, which on a 4-core machine
    // throws away a quarter of the available work.
    while (IsBusy()) {
        if (!tryRunOneJob()) {
            std::this_thread::yield();
        }
    }
}

} // namespace Supersonic
