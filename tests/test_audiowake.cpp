// When a phone's sound stops and starts again (platform/AudioWake.hpp).
//
// "When I minimise the app and come back to it, it loses the sound" (2026-11-08, an Android 17 phone).
// No cause was confirmed, and nothing was heard on any device: the stream's pause and restart was
// only ever read through `dumpsys audio` on an emulator run with -no-audio. One candidate is that a
// start made while the activity is not yet counted as visible is silenced by the system without an
// error. So the policy under test is the defensive one: the stream closes at PAUSE, and a NEW one is
// opened only when the app is certainly in front, never at RESUME or INIT_WINDOW.
//
// The fixture is the order an API 33 emulator logged (planning step 204's lifecycle log, not a phone's):
//   PAUSE, LOST_FOCUS (+48 ms), TERM_WINDOW (+362), STOP (+740), START, RESUME,
//   INIT_WINDOW (RESUME +49 ms), WINDOW_RESIZED (+106), GAINED_FOCUS (+471).
// This suite cannot say whether the sound is HEARD; it pins when the engine asks for a stream.

#include "TestHarness.hpp"
#include "platform/AudioWake.hpp"

#include <cstddef>
#include <utility>
#include <vector>

using namespace Supersonic;
using Action = AudioWake::Action;

namespace {

// A device's lifecycle, with the game's frame ticking at 60 Hz whenever the app is resumed with a window
// (AndroidApp.cpp's WaitUntilDrawable runs once a frame, and only then).
struct Phone {
    AudioWake wake;
    std::vector<std::pair<double, Action>> actions;   // every action that was not None, with its time
    bool resumed{false};
    bool window{false};
    double clock{0.0};

    void note(Action action) {
        if (action != Action::None) actions.emplace_back(clock, action);
    }

    int count(Action action) const {
        int n = 0;
        for (const auto& entry : actions) n += entry.second == action ? 1 : 0;
        return n;
    }

