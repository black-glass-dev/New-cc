#pragma once

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace owncut::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int register_test(const char* name, std::function<void()> fn) {
    registry().push_back(TestCase{name, std::move(fn)});
    return 0;
}

template <typename T>
inline void format_value(std::ostringstream& os, const T& v) {
    if constexpr (requires { os << v; }) {
        os << v;
    } else if constexpr (std::is_enum_v<T>) {
        os << static_cast<std::underlying_type_t<T>>(v);
    } else {
        os << "<value>";
    }
}

inline void fail(const char* expr, const char* file, int line, const std::string& extra = "") {
    std::ostringstream oss;
    oss << file << ":" << line << " assertion failed: " << expr;
    if (!extra.empty()) oss << " (" << extra << ")";
    throw std::runtime_error(oss.str());
}

inline int run_all(const char* suite_name) {
    int passed = 0;
    int failed = 0;
    std::cout << "=== Running Suite: " << suite_name << " (" << registry().size() << " tests) ===\n";
    for (const auto& tc : registry()) {
        try {
            tc.fn();
            ++passed;
            std::cout << "  [PASS] " << tc.name << "\n";
        } catch (const std::exception& ex) {
            ++failed;
            std::cerr << "  [FAIL] " << tc.name << " -> " << ex.what() << "\n";
        } catch (...) {
            ++failed;
            std::cerr << "  [FAIL] " << tc.name << " -> unknown exception\n";
        }
    }
    std::cout << "Summary [" << suite_name << "]: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

} // namespace owncut::test

#define OCU_TEST(name)                                                               \
    static void ocu_test_fn_##name();                                                \
    static const int ocu_test_reg_##name =                                           \
        ::owncut::test::register_test(#name, ocu_test_fn_##name);                    \
    static void ocu_test_fn_##name()

#define OCU_ASSERT_TRUE(cond)                                                        \
    do {                                                                             \
        if (!(cond)) ::owncut::test::fail(#cond, __FILE__, __LINE__);                \
    } while (0)

#define OCU_ASSERT_EQ(a, b)                                                          \
    do {                                                                             \
        const auto& ocu_a = (a);                                                     \
        const auto& ocu_b = (b);                                                     \
        if (!(ocu_a == ocu_b)) {                                                     \
            std::ostringstream ocu_ss;                                               \
            ::owncut::test::format_value(ocu_ss, ocu_a);                             \
            ocu_ss << " != ";                                                        \
            ::owncut::test::format_value(ocu_ss, ocu_b);                             \
            ::owncut::test::fail(#a " == " #b, __FILE__, __LINE__, ocu_ss.str());    \
        }                                                                            \
    } while (0)

#define OCU_ASSERT_NEAR(a, b, eps)                                                   \
    do {                                                                             \
        const double ocu_da = static_cast<double>(a);                                \
        const double ocu_db = static_cast<double>(b);                                \
        if (std::abs(ocu_da - ocu_db) > static_cast<double>(eps)) {                  \
            std::ostringstream ocu_ss;                                               \
            ocu_ss << ocu_da << " vs " << ocu_db << " (tol " << (eps) << ")";        \
            ::owncut::test::fail(#a " ~= " #b, __FILE__, __LINE__, ocu_ss.str());    \
        }                                                                            \
    } while (0)
