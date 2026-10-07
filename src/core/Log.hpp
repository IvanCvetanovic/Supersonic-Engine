#pragma once

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace Supersonic {

// Engine diagnostics.
//
// There were 139 bare std::cout and std::cerr calls across the engine and no
// logging type of any kind - no level, no category, no sink, no way to redirect
// or filter. Those calls are the whole diagnostic surface for the failures
// users actually hit: why an entity will not move, why a surface is untextured,
// why the engine died during init. None of it reached the window the user was
// looking at, because there was no console panel to reach.
//
// The sites already carried a "[Subsystem]" prefix, so the categories were
// there in spirit; they were just baked into the message text where nothing
// could filter on them.
//
// One thing deliberately does NOT go through here: program output. --help text
// and the --frames profiler report are what the program was asked to produce,
// not diagnostics about producing it, and they belong on stdout whether or not
// anyone is filtering logs.
namespace Log {

enum class Level { Trace, Info, Warning, Error };

const char* LevelName(Level level);

struct Entry {
    Level level = Level::Info;
    std::string category;
    std::string message;
};

// Writes to the console sink and appends to the ring buffer. Safe to call from
// any thread: the job system's workers log, and so does the Vulkan debug
// messenger, which the driver invokes on whichever thread made the call.
void Submit(Level level, std::string category, std::string message);

// A copy, because the editor draws this while other threads may still be
// logging. The buffer is bounded; DroppedCount() reports how many entries fell
// off the back, so a console that has lost history says so instead of quietly
// starting mid-story.
std::vector<Entry> Snapshot();
std::size_t DroppedCount();
void Clear();

// Mirrors everything to a file. A shipped game gets WIN32_EXECUTABLE and has no
// console at all, so without this its diagnostics go nowhere.
bool SetFileSink(const std::string& path);
void CloseFileSink();

// Opt-in: the file sink's lines then carry the seconds since the first line was
// logged - "INFO +1.234s [Window] ..." instead of "INFO [Window] ...". Off by
// default, so no log anyone reads or diffs changes unless its program asks.
//
// For a shipped game's log, which is what a player sends when a start stalls:
// without times it cannot say a slow start from a stuck one, and the last line
// is all it can name. The console and the in-memory buffer are not stamped; the
// first is read live and the second is the editor's panel, which has its own
// column for it.
void SetElapsedTimestamps(bool enabled);

// "+1.234s": the form the file sink writes. Split out so a suite can check it
// without a clock.
std::string FormatElapsed(double seconds);

// Built into an ostringstream and submitted on destruction, so the existing
// call sites keep their streaming form and their ordering.
class Stream {
public:
    Stream(Level level, std::string category)
        : m_level(level), m_category(std::move(category)) {}

    ~Stream() { Submit(m_level, std::move(m_category), m_buffer.str()); }

    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    template <typename T>
    Stream& operator<<(const T& value) {
        m_buffer << value;
        return *this;
    }

    // Absorbs a trailing std::endl. The call sites this replaced ended in one,
    // and letting it through would embed a newline in the stored message and
    // put a blank line in the console panel.
    Stream& operator<<(std::ostream& (*)(std::ostream&)) { return *this; }

private:
    Level m_level;
    std::string m_category;
    std::ostringstream m_buffer;
};

} // namespace Log
} // namespace Supersonic

#define SUPERSONIC_LOG_TRACE(category) \
    ::Supersonic::Log::Stream(::Supersonic::Log::Level::Trace, (category))
#define SUPERSONIC_LOG_INFO(category) \
    ::Supersonic::Log::Stream(::Supersonic::Log::Level::Info, (category))
#define SUPERSONIC_LOG_WARN(category) \
    ::Supersonic::Log::Stream(::Supersonic::Log::Level::Warning, (category))
#define SUPERSONIC_LOG_ERROR(category) \
    ::Supersonic::Log::Stream(::Supersonic::Log::Level::Error, (category))
