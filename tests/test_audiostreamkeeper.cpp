// What to do with a phone's output stream when it is closed, lost, reopened or refused
// (core/AudioStreamKeeper.hpp, the policy behind the AAudio backend in AudioEngine.cpp).
//
// "When I minimise the app and come back to it, it loses the sound" (2026-11-08). No cause was
// confirmed and nothing was heard on a device; the candidates include a stream that died with
// nothing to reopen it. The AAudio calls themselves cannot run here, so the stream is a fake that
// records every call and fails when told to; what is pinned is the policy, not the sound.

#include "TestHarness.hpp"
#include "core/AudioStreamKeeper.hpp"

#include <cstddef>
#include <set>
#include <string>
#include <vector>

using namespace Supersonic;

namespace {

// A stream whose every open makes a stream of a NEW id, and which remembers the ones that were closed:
// a keeper that ever shows a closed id as its current one has reused it.
struct FakeStream : AudioStreamKeeper::Ops {
    double* clock{nullptr};
    int nextId{1};
    int current{0};               // 0: no stream
    bool disconnected{false};     // the open stream's client state
    int failNext{0};              // the next N opens fail
    bool failAlways{false};
    bool openedStreamIsDead{false};   // an open that "works" and gives a stream that is disconnected at once

    int opens{0};                 // attempts, failed ones included
    int closes{0};
    int openedWhileOpen{0};       // an open asked for with a stream already open: a leaked stream
    std::vector<double> attemptTimes;
    std::set<int> closedIds;

    bool OpenStream(std::string& error) override {
        ++opens;
        if (clock != nullptr) attemptTimes.push_back(*clock);
        if (current != 0) ++openedWhileOpen;
        if (failAlways || failNext > 0) {
            if (failNext > 0) --failNext;
            error = "AAudioStreamBuilder_openStream failed: scripted";
            return false;
        }
        current = nextId++;
        disconnected = openedStreamIsDead;
        return true;
    }
    void CloseStream() override {
        ++closes;
        closedIds.insert(current);
        current = 0;
        disconnected = false;
    }
    bool HasStream() const override { return current != 0; }
    bool StreamDisconnected() const override { return disconnected; }

