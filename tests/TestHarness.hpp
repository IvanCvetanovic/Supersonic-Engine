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

#define CHECK_NEAR(a, b) \
    ::test::report(::test::nearly((a), (b)), #a " ~= " #b, __FILE__, __LINE__, \
        "got " + std::to_string(a) + ", expected " + std::to_string(b))

#define CHECK_EQ(a, b) \
    ::test::report((a) == (b), #a " == " #b, __FILE__, __LINE__, \
        "got " + std::to_string(a) + ", expected " + std::to_string(b))

#define TEST_MAIN(suite) \
    int main() { runTests(); return ::test::summary(suite); }
