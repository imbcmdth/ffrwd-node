/// A ratio of two whole numbers: a time base, where a timestamp counts units
/// of `num / den` of a second, or a rate, where it is so many per second.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Rational {
    pub num: i32,
    pub den: i32,
}

impl Rational {
    /// The time base of a self-clocked node: microseconds.
    pub const MICROS: Rational = Rational {
        num: 1,
        den: 1_000_000,
    };

    pub const fn new(num: i32, den: i32) -> Rational {
        Rational { num, den }
    }

    /// The time base of a rate: 30/1 frames a second ticks in 1/30.
    pub const fn inverse(self) -> Rational {
        Rational {
            num: self.den,
            den: self.num,
        }
    }

    /// `pts`, counted in this time base, as seconds.
    pub fn seconds(self, pts: i64) -> f64 {
        pts as f64 * self.num as f64 / self.den as f64
    }

    /// The timestamp in this time base nearest to `seconds`.
    pub fn pts(self, seconds: f64) -> i64 {
        (seconds * self.den as f64 / self.num as f64).round() as i64
    }

    /// `pts` in this time base, counted in `to` instead: exact, rounded to the
    /// nearest unit and away from zero on a tie, as ffmpeg's `av_rescale_q`.
    pub fn rescale(self, pts: i64, to: Rational) -> i64 {
        let num = pts as i128 * self.num as i128 * to.den as i128;
        let den = self.den as i128 * to.num as i128;
        let (num, den) = if den < 0 { (-num, -den) } else { (num, den) };
        let half = den / 2;
        let rounded = if num >= 0 {
            (num + half) / den
        } else {
            (num - half) / den
        };
        rounded.clamp(i64::MIN as i128, i64::MAX as i128) as i64
    }

    /// The fraction nearest to `value` whose denominator is at most
    /// `max_den`: a rate a param gives in frames a second, as the clock takes
    /// it. 30 is 30/1 and 29.97 is 2997/100.
    pub fn approximate(value: f64, max_den: i32) -> Rational {
        let max_den = max_den.max(1) as i64;
        let negative = value < 0.0;
        let x = value.abs();
        let (mut p0, mut q0, mut p1, mut q1) = (0i64, 1i64, 1i64, 0i64);
        let mut rest = x;
        loop {
            let whole = rest.floor();
            let a = whole as i64;
            let (p2, q2) = (a * p1 + p0, a * q1 + q0);
            if q2 > max_den || p2 > i32::MAX as i64 {
                let k = (max_den - q0) / q1.max(1);
                let (pk, qk) = (k * p1 + p0, k * q1 + q0);
                let close = |p: i64, q: i64| (p as f64 / q as f64 - x).abs();
                if qk > 0 && pk <= i32::MAX as i64 && close(pk, qk) < close(p1, q1) {
                    p1 = pk;
                    q1 = qk;
                }
                break;
            }
            (p0, q0, p1, q1) = (p1, q1, p2, q2);
            let frac = rest - whole;
            if frac < 1e-9 || (p1 as f64 / q1 as f64 - x).abs() < 1e-12 {
                break;
            }
            rest = 1.0 / frac;
        }
        let num = if negative { -p1 } else { p1 };
        Rational::new(num as i32, q1.max(1) as i32)
    }
}

#[cfg(test)]
mod tests {
    use super::Rational;

    #[test]
    fn seconds_and_back() {
        let tb = Rational::new(1, 15360);
        assert_eq!(tb.seconds(15360), 1.0);
        assert_eq!(tb.pts(1.0), 15360);
        assert_eq!(tb.pts(tb.seconds(1024)), 1024);
        let ntsc = Rational::new(1001, 30000);
        assert_eq!(ntsc.pts(ntsc.seconds(12345)), 12345);
    }

    #[test]
    fn rescale_rounds_to_nearest_away_from_zero() {
        let video = Rational::new(1, 15360);
        let audio = Rational::new(1, 48000);
        assert_eq!(video.rescale(15360, audio), 48000);
        assert_eq!(video.rescale(1024, Rational::new(1, 15)), 1);
        assert_eq!(Rational::new(1, 2).rescale(1, Rational::new(1, 1)), 1);
        assert_eq!(Rational::new(1, 2).rescale(-1, Rational::new(1, 1)), -1);
        assert_eq!(Rational::new(1, 3).rescale(1, Rational::new(1, 1)), 0);
        assert_eq!(
            Rational::new(1001, 30000).rescale(30, Rational::MICROS),
            1_001_000
        );
    }

    #[test]
    fn a_rate_is_a_time_base_inverted() {
        assert_eq!(Rational::new(30, 1).inverse(), Rational::new(1, 30));
        assert_eq!(Rational::new(30000, 1001).inverse().seconds(30), 1.001);
    }

    #[test]
    fn approximate_finds_the_fraction_a_param_meant() {
        assert_eq!(Rational::approximate(30.0, 1001), Rational::new(30, 1));
        assert_eq!(Rational::approximate(29.97, 1001), Rational::new(2997, 100));
        assert_eq!(Rational::approximate(0.5, 1001), Rational::new(1, 2));
        assert_eq!(
            Rational::approximate(30000.0 / 1001.0, 1001),
            Rational::new(30000, 1001)
        );
        assert_eq!(Rational::approximate(-2.5, 10), Rational::new(-5, 2));
        let pi = Rational::approximate(std::f64::consts::PI, 1000);
        assert_eq!(pi, Rational::new(355, 113));
    }
}
