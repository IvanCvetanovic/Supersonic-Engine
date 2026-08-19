#pragma once

// A deliberately tiny, dependency-free assertion harness.
//
// The engine vendors no test framework, and pulling one in for a handful of
// pure-logic checks would cost more than it returns. Each test binary is a
// plain executable that returns non-zero on failure, which is all CTest needs.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace test {

inline int g_failures = 0;
inline int g_checks = 0;

inline void report(bool ok, const char* expr, const char* file, int line, const std::string& extra) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("  FAIL %s:%d\n    %s\n", file, line, expr);
    if (!extra.empty()) std::printf("    %s\n", extra.c_str());
}

inline bool nearly(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

// minChecks is the floor below which "no failures" stops meaning anything.
//
// g_checks was tracked and never used, so a suite that ran zero assertions
// exited 0 and CTest reported it green. That is not hypothetical: test_gltf
// skipped five of its seven cases when its fixture was not reachable from the
// working directory, printed a note, and passed. The floor turns a suite that
// quietly stopped testing into a red run, which is the only way anyone finds
// out. It is set per suite well below the real count, so adding or removing a
// handful of checks does not trip it - only a wholesale skip does.
inline int summary(const char* suite, int minChecks) {
    std::printf("%s: %d checks, %d failures\n", suite, g_checks, g_failures);
    if (g_checks < minChecks) {
        std::printf("  FAIL %s ran %d checks, expected at least %d - the suite skipped work\n",
                    suite, g_checks, minChecks);
        return 1;
    }
    return g_failures == 0 ? 0 : 1;
}

} // namespace test

#define CHECK(expr) \
    ::test::report((expr), #expr, __FILE__, __LINE__, {})

#define CHECK_MSG(expr, msg) \
    ::test::report((expr), #expr, __FILE__, __LINE__, (msg))

// Both arguments are evaluated exactly once.
//
// These used to mention each argument twice - once to compare, once to build
// the failure message - and the order of the two is unspecified. A call with a
// side effect therefore ran twice, in whichever order the compiler chose, and
// the message reported a different evaluation than the one compared. The
// symptom is a test failing with "got 1, expected 1", which sends you looking
// for a bug in the code under test.
#define CHECK_NEAR(a, b) \
    do { \
        const auto _checkA = (a); \
        const auto _checkB = (b); \
        ::test::report(::test::nearly(_checkA, _checkB), #a " ~= " #b, __FILE__, __LINE__, \
            "got " + std::to_string(_checkA) + ", expected " + std::to_string(_checkB)); \
    } while (false)

#define CHECK_EQ(a, b) \
    do { \
        const auto _checkA = (a); \
        const auto _checkB = (b); \
        ::test::report(_checkA == _checkB, #a " == " #b, __FILE__, __LINE__, \
            "got " + std::to_string(_checkA) + ", expected " + std::to_string(_checkB)); \
    } while (false)

// Two arguments, deliberately: every suite must state its floor. A default
// would let a new suite be written without one, which is the case the floor
// exists to catch.
#define TEST_MAIN(suite, minChecks) \
    int main() { runTests(); return ::test::summary(suite, minChecks); }
