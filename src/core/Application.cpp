#include "core/Application.hpp"

#include <atomic>

namespace Supersonic {
namespace Application {

namespace {

// Atomic because the request can come from a job.
//
// Not for the run loop's benefit - that reads it from the main thread like
// everything else - but because a game is free to ask from wherever it noticed
// it wanted to stop, and a plain bool written from a worker and read from the
// main thread is a data race whatever the generated code happens to do.
std::atomic<bool> g_quitRequested{false};

} // namespace

void RequestQuit() { g_quitRequested.store(true, std::memory_order_relaxed); }

bool QuitRequested() { return g_quitRequested.load(std::memory_order_relaxed); }

void ClearQuitRequest() { g_quitRequested.store(false, std::memory_order_relaxed); }

} // namespace Application
} // namespace Supersonic
