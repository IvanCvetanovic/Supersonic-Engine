#pragma once

namespace Supersonic {

// WHEN A PHONE'S SOUND STOPS AND STARTS AGAIN, as a pure policy.
//
// The Android lifecycle hands the engine a stream of commands (PAUSE, LOST_FOCUS, TERM_WINDOW, STOP,
// START, RESUME, INIT_WINDOW, WINDOW_RESIZED, GAINED_FOCUS); the sound has to stop with the app and
// come back when the app is certainly in front. Which command is "certainly" is the whole question,
// so the answer lives here, with no platform in it, where a suite can feed it the order a device was
// seen to send and say what it did (tests/test_audiowake.cpp). AndroidApp.cpp only forwards its
// commands and carries out the action it is handed.
//
// WHY NOT WAKE AT RESUME. The NDK's glue holds the activity's UI thread inside Activity.onResume until
// the game thread has dequeued APP_CMD_RESUME (android_native_app_glue.c: onResume waits in
// android_app_set_activity_state, released by android_app_pre_exec_cmd, which runs BEFORE the app's
// handler body; AndroidApp.hpp, PumpEvents, says the same). So the handler body runs while the UI
// thread is only now leaving onResume, and a stream started from it may start before the platform has
// been told that the activity resumed. That "may" is a reading of the glue, not a measurement. Android 17 documents
// "background audio hardening" (developer.android.com/about/versions/17/changes/bg-audio, fetched on
// 2026-11-08): every app on Android 17, whatever its targetSdk, needs a visible activity (or a
// foreground service) for audio playback, focus and volume calls, and when it has none "the audio
// playback ... fail[s] silently without throwing an exception or providing a failure message"; the
// page names AAudioStream_write among the silenced calls and does not list a callback stream's start,
// and a hardened call leaves an `AudioHardening` entry in `dumpsys audio` and logcat. Whether a start made at RESUME counts as
// outside is NOT MEASURED, and nothing was heard on any device; the bug that prompted this (the
// sound is gone after a minimise) has no confirmed cause. A stream opened once the window has focus is outside the
// question either way, which is why that is the point chosen. On the real order of the command log
// (an API 33 x86_64 emulator, the port's planning step 204) GAINED_FOCUS came 470 ms after RESUME.
//
// Not thread-safe: the game's thread owns it. Time is an argument, in seconds from any steady clock,
// so a test never sleeps.
class AudioWake {
public:
    enum class Action {
        None,
        Suspend,   // close the sound's stream
        Wake,      // open a NEW one
        Rewake,    // the stream opened without focus is not trusted any more: close it and open a NEW one
    };

    // How long a resumed app with a window may go without GAINED_FOCUS before its sound starts anyway:
    // a phone has been seen to resume without ever sending it (AndroidApp.cpp's touch heal).
    static constexpr double kFallbackSeconds = 1.0;

    // The sound handler has been installed, and the stream it belongs to has just been opened running.
    // A handler installed while the app is already in the background starts suspended, rather than
    // playing until the next pause.
    Action OnHandlerInstalled(bool resumedNow) {
        m_awake = true;
        m_wokeWithoutFocus = false;
        if (resumedNow) return Action::None;
        m_awake = false;
        return Action::Suspend;
    }

    // The activity paused: the app is leaving the screen. One suspend per pause, however many commands
    // say so, and focus is gone from here (LOST_FOCUS follows later, or not at all).
    Action OnPause() {
        m_resumed = false;
        m_focused = false;
        m_wokeWithoutFocus = false;
        if (!m_awake) return Action::None;
        m_awake = false;
        return Action::Suspend;
    }

    // The activity resumed. Nothing is started here (see the class comment); the clock for the
    // fallback starts.
    Action OnResume(double now) {
        m_resumed = true;
        m_resumedAt = now;
        return Action::None;
    }

    // A native window exists (INIT_WINDOW) or does not (TERM_WINDOW).
    Action OnWindow(bool present) {
        m_window = present;
        return Action::None;
    }

    // The window gained or lost focus, or a finger proved that it has it (AndroidApp.cpp's touch heal).
    Action OnFocus(bool focused) {
        m_focused = focused;
        if (!focused) return Action::None;
        return Evaluate(0.0, false);
    }

    // Once a frame, while the app has frames to run.
    Action Tick(double now) { return Evaluate(now, true); }

    bool Awake() const { return m_awake; }

private:
    Action Evaluate(double now, bool fromTick) {
        if (!m_resumed || !m_window) return Action::None;
        if (m_awake) {
            // The sound started without focus (the fallback) and focus has now arrived: what started
            // before it is exactly what cannot be trusted, so it is opened again.
            if (m_focused && m_wokeWithoutFocus) {
                m_wokeWithoutFocus = false;
                return Action::Rewake;
            }
            return Action::None;
        }
        if (!m_focused) {
            if (!fromTick || now - m_resumedAt < kFallbackSeconds) return Action::None;
            m_wokeWithoutFocus = true;
        }
        m_awake = true;
        return Action::Wake;
    }

    bool m_resumed{false};
    bool m_window{false};
    bool m_focused{false};
    bool m_awake{true};   // a stream is open and running until a pause says otherwise
    bool m_wokeWithoutFocus{false};
    double m_resumedAt{0.0};
};

} // namespace Supersonic
