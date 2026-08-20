#pragma once

#include <array>
#include <chrono>
#include <cstddef>

namespace Supersonic {

// Per-frame CPU timings for the phases Run() already executes in sequence.
//
// There was no timing instrumentation anywhere in the engine - no CPU zones,
// no GPU queries - so every statement about where a frame goes was a guess.
// The statistics panel showed one number, and that number was ImGui's own
// 60-frame mean, which cannot contain a hitch.
//
// Deliberately not thread-safe and deliberately not nestable. Every zone below
// brackets a call made from the main thread in Run(); the job system's workers
// are timed by the zone that dispatches and waits on them, which is the number
// that matters to a frame. A hierarchical profiler is a different tool and a
// much larger one.
enum class ProfileZone : std::size_t {
    Physics,
    Audio,
    Scripts,
    Animation,
    Particles,
    Transform,
    EditorUI,
    ImGuiRender,
    ResourceSync,
    PoseEvaluation,

    // The four halves of DrawFrame, which partition it exactly.
    //
    // One zone for the whole of it said 6.08 ms and answered nothing, because
    // most of that can be the CPU waiting on the GPU rather than doing
    // anything: DrawFrame opens by waiting on the fence of the frame two ago,
    // possibly waiting again on the image's own fence, and then acquiring -
    // which blocks on presentation. Optimising recording against that number
    // would be optimising against a wait.
    FrameWait,
    FramePrepare,
    ShadowRecord,
    SceneRecord,

    UndoCommit,
    Count,
};

class Profiler {
public:
    static constexpr std::size_t kZoneCount = static_cast<std::size_t>(ProfileZone::Count);

    // Call once at the top of a frame. Timings are per frame, not cumulative:
    // a running total answers a question nobody is asking.
    static void BeginFrame() {
        for (double& ms : Accumulator()) ms = 0.0;
    }

    // Milliseconds spent in a zone during the frame just measured.
    static double Milliseconds(ProfileZone zone) {
        return Accumulator()[static_cast<std::size_t>(zone)];
    }

    static const char* Name(ProfileZone zone) {
        static constexpr const char* kNames[kZoneCount] = {
            "Physics", "Audio", "Scripts", "Animation", "Particles",
            "Transform", "Editor UI", "ImGui Render", "Resource Sync",
            "Pose Evaluation",
            "Frame Wait", "Frame Prepare", "Shadow Record", "Scene Record",
            "Undo Commit",
        };
        return kNames[static_cast<std::size_t>(zone)];
    }

    // Accumulates rather than overwrites, so a zone entered more than once in a
    // frame - the fixed physics step, most obviously - reports the frame's
    // total cost rather than the last iteration's.
    class Scope {
    public:
        explicit Scope(ProfileZone zone)
            : m_zone(zone), m_start(std::chrono::steady_clock::now()) {}

        // Ends the zone before the object dies.
        //
        // For a region that cannot be wrapped in braces because what it
        // computes is used after it - which is most of a render function, where
        // the command buffer and the frustum outlive the setup that built them.
        // The alternative is hoisting half a dozen declarations out of the
        // function body to make room for a brace, which is instrumentation
        // rearranging the code it is supposed to be measuring.
        //
        // Idempotent, so the destructor after an explicit Stop does nothing and
        // a zone is never counted twice.
        void Stop() {
            if (m_stopped) return;
            m_stopped = true;
            const auto elapsed = std::chrono::steady_clock::now() - m_start;
            const double ms =
                std::chrono::duration<double, std::milli>(elapsed).count();
            Accumulator()[static_cast<std::size_t>(m_zone)] += ms;
        }

        ~Scope() { Stop(); }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        ProfileZone m_zone;
        std::chrono::steady_clock::time_point m_start;
        bool m_stopped{false};
    };

private:
    // A function-local static rather than a namespace-scope one: this header is
    // included from a static library linked into several binaries, and a
    // function-local static has one definition no matter how many translation
    // units reach it.
    static std::array<double, kZoneCount>& Accumulator() {
        static std::array<double, kZoneCount> zones{};
        return zones;
    }
};

} // namespace Supersonic

// Token pasting so two zones can share a scope without colliding.
#define SUPERSONIC_PROFILE_CONCAT_INNER(a, b) a##b
#define SUPERSONIC_PROFILE_CONCAT(a, b) SUPERSONIC_PROFILE_CONCAT_INNER(a, b)
#define SUPERSONIC_PROFILE(zone) \
    ::Supersonic::Profiler::Scope SUPERSONIC_PROFILE_CONCAT(_profileScope, __LINE__)( \
        ::Supersonic::ProfileZone::zone)
