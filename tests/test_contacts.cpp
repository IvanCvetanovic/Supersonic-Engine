// Tests for turning a contact list into something gameplay can act on.
//
// PhysicsSystem has always produced contacts. SupersonicApp accumulated them
// and handed them to the editor, which showed a COUNT - and nothing else read
// them. A trigger volume therefore could not fire: the engine knew the player
// had entered it, computed the overlap, and threw the fact away. Non-resolving
// trigger volumes were a feature with no consumer.
//
// The property that needs testing is not "did it see a contact" but the diff.
// A list of what is currently overlapping cannot express "started touching" or
// "stopped touching": a door opening on Enter would re-open every frame, and
// one closing on Exit would never close, because a finished contact is simply
// absent rather than reported as gone.

#include "TestHarness.hpp"
#include "core/ContactTracker.hpp"

#include <vector>

using namespace Supersonic;

namespace {

PhysicsSystem::Contact makeContact(entt::entity a, entt::entity b,
                                   const glm::vec3& normal = glm::vec3(0.0f, 1.0f, 0.0f),
                                   bool isTrigger = false) {
    PhysicsSystem::Contact contact;
    contact.a = a;
    contact.b = b;
    contact.normal = normal;
    contact.isTrigger = isTrigger;
    return contact;
}

int phaseOf(const ContactTracker& tracker, entt::entity self, entt::entity other) {
    const auto* events = tracker.For(self);
    if (!events) return -1;
    for (const auto& event : *events) {
        if (event.other == other) return static_cast<int>(event.phase);
    }
    return -1;
}

} // namespace

static void testFirstTouchIsEnterAndThenStay() {
    entt::registry registry;
    const auto a = registry.create();
    const auto b = registry.create();

    ContactTracker tracker;

    tracker.Update({ makeContact(a, b) });
    CHECK_EQ(phaseOf(tracker, a, b), static_cast<int>(ContactTracker::Phase::Enter));
    CHECK_EQ(phaseOf(tracker, b, a), static_cast<int>(ContactTracker::Phase::Enter));

    // Still touching: Enter must not repeat, or a door opening on Enter would
    // re-open on every frame it stayed open.
    tracker.Update({ makeContact(a, b) });
    CHECK_EQ(phaseOf(tracker, a, b), static_cast<int>(ContactTracker::Phase::Stay));

    tracker.Update({ makeContact(a, b) });
    CHECK_EQ(phaseOf(tracker, a, b), static_cast<int>(ContactTracker::Phase::Stay));
}

static void testSeparationIsReportedOnceAsExit() {
    entt::registry registry;
    const auto a = registry.create();
    const auto b = registry.create();

    ContactTracker tracker;
    tracker.Update({ makeContact(a, b) });

    // The contact is simply ABSENT now. Without the diff there is nothing to
    // read at all, which is why "stopped touching" could not be written.
    tracker.Update({});
    CHECK_EQ(phaseOf(tracker, a, b), static_cast<int>(ContactTracker::Phase::Exit));
    CHECK_EQ(phaseOf(tracker, b, a), static_cast<int>(ContactTracker::Phase::Exit));

    // And exactly once: a second empty frame reports nothing.
    tracker.Update({});
    CHECK_MSG(tracker.CountFor(a) == 0, "Exit must fire once, not every frame afterwards");
}

static void testPairOrderDoesNotRestartTheContact() {
    // The broadphase sorts its proxies on X, so two bodies swapping positions
    // swaps their order in every pair they appear in. If the key were ordered
    // by arrival, that would read as an Exit and an immediate re-Enter.
    entt::registry registry;
    const auto a = registry.create();
    const auto b = registry.create();

    ContactTracker tracker;
    tracker.Update({ makeContact(a, b) });
    CHECK_EQ(phaseOf(tracker, a, b), static_cast<int>(ContactTracker::Phase::Enter));

    tracker.Update({ makeContact(b, a) });   // same pair, other way round
    CHECK_MSG(phaseOf(tracker, a, b) == static_cast<int>(ContactTracker::Phase::Stay),
              "a pair reported in the other order is the same pair");
}

