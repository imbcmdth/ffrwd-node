#pragma once

// The least a test needs: named cases, and checks that say what failed and
// where.

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "ffrwd/result.hpp"

namespace check {

struct Case {
    const char* name;
    std::function<void()> run;
};

inline std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Register {
    Register(const char* name, std::function<void()> run) { cases().push_back({name, std::move(run)}); }
};

inline void failed(const char* file, int line, const std::string& what) {
    ++failures();
    std::fprintf(stderr, "  %s:%d: %s\n", file, line, what.c_str());
}

template <class T>
std::string shown(const T& value) {
    if constexpr (requires(std::ostream& out) { out << value; }) {
        std::ostringstream out;
        out << value;
        return out.str();
    } else {
        return "(a value)";
    }
}

template <class T>
T ok(const char* file, int line, ffrwd::Result<T> result) {
    if (!result) {
        failed(file, line, result.error().message);
        throw result.error();
    }
    if constexpr (!std::is_void_v<T>) return std::move(*result);
}

}  // namespace check

#define TEST(name)                                                  \
    static void name();                                             \
    static const check::Register name##_registered(#name, &name);   \
    static void name()

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) check::failed(__FILE__, __LINE__, "not " #condition);   \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        const auto& check_a_ = (a);                                                        \
        const auto& check_b_ = (b);                                                        \
        if (!(check_a_ == check_b_))                                                       \
            check::failed(__FILE__, __LINE__,                                              \
                          #a " == " #b ": " + check::shown(check_a_) + " against " +       \
                              check::shown(check_b_));                                     \
    } while (0)

/// The value of a `Result`, or the test stops here with its error.
#define CHECK_OK(...) check::ok(__FILE__, __LINE__, (__VA_ARGS__))

/// The error message of a `Result` that should have failed.
#define CHECK_ERR(...)                                                             \
    ([&]() -> std::string {                                                        \
        auto check_result_ = (__VA_ARGS__);                                        \
        if (check_result_) {                                                       \
            check::failed(__FILE__, __LINE__, "succeeded: " #__VA_ARGS__);         \
            return {};                                                             \
        }                                                                          \
        return check_result_.error().message;                                      \
    }())

#define CHECK_HAS(text, part)                                                                  \
    do {                                                                                       \
        const std::string check_text_ = (text);                                                \
        if (check_text_.find(part) == std::string::npos)                                       \
            check::failed(__FILE__, __LINE__, "`" + check_text_ + "` does not say `" + (part) + "`"); \
    } while (0)
