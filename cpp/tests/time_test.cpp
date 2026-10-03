#include <numbers>

#include "check.hpp"
#include "ffrwd/time.hpp"

using ffrwd::Rational;

TEST(seconds_and_back) {
    Rational tb(1, 15360);
    CHECK_EQ(tb.seconds(15360), 1.0);
    CHECK_EQ(tb.pts(1.0), 15360);
    CHECK_EQ(tb.pts(tb.seconds(1024)), 1024);
    Rational ntsc(1001, 30000);
    CHECK_EQ(ntsc.pts(ntsc.seconds(12345)), 12345);
}

TEST(rescale_rounds_to_nearest_away_from_zero) {
    Rational video(1, 15360), audio(1, 48000);
    CHECK_EQ(video.rescale(15360, audio), 48000);
    CHECK_EQ(video.rescale(1024, Rational(1, 15)), 1);
    CHECK_EQ(Rational(1, 2).rescale(1, Rational(1, 1)), 1);
    CHECK_EQ(Rational(1, 2).rescale(-1, Rational(1, 1)), -1);
    CHECK_EQ(Rational(1, 3).rescale(1, Rational(1, 1)), 0);
    CHECK_EQ(Rational(1001, 30000).rescale(30, Rational::micros()), 1001000);
}

TEST(a_rate_is_a_time_base_inverted) {
    CHECK(Rational(30, 1).inverse() == Rational(1, 30));
    CHECK_EQ(Rational(30000, 1001).inverse().seconds(30), 1.001);
}

TEST(seconds_count_frames_and_samples_at_a_rate) {
    CHECK_EQ(Rational(48000, 1).count(2.0), 96000u);
    CHECK_EQ(Rational(16000, 1).count(30.0), 480000u);
    CHECK_EQ(Rational(30000, 1001).count(1.0), 30u);
    CHECK_EQ(Rational(30, 1).count(0.1), 3u);
    CHECK_EQ(Rational(25, 1).count(0.0), 0u);
    CHECK_EQ(Rational(30000, 1001).duration(1), 1001.0 / 30000.0);
    CHECK_EQ(Rational(2, 1).duration(1), 0.5);
    Rational ntsc(30000, 1001);
    CHECK_EQ(ntsc.count(ntsc.duration(300)), 300u);
}

TEST(approximate_finds_the_fraction_a_param_meant) {
    CHECK(Rational::approximate(30.0, 1001) == Rational(30, 1));
    CHECK(Rational::approximate(29.97, 1001) == Rational(2997, 100));
    CHECK(Rational::approximate(0.5, 1001) == Rational(1, 2));
    CHECK(Rational::approximate(30000.0 / 1001.0, 1001) == Rational(30000, 1001));
    CHECK(Rational::approximate(-2.5, 10) == Rational(-5, 2));
    CHECK(Rational::approximate(std::numbers::pi, 1000) == Rational(355, 113));
}
