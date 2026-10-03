#pragma once

#include <cstdint>

namespace ffrwd {

/// A ratio of two whole numbers: a time base, where a timestamp counts units
/// of `num / den` of a second, or a rate, where it is so many per second.
struct Rational {
    std::int32_t num = 0;
    std::int32_t den = 1;

    constexpr Rational() = default;
    constexpr Rational(std::int32_t num, std::int32_t den) : num(num), den(den) {}

    /// The time base of a self-clocked node: microseconds.
    static constexpr Rational micros() { return {1, 1000000}; }

    /// The time base of a rate: 30/1 frames a second ticks in 1/30.
    constexpr Rational inverse() const { return {den, num}; }

    /// `pts`, counted in this time base, as seconds.
    double seconds(std::int64_t pts) const {
        return double(pts) * double(num) / double(den);
    }

    /// The timestamp in this time base nearest to `seconds`.
    std::int64_t pts(double seconds) const;

    /// At this rate, the frames or samples `seconds` takes, a part counted
    /// whole: what a window or a latency known in seconds spans. 2 s at
    /// 48000/1 is 96000, and 1 s at 30000/1001 is 30.
    std::uint64_t count(double seconds) const;

    /// At this rate, how long `count` frames or samples last, in seconds.
    double duration(std::uint64_t count) const {
        return double(count) * double(den) / double(num);
    }

    /// `pts` in this time base, counted in `to` instead: exact, rounded to
    /// the nearest unit and away from zero on a tie, as ffmpeg's
    /// `av_rescale_q`.
    std::int64_t rescale(std::int64_t pts, Rational to) const;

    /// The fraction nearest to `value` whose denominator is at most
    /// `max_den`: a rate a param gives in frames a second, as the clock takes
    /// it. 30 is 30/1 and 29.97 is 2997/100.
    static Rational approximate(double value, std::int32_t max_den);

    friend constexpr bool operator==(Rational a, Rational b) {
        return a.num == b.num && a.den == b.den;
    }
};

}  // namespace ffrwd
