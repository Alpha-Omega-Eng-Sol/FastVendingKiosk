// Tiny dependency-free test harness, intentionally no fancier than this
// project needs: a list of named cases, each either passes or throws/asserts.
#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct Failure {
    std::string message;
};

inline std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> r;
    return r;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().emplace_back(name, std::move(fn));
    }
};

inline int runAll() {
    int passed = 0, failed = 0;
    for (auto& [name, fn] : registry()) {
        try {
            fn();
            std::cout << "[PASS] " << name << "\n";
            ++passed;
        } catch (const Failure& f) {
            std::cout << "[FAIL] " << name << ": " << f.message << "\n";
            ++failed;
        } catch (const std::exception& e) {
            std::cout << "[FAIL] " << name << ": uncaught exception: " << e.what() << "\n";
            ++failed;
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed, "
               << (passed + failed) << " total\n";
    return failed == 0 ? 0 : 1;
}

} // namespace testing

#define TEST_CASE(name) \
    void name(); \
    static testing::Registrar registrar_##name(#name, name); \
    void name()

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::ostringstream oss; \
        oss << "CHECK failed: " #cond " at " << __FILE__ << ":" << __LINE__; \
        throw testing::Failure{oss.str()}; \
    } } while (0)

#define CHECK_EQ(a, b) \
    do { if (!((a) == (b))) { \
        std::ostringstream oss; \
        oss << "CHECK_EQ failed: " #a " == " #b " (" << (a) << " != " << (b) << ") at " \
            << __FILE__ << ":" << __LINE__; \
        throw testing::Failure{oss.str()}; \
    } } while (0)
