#include "core/Log.hpp"

#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>

namespace Supersonic::Log {

namespace {

// Bounded on purpose. An engine that runs for an hour with an asset error in
// the frame loop would otherwise grow this without limit, and an out-of-memory
// crash caused by the log is a poor way to learn about a missing texture.
constexpr std::size_t kCapacity = 2048;

struct State {
    std::mutex mutex;
    std::deque<Entry> entries;
    std::size_t dropped = 0;
    std::ofstream file;
    bool elapsed = false;
    // Taken when the state is first built, which is the first line logged: the
    // zero of the times SetElapsedTimestamps writes.
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

// Function-local so it is constructed on first use. A namespace-scope object
// would be racing static initialisation against the first subsystem that logs
// during construction, which on this codebase is JobSystem::Initialize.
State& state() {
    static State s;
    return s;
}

} // namespace

const char* LevelName(Level level) {
    switch (level) {
        case Level::Trace:   return "TRACE";
        case Level::Info:    return "INFO";
        case Level::Warning: return "WARN";
        case Level::Error:   return "ERROR";
    }
    return "?";
}

void Submit(Level level, std::string category, std::string message) {
    // Warnings and errors to stderr, everything else to stdout - which is what
    // the call sites this replaced already did, so piping behaviour is
    // unchanged for anyone who was redirecting one and not the other.
    std::ostream& console = (level >= Level::Warning) ? std::cerr : std::cout;
    console << '[' << category << "] " << message << std::endl;

    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);

    if (s.file.is_open()) {
        s.file << LevelName(level);
        if (s.elapsed) {
            s.file << ' '
                   << FormatElapsed(std::chrono::duration<double>(std::chrono::steady_clock::now() - s.start).count());
        }
        s.file << " [" << category << "] " << message << '\n';
        s.file.flush();  // a crash is exactly when the last line matters most
    }

    if (s.entries.size() >= kCapacity) {
        s.entries.pop_front();
        ++s.dropped;
    }
    s.entries.push_back(Entry{level, std::move(category), std::move(message)});
}

std::vector<Entry> Snapshot() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return std::vector<Entry>(s.entries.begin(), s.entries.end());
}

std::size_t DroppedCount() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.dropped;
}

void Clear() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.entries.clear();
    s.dropped = 0;
}

bool SetFileSink(const std::string& path) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.file.close();
    s.file.clear();
    s.file.open(path, std::ios::out | std::ios::trunc);
    return s.file.is_open();
}

void CloseFileSink() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.file.close();
}

void SetElapsedTimestamps(bool enabled) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.elapsed = enabled;
}

std::string FormatElapsed(double seconds) {
    char text[32];
    std::snprintf(text, sizeof(text), "+%.3fs", seconds < 0.0 ? 0.0 : seconds);
    return text;
}

} // namespace Supersonic::Log
