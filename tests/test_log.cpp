// The engine log: a bounded, thread-safe ring of entries with an optional file behind it.
//
// Log had no suite, and it is the one thing every other subsystem talks to when
// something goes wrong - including from the job system's worker threads. The property
// worth pinning is the bound: an engine that runs for an hour with an asset error in
// the frame loop must not grow the log without limit, and must say how much it threw
// away.

#include "TestHarness.hpp"
#include "core/Log.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Supersonic;

namespace {

constexpr size_t kCapacity = 2048;   // Log.cpp's, and the reason this test fills past it

std::string slurp(const std::string& path) {
    std::ifstream file(path);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

} // namespace

static void testALevelHasAName() {
    CHECK(std::string(Log::LevelName(Log::Level::Trace)) == "TRACE");
    CHECK(std::string(Log::LevelName(Log::Level::Info)) == "INFO");
    CHECK(std::string(Log::LevelName(Log::Level::Warning)) == "WARN");
    CHECK(std::string(Log::LevelName(Log::Level::Error)) == "ERROR");
}

static void testEntriesComeBackInTheOrderTheyWereSubmitted() {
    Log::Clear();
    Log::Submit(Log::Level::Info, "alpha", "first");
    Log::Submit(Log::Level::Warning, "beta", "second");
    Log::Submit(Log::Level::Error, "gamma", "third");

    const auto entries = Log::Snapshot();
    CHECK_EQ(static_cast<int>(entries.size()), 3);
    if (entries.size() != 3) return;
    CHECK(entries[0].category == "alpha" && entries[0].message == "first" && entries[0].level == Log::Level::Info);
    CHECK(entries[1].category == "beta" && entries[1].level == Log::Level::Warning);
    CHECK(entries[2].message == "third" && entries[2].level == Log::Level::Error);
    Log::Clear();
}

static void testAStreamIsOneEntryAndFlushesWhenItEnds() {
    Log::Clear();
    SUPERSONIC_LOG_INFO("stream") << "loaded " << 42 << " things in " << 1.5f << "s" << std::endl;
    SUPERSONIC_LOG_ERROR("stream") << "bad";

    const auto entries = Log::Snapshot();
    CHECK_EQ(static_cast<int>(entries.size()), 2);
    if (entries.size() != 2) return;
    CHECK_MSG(entries[0].message == "loaded 42 things in 1.5s",
              "the pieces are one message, and std::endl is not part of it: '" + entries[0].message + "'");
    CHECK(entries[1].level == Log::Level::Error && entries[1].message == "bad");
    Log::Clear();
}

static void testTheLogIsBoundedAndSaysWhatItDropped() {
    Log::Clear();
    constexpr size_t kExtra = 25;
    for (size_t i = 0; i < kCapacity + kExtra; ++i) {
        Log::Submit(Log::Level::Trace, "flood", std::to_string(i));
    }

    const auto entries = Log::Snapshot();
    CHECK_MSG(entries.size() == kCapacity, "the log holds no more than its capacity");
    CHECK_MSG(Log::DroppedCount() == kExtra, "and counts what it threw away");
    if (entries.size() == kCapacity) {
        CHECK_MSG(entries.front().message == std::to_string(kExtra),
                  "the OLDEST are the ones dropped, so the newest news is kept");
        CHECK(entries.back().message == std::to_string(kCapacity + kExtra - 1));
    }

    Log::Clear();
    CHECK_MSG(Log::Snapshot().empty() && Log::DroppedCount() == 0, "Clear empties it and resets the count");
}

static void testTheFileSinkWritesEveryLineAndStopsWhenClosed() {
    Log::Clear();
    const std::string path = "test_log_sink_tmp.log";
    std::remove(path.c_str());

    CHECK_MSG(Log::SetFileSink(path), "a writable path opens");
    Log::Submit(Log::Level::Info, "disk", "one");
    Log::Submit(Log::Level::Error, "disk", "two");
    // Flushed per line: a crash is exactly when the last line matters, so the file
    // is complete without closing it.
    const std::string whileOpen = slurp(path);
    CHECK_MSG(whileOpen == "INFO [disk] one\nERROR [disk] two\n",
              "each line is level, category and message, already on disk: '" + whileOpen + "'");

    Log::CloseFileSink();
    Log::Submit(Log::Level::Info, "disk", "after");
    CHECK_MSG(slurp(path) == whileOpen, "a closed sink takes nothing more");

    // Reopening starts the file again rather than appending to a previous run's.
    CHECK(Log::SetFileSink(path));
    Log::Submit(Log::Level::Warning, "disk", "fresh");
    Log::CloseFileSink();
    CHECK_MSG(slurp(path) == "WARN [disk] fresh\n", "and a new sink truncates");

    std::remove(path.c_str());
    Log::Clear();
}

static void testASinkThatCannotOpenSaysSoAndLogsOnRegardless() {
    Log::Clear();
    CHECK_MSG(!Log::SetFileSink("test_log_no_such_directory_tmp/inner/file.log"),
              "a path that cannot be opened is reported");
    Log::Submit(Log::Level::Info, "still", "works");
    CHECK_MSG(Log::Snapshot().size() == 1, "and the in-memory log is unaffected");
    Log::Clear();
}

static void testManyThreadsLoggingLoseNothingThatWasKept() {
    // The job system's workers log. Four threads past the capacity: nothing may
    // be lost or double-counted, whatever order they interleave in.
    Log::Clear();
    constexpr int kThreads = 4;
    constexpr int kEach = 550;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t] {
            for (int i = 0; i < kEach; ++i) {
                Log::Submit(Log::Level::Trace, "thread" + std::to_string(t), std::to_string(i));
            }
        });
    }
    for (auto& thread : threads) thread.join();

    const size_t total = static_cast<size_t>(kThreads) * kEach;
    CHECK_MSG(Log::Snapshot().size() + Log::DroppedCount() == total,
              "every submit is either kept or counted as dropped");
    CHECK(Log::Snapshot().size() == kCapacity);

    // Within one thread, entries stay in the order that thread wrote them.
    int previous[kThreads];
    for (int& p : previous) p = -1;
    bool inOrder = true;
    for (const auto& entry : Log::Snapshot()) {
        const int thread = entry.category.back() - '0';
        const int number = std::stoi(entry.message);
        if (number <= previous[thread]) inOrder = false;
        previous[thread] = number;
    }
    CHECK_MSG(inOrder, "and no thread's own lines are reordered");
    Log::Clear();
}

static void runTests() {
    testALevelHasAName();
    testEntriesComeBackInTheOrderTheyWereSubmitted();
    testAStreamIsOneEntryAndFlushesWhenItEnds();
    testTheLogIsBoundedAndSaysWhatItDropped();
    testTheFileSinkWritesEveryLineAndStopsWhenClosed();
    testASinkThatCannotOpenSaysSoAndLogsOnRegardless();
    testManyThreadsLoggingLoseNothingThatWasKept();
}

TEST_MAIN("test_log", 22)
