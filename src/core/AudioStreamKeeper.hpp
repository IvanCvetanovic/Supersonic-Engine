#pragma once

#include <algorithm>
#include <string>

namespace Supersonic {

// WHEN THE OUTPUT STREAM IS OPEN, as a pure policy (the AAudio backend's, in AudioEngine.cpp).
//
// A phone's stream comes and goes: it is closed with the app, opened again when the app is in front,
// lost when the output device changes, and an open can fail. What to do on each of those is decided
// here, with the stream behind an interface, so a suite can script the failures that a device only
// shows on a bad day (tests/test_audiostreamkeeper.cpp). The backend implements Ops over the real
// stream and owns the mutex: the keeper itself is not thread-safe, and every call is made with the
// backend's stream lock held.
//
// The rules, each of which a test pins:
//  - SUSPEND CLOSES the stream. It is not paused. A paused stream that is started again carries
//    whatever the system decided about it while the app was away, and a hardened Android is said to
//    decide silently (see AudioWake.hpp: unconfirmed). A closed one has no past.
//  - RESUME OPENS A NEW STREAM, and never reuses an old one. Idempotent: a resume that finds the
//    stream running (no suspend came before it) opens nothing over it. Self-healing: a resume that
//    finds no stream (an earlier open failed) opens one.
//  - NOTHING OPENS WHILE SUSPENDED: not a retry, not the answer to a device that went away in the
//    background. The resume opens the stream, once the app is in front.
//  - A STREAM THAT IS GONE OR DISCONNECTED is opened again by Maintain, once a frame, with a wait
//    between failed attempts that doubles: 1, 2, 4, 8, 16, 30, 30 ... seconds. Each attempt can
//    stall the game's thread, so a stream that cannot be had is not asked for every frame. A success
//    puts the wait back at one second, and also keeps the next attempt at least that far off, so a
//    stream that opens and dies at once cannot be reopened every frame.
//
// Time is an argument, in seconds from any steady clock.
class AudioStreamKeeper {
public:
    // What the backend's stream can do. Called with the backend's lock held.
    class Ops {
    public:
        virtual ~Ops() = default;
        // Open a NEW stream and start it. True: a running stream exists. False: none does, and `error` says why.
        virtual bool OpenStream(std::string& error) = 0;
        // Close the stream. Only called when HasStream().
        virtual void CloseStream() = 0;
        virtual bool HasStream() const = 0;
        // The client's last word that the stream is gone (AAUDIO_STREAM_STATE_DISCONNECTED). Must not block.
        virtual bool StreamDisconnected() const = 0;
    };

    // What a call did, for the log.
    struct Report {
        bool closed{false};
        bool tried{false};    // an open was attempted
        bool opened{false};   // and it worked
        std::string error;    // why it did not
        double retryAfter{0.0};   // after a failed attempt: seconds until Maintain tries again
    };

    static constexpr double kFirstRetrySeconds = 1.0;
    static constexpr double kLongestRetrySeconds = 30.0;

    explicit AudioStreamKeeper(Ops& ops) : m_ops(ops) {}

    bool Suspended() const { return m_suspended; }

    // `now` is unused today: it keeps Suspend shaped like Resume, Reopen and Maintain (every call hands the keeper
    // the caller's clock), so a rule that needs the time of a suspend changes no caller.
    Report Suspend(double /*now*/) {
        Report report;
        m_suspended = true;
        if (m_ops.HasStream()) {
            m_ops.CloseStream();
            report.closed = true;
        }
        return report;
    }

    Report Resume(double now) {
        Report report;
        if (!m_suspended && m_ops.HasStream()) return report;
        m_suspended = false;
        // Not a stream that lived through the suspend: nothing does, and a leftover one is not trusted.
        Close(report);
        Attempt(now, report);
        return report;
    }

    // The device went away (AAudio's disconnect callback, on the thread the backend hands it to).
    Report Reopen(double now) {
        Report report;
        Close(report);
        if (m_suspended) return report;
        Attempt(now, report);
        return report;
    }

    // Once a frame, on the game's thread.
    Report Maintain(double now) {
        Report report;
        if (m_suspended) return report;
        const bool has = m_ops.HasStream();
        if (has && !m_ops.StreamDisconnected()) return report;
        if (now < m_nextAttempt) return report;
        Close(report);
        Attempt(now, report);
        return report;
    }

private:
    void Close(Report& report) {
        if (!m_ops.HasStream()) return;
        m_ops.CloseStream();
        report.closed = true;
    }

    void Attempt(double now, Report& report) {
        report.tried = true;
        report.opened = m_ops.OpenStream(report.error);
        if (report.opened) {
            m_retryDelay = kFirstRetrySeconds;
            m_nextAttempt = now + kFirstRetrySeconds;
            return;
        }
        report.retryAfter = m_retryDelay;
        m_nextAttempt = now + m_retryDelay;
        m_retryDelay = std::min(m_retryDelay * 2.0, kLongestRetrySeconds);
    }

    Ops& m_ops;
    bool m_suspended{false};
    double m_retryDelay{kFirstRetrySeconds};
    double m_nextAttempt{0.0};
};

} // namespace Supersonic