static void testTheNormalPointsAwayFromWhoeverAsks() {
    // The solver's normal points from a toward b. A script reading "which way
    // did I get hit from" needs it pointing away from itself, whichever half of
    // the pair it happens to be.
    entt::registry registry;
    const auto a = registry.create();
    const auto b = registry.create();

    ContactTracker tracker;
    tracker.Update({ makeContact(a, b, glm::vec3(0.0f, 1.0f, 0.0f)) });

    const auto* eventsA = tracker.For(a);
    const auto* eventsB = tracker.For(b);
    CHECK(eventsA != nullptr && eventsB != nullptr);
    if (!eventsA || !eventsB) return;

    CHECK_NEAR((*eventsA)[0].normal.y, 1.0f);
    CHECK_MSG(::test::nearly((*eventsB)[0].normal.y, -1.0f),
              "the second half of the pair must read the flipped normal");
}

static void testRepeatsWithinOneFrameAreOneTouch() {
    // The fixed step can run several times in a frame and the app concatenates
    // each step's contacts, so the same pair legitimately appears more than
    // once. That is one touch continuing, not several.
    entt::registry registry;
    const auto a = registry.create();
    const auto b = registry.create();

    ContactTracker tracker;
    tracker.Update({ makeContact(a, b), makeContact(a, b), makeContact(a, b) });

    CHECK_EQ(tracker.CountFor(a), size_t{1});
    CHECK_EQ(phaseOf(tracker, a, b), static_cast<int>(ContactTracker::Phase::Enter));
}

static void testTriggersAreFlaggedAndStillReported() {
    entt::registry registry;
    const auto player = registry.create();
    const auto volume = registry.create();

    ContactTracker tracker;
    tracker.Update({ makeContact(player, volume, glm::vec3(0.0f, 1.0f, 0.0f), true) });

    const auto* events = tracker.For(player);
    CHECK(events != nullptr);
    if (!events) return;
    CHECK_EQ(events->size(), size_t{1});
    CHECK_MSG((*events)[0].isTrigger, "a trigger overlap must be reported AS a trigger");
}

static void testClearForgetsThePreviousFrame() {
    // Needed on Stop and on scene load. Carrying pairs across would report an
    // Exit for handles that have since been recycled into different entities.
    entt::registry registry;
    const auto a = registry.create();
    const auto b = registry.create();

    ContactTracker tracker;
    tracker.Update({ makeContact(a, b) });
    tracker.Clear();

    CHECK_EQ(tracker.CountFor(a), size_t{0});

    // The next contact is an Enter again, not a Stay.
    tracker.Update({ makeContact(a, b) });
    CHECK_MSG(phaseOf(tracker, a, b) == static_cast<int>(ContactTracker::Phase::Enter),
              "after Clear the next touch must read as a fresh Enter");
}

static void testSeveralPartnersAreAllReported() {
    entt::registry registry;
    const auto centre = registry.create();
    const auto left = registry.create();
    const auto right = registry.create();

    ContactTracker tracker;
    tracker.Update({ makeContact(centre, left), makeContact(centre, right) });

    CHECK_EQ(tracker.CountFor(centre), size_t{2});
    CHECK_EQ(tracker.CountFor(left), size_t{1});
    CHECK_EQ(phaseOf(tracker, centre, left), static_cast<int>(ContactTracker::Phase::Enter));
    CHECK_EQ(phaseOf(tracker, centre, right), static_cast<int>(ContactTracker::Phase::Enter));
}

static void runTests() {
    testFirstTouchIsEnterAndThenStay();
    testSeparationIsReportedOnceAsExit();
    testPairOrderDoesNotRestartTheContact();
    testTheNormalPointsAwayFromWhoeverAsks();
    testRepeatsWithinOneFrameAreOneTouch();
    testTriggersAreFlaggedAndStillReported();
    testClearForgetsThePreviousFrame();
    testSeveralPartnersAreAllReported();
}

TEST_MAIN("test_contacts", 16)
