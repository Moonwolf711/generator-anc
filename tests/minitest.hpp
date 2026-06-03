// minitest.hpp -- zero-dependency test harness (own IP, no GoogleTest needed).
#ifndef MINITEST_HPP
#define MINITEST_HPP

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace minitest {

inline std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> r;
    return r;
}
inline int& failures() {
    static int f = 0;
    return f;
}
struct Reg {
    Reg(const std::string& name, std::function<void()> fn) { registry().emplace_back(name, std::move(fn)); }
};

inline void check(bool cond, const char* expr, const char* file, int line) {
    if (!cond) {
        ++failures();
        std::printf("    FAIL %s:%d: %s\n", file, line, expr);
    }
}
inline void check_near(double a, double b, double tol, const char* expr, const char* file, int line) {
    if (std::fabs(a - b) > tol) {
        ++failures();
        std::printf("    FAIL %s:%d: %s  (|%g - %g| = %g > %g)\n", file, line, expr, a, b, std::fabs(a - b), tol);
    }
}

inline int run() {
    int passed = 0;
    for (auto& t : registry()) {
        const int before = failures();
        std::printf("[ RUN  ] %s\n", t.first.c_str());
        t.second();
        if (failures() == before) {
            ++passed;
            std::printf("[  OK  ] %s\n", t.first.c_str());
        } else {
            std::printf("[ FAIL ] %s\n", t.first.c_str());
        }
    }
    std::printf("\n%d/%zu tests passed, %d checks failed\n",
                passed, registry().size(), failures());
    return failures() ? 1 : 0;
}

}  // namespace minitest

#define TEST(name)                                                    \
    static void name();                                               \
    static minitest::Reg minitest_reg_##name(#name, name);            \
    static void name()
#define CHECK(c) minitest::check((c), #c, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) minitest::check_near((a), (b), (tol), #a " ~= " #b, __FILE__, __LINE__)
#define CHECK_GE(a, b) minitest::check((a) >= (b), #a " >= " #b, __FILE__, __LINE__)
#define CHECK_LT(a, b) minitest::check((a) < (b), #a " < " #b, __FILE__, __LINE__)

#endif  // MINITEST_HPP
