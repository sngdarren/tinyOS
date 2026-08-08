#pragma once

// Minimal test framework -- no GoogleTest dependency, no fetch step.
//
//   TEST(name) { ... CHECK(cond); ... }
//   int main() { return tinyos::test::run_all(); }

#include <cstdio>
#include <string>
#include <vector>

namespace tinyos::test {

struct Case {
    char const* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int count = 0;
    return count;
}

inline bool& current_failed() {
    static bool failed = false;
    return failed;
}

struct Registrar {
    Registrar(char const* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void report_failure(char const* file, int line, char const* expr, std::string const& detail) {
    std::printf("    FAIL %s:%d\n         %s\n", file, line, expr);
    if (!detail.empty()) {
        std::printf("         %s\n", detail.c_str());
    }
    current_failed() = true;
    ++failures();
}

inline int run_all() {
    int passed = 0;
    for (auto const& c : registry()) {
        current_failed() = false;
        int const before = failures();
        c.fn();
        if (failures() == before) {
            std::printf("  ok   %s\n", c.name);
            ++passed;
        } else {
            std::printf("  FAIL %s\n", c.name);
        }
    }
    std::printf("\n%d/%zu passed\n", passed, registry().size());
    return failures() == 0 ? 0 : 1;
}

}  // namespace tinyos::test

#define TINYOS_CONCAT_(a, b) a##b
#define TINYOS_CONCAT(a, b) TINYOS_CONCAT_(a, b)

#define TEST(name)                                                       \
    static void TINYOS_CONCAT(test_fn_, __LINE__)();                     \
    static ::tinyos::test::Registrar TINYOS_CONCAT(test_reg_, __LINE__)( \
        name, &TINYOS_CONCAT(test_fn_, __LINE__));                       \
    static void TINYOS_CONCAT(test_fn_, __LINE__)()

#define CHECK(expr)                                                     \
    do {                                                                \
        if (!(expr)) {                                                  \
            ::tinyos::test::report_failure(__FILE__, __LINE__, #expr, {}); \
        }                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                          \
    do {                                                                        \
        auto const lhs_ = (a);                                                  \
        auto const rhs_ = (b);                                                  \
        if (!(lhs_ == rhs_)) {                                                  \
            ::tinyos::test::report_failure(__FILE__, __LINE__, #a " == " #b,    \
                                           "got " + std::to_string(lhs_) +      \
                                               ", expected " + std::to_string(rhs_)); \
        }                                                                       \
    } while (0)

// Stops a test after a fatal precondition instead of cascading failures.
#define REQUIRE(expr)                                                      \
    do {                                                                   \
        if (!(expr)) {                                                     \
            ::tinyos::test::report_failure(__FILE__, __LINE__, #expr, "required"); \
            return;                                                        \
        }                                                                  \
    } while (0)