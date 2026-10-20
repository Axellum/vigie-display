// Minimal checks for the PC tests of firmware/core (no framework, works with NDEBUG).
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>

inline int& test_failures() {
    static int failures = 0;
    return failures;
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++test_failures();                                               \
        }                                                                    \
    } while (0)

#define CHECK_STR(actual, expected)                                                                   \
    do {                                                                                              \
        const char* a_ = (actual);                                                                    \
        const char* e_ = (expected);                                                                  \
        if (std::strcmp(a_, e_) != 0) {                                                               \
            std::printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, a_, e_);               \
            ++test_failures();                                                                        \
        }                                                                                             \
    } while (0)

inline int test_report(const char* name) {
    if (test_failures() != 0) {
        std::printf("%s: %d failure(s)\n", name, test_failures());
        return EXIT_FAILURE;
    }
    std::printf("OK: %s\n", name);
    return EXIT_SUCCESS;
}
