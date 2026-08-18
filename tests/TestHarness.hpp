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

inline int summary(const char* suite) {
    std::printf("%s: %d checks, %d failures\n", suite, g_checks, g_failures);
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

#define TEST_MAIN(suite) \
    int main() { runTests(); return ::test::summary(suite); }