    // The action an event returned, noted.
    Action resume(double at) {
        clock = at;
        resumed = true;
        const Action action = wake.OnResume(at);
        note(action);
        return action;
    }
    Action pause(double at) {
        clock = at;
        resumed = false;
        const Action action = wake.OnPause();
        note(action);
        return action;
    }
    Action windowUp(double at, bool present) {
        clock = at;
        window = present;
        const Action action = wake.OnWindow(present);
        note(action);
        return action;
    }
    Action focus(double at, bool focused) {
        clock = at;
        const Action action = wake.OnFocus(focused);
        note(action);
        return action;
    }
    // Frames from `from` up to (not including) `to`.
    void frames(double from, double to) {
        for (double t = from; t < to; t += 1.0 / 60.0) {
            clock = t;
            if (resumed && window) note(wake.Tick(t));
        }
    }
};

// The lifecycle of the log up to INIT_WINDOW: pause, the focus and the window going, three seconds away, then
// RESUME and the window back. Returns the time of RESUME. What follows (the frames, GAINED_FOCUS or not) is the
// caller's, since that is what each case varies.
double minimiseAndReturn(Phone& phone, double pauseAt) {
    phone.pause(pauseAt);
    phone.focus(pauseAt + 0.048, false);
    phone.windowUp(pauseAt + 0.362, false);
    // STOP, START: no audio meaning.
    const double resumeAt = pauseAt + 3.896;
    CHECK(phone.resume(resumeAt) == Action::None);
    CHECK(phone.windowUp(resumeAt + 0.049, true) == Action::None);
    return resumeAt;
}

// A running, focused app: the first launch, then one minimise.
void startRunning(Phone& phone) {
    phone.resume(0.0);
    phone.windowUp(0.05, true);
    phone.focus(0.5, true);
    // The audio engine comes up and installs its handler: running.
    CHECK(phone.wake.OnHandlerInstalled(true) == Action::None);
    phone.frames(0.5, 5.0);
}

void testTheRealOrderWakesOnceAtFocusAndNeverBefore() {
    Phone phone;
    startRunning(phone);
    CHECK(phone.actions.empty());

    const double pauseAt = 10.0;
    phone.pause(pauseAt);
    CHECK(phone.count(Action::Suspend) == 1);
    CHECK(!phone.wake.Awake());

    phone.focus(pauseAt + 0.048, false);
    phone.windowUp(pauseAt + 0.362, false);
    const double resumeAt = pauseAt + 3.896;
    CHECK(phone.resume(resumeAt) == Action::None);
    CHECK(!phone.wake.Awake());   // the sound does not start at RESUME

    CHECK(phone.windowUp(resumeAt + 0.049, true) == Action::None);
    CHECK(!phone.wake.Awake());   // nor at INIT_WINDOW

    // The frames the game runs between INIT_WINDOW and GAINED_FOCUS (WINDOW_RESIZED at +106 ms changes nothing).
    phone.frames(resumeAt + 0.049, resumeAt + 0.471);
    CHECK(phone.count(Action::Wake) == 0);
    CHECK(!phone.wake.Awake());

    CHECK(phone.focus(resumeAt + 0.471, true) == Action::Wake);
    CHECK(phone.wake.Awake());

    // And nothing more, over ten seconds of frames and a repeated GAINED_FOCUS.
    phone.frames(resumeAt + 0.471, resumeAt + 10.0);
    CHECK(phone.focus(resumeAt + 10.0, true) == Action::None);
    CHECK(phone.count(Action::Wake) == 1);
    CHECK(phone.count(Action::Suspend) == 1);
    CHECK(phone.count(Action::Rewake) == 0);
    CHECK(phone.actions.size() == 2);
    CHECK(phone.actions[1].first >= resumeAt + 0.471 - 1e-9);
}

void testAPhoneThatNeverSendsFocusIsWokenAboutASecondLater() {
    Phone phone;
    startRunning(phone);
    const double resumeAt = minimiseAndReturn(phone, 10.0);
    phone.frames(resumeAt + 0.471, resumeAt + 0.9);
    CHECK(phone.count(Action::Wake) == 0);   // not yet: the fallback is a second, not a few frames

    phone.frames(resumeAt + 0.9, resumeAt + 8.0);
    CHECK(phone.count(Action::Wake) == 1);
    const double wokeAt = phone.actions.back().first;
    CHECK(phone.actions.back().second == Action::Wake);
    CHECK(wokeAt >= resumeAt + AudioWake::kFallbackSeconds - 1e-9);
    CHECK(wokeAt <= resumeAt + AudioWake::kFallbackSeconds + 0.05);   // on the first frame past the second
    CHECK(phone.count(Action::Suspend) == 1);
    CHECK(phone.actions.size() == 2);
}

void testAFingerOnTheWindowIsFocusEnough() {
    // AndroidApp.cpp's touch heal reports a finger as a focus gain. The first one wakes; the real
    // GAINED_FOCUS that may follow is not a second reason to reopen.
    Phone phone;
    startRunning(phone);
    const double resumeAt = minimiseAndReturn(phone, 10.0);
    phone.frames(resumeAt + 0.471, resumeAt + 0.6);
    CHECK(phone.focus(resumeAt + 0.6, true) == Action::Wake);
    CHECK(phone.focus(resumeAt + 0.7, true) == Action::None);
    phone.frames(resumeAt + 0.7, resumeAt + 4.0);
    CHECK(phone.count(Action::Wake) == 1);
    CHECK(phone.count(Action::Rewake) == 0);
}

void testFocusThatArrivesAfterTheFallbackOpensTheStreamAgain() {
    // The fallback woke the sound without focus. A stream started then is exactly the one that cannot
    // be trusted (the candidate this exists for), so focus arriving afterwards asks for a fresh one,
    // once.
    Phone phone;
    startRunning(phone);
    const double resumeAt = minimiseAndReturn(phone, 10.0);
    phone.frames(resumeAt + 0.471, resumeAt + 1.5);
    CHECK(phone.count(Action::Wake) == 1);

    CHECK(phone.focus(resumeAt + 1.5, true) == Action::Rewake);
    CHECK(phone.focus(resumeAt + 1.6, true) == Action::None);
    CHECK(phone.focus(resumeAt + 1.7, false) == Action::None);
    CHECK(phone.focus(resumeAt + 1.8, true) == Action::None);   // a later focus gain is not another rewake
    phone.frames(resumeAt + 1.8, resumeAt + 6.0);
    CHECK(phone.count(Action::Wake) == 1);
    CHECK(phone.count(Action::Rewake) == 1);
}

void testAPauseEndsWhateverTheLastResumeWasWaitingFor() {
    Phone phone;
    startRunning(phone);
    // The fallback wake, then pause, resume, and a focus gain: that one is a plain wake, not a rewake.
    const double first = minimiseAndReturn(phone, 10.0);
    phone.frames(first + 0.471, first + 2.0);
    CHECK(phone.count(Action::Wake) == 1);

    phone.pause(first + 2.5);
    phone.windowUp(first + 2.6, false);
    CHECK(phone.count(Action::Suspend) == 2);
    CHECK(phone.resume(first + 6.0) == Action::None);
    phone.windowUp(first + 6.05, true);
    CHECK(phone.focus(first + 6.4, true) == Action::Wake);
    CHECK(phone.count(Action::Rewake) == 0);
    // The fallback's wake of the first return left nothing pending: this one had focus.
    CHECK(phone.focus(first + 6.5, true) == Action::None);
    CHECK(phone.count(Action::Rewake) == 0);
}

void testASecondPauseIsNotAnotherSuspend() {
    Phone phone;
    startRunning(phone);
    CHECK(phone.pause(10.0) == Action::Suspend);
    CHECK(phone.pause(10.01) == Action::None);
    CHECK(phone.focus(10.05, false) == Action::None);
    CHECK(phone.windowUp(10.3, false) == Action::None);
    CHECK(phone.pause(10.7) == Action::None);
    CHECK(phone.count(Action::Suspend) == 1);
}

void testAFocusGainBeforeTheWindowWakesOnTheNextTick() {
    Phone phone;
    startRunning(phone);
    phone.pause(10.0);
    phone.windowUp(10.3, false);
    CHECK(phone.resume(13.0) == Action::None);
    CHECK(phone.focus(13.02, true) == Action::None);   // no window to be in front of yet
    CHECK(!phone.wake.Awake());
    CHECK(phone.windowUp(13.04, true) == Action::None);   // the window is not a reason either
    CHECK(!phone.wake.Awake());
    CHECK(phone.wake.Tick(13.05) == Action::Wake);
    CHECK(phone.wake.Tick(13.07) == Action::None);
    CHECK(phone.wake.Awake());
}

void testAPauseBeforeTheFallbackFiresCancelsIt() {
    Phone phone;
    startRunning(phone);
    phone.pause(10.0);
    phone.windowUp(10.3, false);
    phone.resume(13.0);
    phone.windowUp(13.05, true);
    phone.frames(13.05, 13.9);   // 0.9 s: not yet
    CHECK(phone.count(Action::Wake) == 0);

    CHECK(phone.pause(13.9) == Action::None);   // already suspended: nothing to do
    // The app is away for a long time. A frame that somehow still ticks (the grace after a pause)
    // must not wake it: it is not resumed.
    CHECK(phone.wake.Tick(14.5) == Action::None);
    CHECK(phone.wake.Tick(30.0) == Action::None);
    CHECK(phone.count(Action::Wake) == 0);

    // The next return counts its second from ITS resume, not from the first one's.
    phone.resume(40.0);
    phone.windowUp(40.01, true);
    CHECK(phone.wake.Tick(40.9) == Action::None);
    CHECK(phone.wake.Tick(41.0) == Action::Wake);
}

void testAFirstLaunchNeverSuspendsOrWakes() {
    // No pause before the resume: nothing to wake, and no stream is opened over the running one.
    Phone phone;
    CHECK(phone.resume(0.0) == Action::None);
    CHECK(phone.windowUp(0.05, true) == Action::None);
    CHECK(phone.wake.OnHandlerInstalled(true) == Action::None);
    phone.frames(0.1, 5.0);
    CHECK(phone.focus(5.0, true) == Action::None);
    CHECK(phone.actions.empty());
    // Focus lost and gained (a notification shade) while the app is in front: the stream is not touched.
    CHECK(phone.focus(6.0, false) == Action::None);
    CHECK(phone.focus(7.0, true) == Action::None);
    phone.frames(7.0, 12.0);
    CHECK(phone.actions.empty());
}

void testAHandlerInstalledInTheBackgroundStartsSuspended() {
    Phone phone;
    CHECK(phone.wake.OnHandlerInstalled(false) == Action::Suspend);
    CHECK(!phone.wake.Awake());
    phone.resume(5.0);
    phone.windowUp(5.05, true);
    CHECK(phone.focus(5.4, true) == Action::Wake);
}

} // namespace

int main() {
    testTheRealOrderWakesOnceAtFocusAndNeverBefore();
    testAPhoneThatNeverSendsFocusIsWokenAboutASecondLater();
    testAFingerOnTheWindowIsFocusEnough();
    testFocusThatArrivesAfterTheFallbackOpensTheStreamAgain();
    testAPauseEndsWhateverTheLastResumeWasWaitingFor();
    testASecondPauseIsNotAnotherSuspend();
    testAFocusGainBeforeTheWindowWakesOnTheNextTick();
    testAPauseBeforeTheFallbackFiresCancelsIt();
    testAFirstLaunchNeverSuspendsOrWakes();
    testAHandlerInstalledInTheBackgroundStartsSuspended();
    return test::summary("test_audiowake", 60);
}
