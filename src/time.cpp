#include "ffrwd/time.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ffrwd {

namespace {

std::int64_t saturate(double value) {
    if (std::isnan(value)) return 0;
    if (value >= 9223372036854775807.0) return std::numeric_limits<std::int64_t>::max();
    if (value <= -9223372036854775808.0) return std::numeric_limits<std::int64_t>::min();
    return static_cast<std::int64_t>(value);
}

}  // namespace

std::int64_t Rational::pts(double seconds) const {
    return saturate(std::round(seconds * double(den) / double(num)));
}

std::uint64_t Rational::count(double seconds) const {
    double exact = seconds * double(num) / double(den);
    double whole = std::max(std::ceil(exact - 1e-9), 0.0);
    if (std::isnan(whole)) return 0;
    if (whole >= 18446744073709551615.0) return std::numeric_limits<std::uint64_t>::max();
    return static_cast<std::uint64_t>(whole);
}

std::int64_t Rational::rescale(std::int64_t pts, Rational to) const {
    __int128 top = __int128(pts) * num * to.den;
    __int128 bottom = __int128(den) * to.num;
    if (bottom < 0) {
        top = -top;
        bottom = -bottom;
    }
    __int128 half = bottom / 2;
    __int128 rounded = top >= 0 ? (top + half) / bottom : (top - half) / bottom;
    const __int128 lo = std::numeric_limits<std::int64_t>::min();
    const __int128 hi = std::numeric_limits<std::int64_t>::max();
    return static_cast<std::int64_t>(std::clamp(rounded, lo, hi));
}

Rational Rational::approximate(double value, std::int32_t max_den) {
    const std::int64_t most = std::max<std::int64_t>(max_den, 1);
    const std::int64_t top = std::numeric_limits<std::int32_t>::max();
    bool negative = value < 0.0;
    double x = std::fabs(value);
    std::int64_t p0 = 0, q0 = 1, p1 = 1, q1 = 0;
    double rest = x;
    for (;;) {
        double whole = std::floor(rest);
        std::int64_t a = static_cast<std::int64_t>(whole);
        std::int64_t p2 = a * p1 + p0, q2 = a * q1 + q0;
        if (q2 > most || p2 > top) {
            std::int64_t k = (most - q0) / std::max<std::int64_t>(q1, 1);
            std::int64_t pk = k * p1 + p0, qk = k * q1 + q0;
            auto close = [x](std::int64_t p, std::int64_t q) { return std::fabs(double(p) / double(q) - x); };
            if (qk > 0 && pk <= top && close(pk, qk) < close(p1, q1)) {
                p1 = pk;
                q1 = qk;
            }
            break;
        }
        p0 = p1;
        q0 = q1;
        p1 = p2;
        q1 = q2;
        double frac = rest - whole;
        if (frac < 1e-9 || std::fabs(double(p1) / double(q1) - x) < 1e-12) break;
        rest = 1.0 / frac;
    }
    std::int64_t num = negative ? -p1 : p1;
    return {static_cast<std::int32_t>(num), static_cast<std::int32_t>(std::max<std::int64_t>(q1, 1))};
}

}  // namespace ffrwd
