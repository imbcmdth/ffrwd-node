#pragma once

#include <expected>
#include <string>
#include <utility>

namespace ffrwd {

/// Why a call failed: the message the run ends with.
struct Error {
    std::string message;

    Error() = default;
    Error(std::string message) : message(std::move(message)) {}
    Error(const char* message) : message(message) {}
};

/// A value or the error that stopped it.
template <class T = void>
using Result = std::expected<T, Error>;

/// Done, or the error that stopped it.
using Status = Result<void>;

/// An error to return from any function answering a `Result`.
inline std::unexpected<Error> fail(std::string message) {
    return std::unexpected<Error>(Error(std::move(message)));
}

}  // namespace ffrwd

#define FFRWD_CONCAT_(a, b) a##b
#define FFRWD_CONCAT(a, b) FFRWD_CONCAT_(a, b)

/// Returns the error of `expr` from the enclosing function, if it has one.
#define FFRWD_TRY(...)                                                              \
    do {                                                                            \
        auto&& ffrwd_try_ = (__VA_ARGS__);                                          \
        if (!ffrwd_try_)                                                            \
            return std::unexpected<::ffrwd::Error>(                                 \
                ::ffrwd::Error(std::move(ffrwd_try_).error()));                     \
    } while (0)

/// Declares `name` as the value of `expr`, or returns its error from the
/// enclosing function.
#define FFRWD_LET(name, ...)                                                        \
    auto FFRWD_CONCAT(ffrwd_let_, __LINE__) = (__VA_ARGS__);                        \
    if (!FFRWD_CONCAT(ffrwd_let_, __LINE__))                                        \
        return std::unexpected<::ffrwd::Error>(                                     \
            ::ffrwd::Error(std::move(FFRWD_CONCAT(ffrwd_let_, __LINE__)).error())); \
    auto name = std::move(*FFRWD_CONCAT(ffrwd_let_, __LINE__))
