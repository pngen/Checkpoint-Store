#ifndef CPSTEST_TEST_UTIL_HPP
#define CPSTEST_TEST_UTIL_HPP

#include <iostream>
#include <string>

namespace cpstest {
inline int g_failures = 0;
inline void check(bool cond, const char* msg, const char* file, int line) {
    if (!cond) {
        std::cout << "FAIL(" << file << ":" << line << "): " << msg << "\n";
        ++g_failures;
    }
}
inline int failures() { return g_failures; }
inline int finish(const char* name) {
    if (g_failures == 0) {
        std::cout << name << ": PASS\n";
    } else {
        std::cout << name << ": FAIL (" << g_failures << ")\n";
    }
    return g_failures == 0 ? 0 : 1;
}
}  // namespace cpstest

#define CHECK(cond) cpstest::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_MSG(cond, msg) cpstest::check((cond), msg, __FILE__, __LINE__)
#define CHECK_EQ(a, b) cpstest::check((a) == (b), #a " == " #b, __FILE__, __LINE__)

#endif
