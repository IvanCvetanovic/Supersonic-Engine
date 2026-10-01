// The thin platform layer: what a desktop build answers when asked about a screen it
// does not have.
//
// SafeArea is how a game keeps its buttons clear of a notch, a rounded corner or a home
// indicator, and a game asks it every frame. On the desktop nothing covers the window,
// so the answer must be zero - not undefined, not a stale value from a previous window.
// The native-surface backends (Android, iOS) define their own; those need a device.

#include "TestHarness.hpp"
#include "platform/SafeArea.hpp"

using namespace Supersonic;

static void testADesktopWindowHasNoSafeAreaInsets() {
    const SafeAreaInsets insets = SafeArea::Get();
    CHECK_MSG(insets.IsZero(), "nothing covers a desktop window");
    CHECK(insets.left == 0.0f && insets.top == 0.0f && insets.right == 0.0f && insets.bottom == 0.0f);

    // Asked every frame, and the answer does not drift.
    for (int frame = 0; frame < 100; ++frame) CHECK(SafeArea::Get() == insets);
}

static void testInsetsCompareByValue() {
    SafeAreaInsets a;
    SafeAreaInsets b;
    CHECK(a == b);
    CHECK(a.IsZero());

    a.top = 44.0f;
    CHECK_MSG(!(a == b), "an inset is a difference");
    CHECK_MSG(!a.IsZero(), "and a nonzero one is not zero, whichever edge it is on");

    b.top = 44.0f;
    CHECK(a == b);

    for (int edge = 0; edge < 4; ++edge) {
        SafeAreaInsets one;
        float* sides[4] = {&one.left, &one.top, &one.right, &one.bottom};
        *sides[edge] = 1.0f;
        CHECK_MSG(!one.IsZero(), "any one edge is enough");
    }
}

static void runTests() {
    testADesktopWindowHasNoSafeAreaInsets();
    testInsetsCompareByValue();
}

TEST_MAIN("test_platform", 100)