    bool reusedAClosedStream() const { return current != 0 && closedIds.count(current) != 0; }
};

// The state the AudioEngine constructor leaves: one stream, opened and running, and a keeper over it.
struct Rig {
    double clock{0.0};
    FakeStream stream;
    AudioStreamKeeper keeper{stream};
    Rig() {
        stream.clock = &clock;
        std::string error;
        stream.OpenStream(error);
        stream.attemptTimes.clear();
        stream.opens = 0;
    }
    // Frames at 60 Hz from now to `until`, Maintain each.
    void frames(double until) {
        for (; clock < until; clock += 1.0 / 64.0) keeper.Maintain(clock);   // 1/64: exactly representable
    }
};

void testASuspendClosesTheStream() {
    Rig rig;
    const int first = rig.stream.current;
    const auto report = rig.keeper.Suspend(1.0);
    CHECK(report.closed);
    CHECK(!report.tried);
    CHECK(rig.stream.closes == 1);
    CHECK(!rig.stream.HasStream());
    CHECK(rig.keeper.Suspended());
    CHECK(rig.stream.closedIds.count(first) == 1);
}

void testAResumeOpensANewStreamAndNeverReusesTheOldOne() {
    Rig rig;
    const int first = rig.stream.current;
    rig.keeper.Suspend(1.0);
    const auto report = rig.keeper.Resume(5.0);
    CHECK(report.tried && report.opened);
    CHECK(rig.stream.opens == 1);
    CHECK(rig.stream.HasStream());
    CHECK(rig.stream.current != first);
    CHECK(!rig.stream.reusedAClosedStream());
    CHECK(rig.stream.openedWhileOpen == 0);
    CHECK(!rig.keeper.Suspended());

    // Another minimise: a third stream, again not any earlier one.
    const int second = rig.stream.current;
    rig.keeper.Suspend(9.0);
    rig.keeper.Resume(12.0);
    CHECK(rig.stream.current != first && rig.stream.current != second);
    CHECK(!rig.stream.reusedAClosedStream());
    CHECK(rig.stream.closes == 2);
}

void testAResumeThatFindsTheStreamRunningOpensNothing() {
    // No pause came before it (the first launch's RESUME, or a handler that is told twice).
    Rig rig;
    const int first = rig.stream.current;
    const auto report = rig.keeper.Resume(2.0);
    CHECK(!report.tried && !report.closed);
    CHECK(rig.stream.opens == 0);
    CHECK(rig.stream.closes == 0);
    CHECK(rig.stream.current == first);

    // And a repeated resume after a real one.
    rig.keeper.Suspend(3.0);
    rig.keeper.Resume(4.0);
    const int reopened = rig.stream.current;
    CHECK(rig.stream.opens == 1);
    const auto again = rig.keeper.Resume(4.1);
    CHECK(!again.tried && !again.closed);
    CHECK(rig.stream.opens == 1);
    CHECK(rig.stream.current == reopened);
    CHECK(rig.stream.openedWhileOpen == 0);
}

void testAResumeWithNoStreamOpensOne() {
    // An earlier open failed and nothing has retried yet; the next resume is another chance.
    Rig rig;
    rig.keeper.Suspend(1.0);
    rig.stream.failNext = 1;
    CHECK(!rig.keeper.Resume(2.0).opened);
    CHECK(!rig.stream.HasStream());
    CHECK(rig.keeper.Resume(2.1).opened);   // not suspended any more, no stream: it opens one
    CHECK(rig.stream.HasStream());
    CHECK(rig.stream.opens == 2);
    CHECK(rig.stream.openedWhileOpen == 0);

    // A keeper that never had a stream (the engine's first open failed and it came up unavailable).
    Rig bare;
    bare.stream.CloseStream();
    CHECK(bare.keeper.Resume(1.0).opened);
    CHECK(bare.stream.HasStream());
}

void testAFailedOpenIsRetriedOnTheDoublingSchedule() {
    Rig rig;
    rig.keeper.Suspend(100.0);
    rig.clock = 100.0;
    rig.stream.failAlways = true;
    const auto report = rig.keeper.Resume(rig.clock);
    CHECK(!report.opened && !report.error.empty());
    CHECK(report.retryAfter == 1.0);
    rig.clock += 1.0 / 64.0;
    rig.frames(100.0 + 200.0);

    // The attempt at the resume, then 1, 2, 4, 8, 16, 30, 30 seconds apart.
    const std::vector<double> gaps = {1.0, 2.0, 4.0, 8.0, 16.0, 30.0, 30.0};
    const auto& times = rig.stream.attemptTimes;
    CHECK(times.size() >= gaps.size() + 1);
    CHECK(times.front() == 100.0);
    for (std::size_t i = 0; i < gaps.size() && i + 1 < times.size(); ++i) {
        CHECK_MSG(times[i + 1] - times[i] == gaps[i],
                  "attempt " + std::to_string(i + 1) + " came " + std::to_string(times[i + 1] - times[i]) +
                      " s after the one before; expected " + std::to_string(gaps[i]));
    }
    CHECK(!rig.stream.HasStream());
    CHECK(rig.stream.openedWhileOpen == 0);
}

void testASuccessEndsTheRetriesAndPutsTheWaitBackToOneSecond() {
    Rig rig;
    rig.keeper.Suspend(10.0);
    rig.clock = 10.0;
    rig.stream.failNext = 3;
    rig.keeper.Resume(rig.clock);   // fails: next try in 1 s, then 2, then 4
    rig.frames(10.0 + 30.0);
    CHECK(rig.stream.HasStream());
    CHECK(rig.stream.opens == 4);   // the resume and three retries; the fourth works
    CHECK(rig.stream.attemptTimes.size() == 4);
    const int survivor = rig.stream.current;
    rig.frames(rig.clock + 120.0);   // a healthy stream is left alone
    CHECK(rig.stream.opens == 4);
    CHECK(rig.stream.current == survivor);

    // The stream dies; the new attempt fails once. The wait is 1 s again, not 8.
    rig.stream.disconnected = true;
    rig.stream.failNext = 1;
    const double diedAt = rig.clock;
    rig.frames(diedAt + 10.0);
    CHECK(rig.stream.HasStream());
    CHECK(rig.stream.opens == 6);
    const auto& times = rig.stream.attemptTimes;
    CHECK(times.size() == 6);
    CHECK(times[5] - times[4] == 1.0);
}

void testADisconnectedStreamIsReopened() {
    Rig rig;
    const int first = rig.stream.current;
    rig.stream.disconnected = true;   // the disconnect callback was dropped, or the stream died with no callback
    rig.clock = 50.0;
    const auto report = rig.keeper.Maintain(rig.clock);
    CHECK(report.closed && report.tried && report.opened);
    CHECK(rig.stream.current != first);
    CHECK(!rig.stream.reusedAClosedStream());
    CHECK(rig.stream.openedWhileOpen == 0);
    CHECK(!rig.stream.disconnected);
}

void testAHealthyStreamIsLeftAlone() {
    Rig rig;
    rig.frames(30.0);
    CHECK(rig.stream.opens == 0);
    CHECK(rig.stream.closes == 0);
}

void testAStreamThatDiesAtOnceIsNotReopenedEveryFrame() {
    Rig rig;
    rig.stream.openedStreamIsDead = true;
    rig.stream.disconnected = true;
    rig.frames(10.0);   // 640 frames
    CHECK(rig.stream.opens >= 9);
    CHECK(rig.stream.opens <= 11);
    const auto& times = rig.stream.attemptTimes;
    for (std::size_t i = 1; i < times.size(); ++i) CHECK(times[i] - times[i - 1] >= 1.0);
    CHECK(rig.stream.openedWhileOpen == 0);
}

void testNothingOpensWhileSuspended() {
    Rig rig;
    rig.keeper.Suspend(1.0);
    rig.clock = 1.0;
    rig.frames(120.0);
    CHECK(rig.stream.opens == 0);

    // The device goes away in the background: the reopen opens nothing.
    const auto report = rig.keeper.Reopen(rig.clock);
    CHECK(!report.tried);
    CHECK(rig.stream.opens == 0);
    CHECK(!rig.stream.HasStream());

    // A retry that was pending when the app left does not fire in the background.
    Rig failing;
    failing.stream.failAlways = true;
    failing.keeper.Suspend(1.0);
    failing.clock = 1.0;
    failing.keeper.Resume(failing.clock);   // fails, retry due at 2.0
    failing.keeper.Suspend(1.5);
    failing.clock = 1.5;
    const int before = failing.stream.opens;
    failing.frames(60.0);
    CHECK(failing.stream.opens == before);

    // And the return after all that opens at once, not at the old schedule.
    failing.stream.failAlways = false;
    CHECK(failing.keeper.Resume(failing.clock).opened);
}

void testTheDeviceGoingAwayOpensANewStreamAtOnce() {
    Rig rig;
    const int first = rig.stream.current;
    rig.clock = 20.0;
    const auto report = rig.keeper.Reopen(rig.clock);
    CHECK(report.closed && report.opened);
    CHECK(rig.stream.current != first);
    CHECK(!rig.stream.reusedAClosedStream());
    CHECK(rig.stream.openedWhileOpen == 0);

    // The new device refuses the stream: Maintain picks it up on the schedule.
    rig.stream.failAlways = true;
    CHECK(!rig.keeper.Reopen(rig.clock).opened);
    CHECK(!rig.stream.HasStream());
    rig.stream.failAlways = false;
    rig.frames(rig.clock + 5.0);
    CHECK(rig.stream.HasStream());
}

} // namespace

int main() {
    testASuspendClosesTheStream();
    testAResumeOpensANewStreamAndNeverReusesTheOldOne();
    testAResumeThatFindsTheStreamRunningOpensNothing();
    testAResumeWithNoStreamOpensOne();
    testAFailedOpenIsRetriedOnTheDoublingSchedule();
    testASuccessEndsTheRetriesAndPutsTheWaitBackToOneSecond();
    testADisconnectedStreamIsReopened();
    testAHealthyStreamIsLeftAlone();
    testAStreamThatDiesAtOnceIsNotReopenedEveryFrame();
    testNothingOpensWhileSuspended();
    testTheDeviceGoingAwayOpensANewStreamAtOnce();
    return test::summary("test_audiostreamkeeper", 70);
}
