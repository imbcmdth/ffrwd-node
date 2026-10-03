/** A ratio of two whole numbers: a time base, where a timestamp counts units
 * of `num / den` of a second, or a rate, where it is so many per second. */
export class Rational {
  constructor(num, den) {
    this.num = num;
    this.den = den;
  }

  /** The time base of a rate: 30/1 frames a second ticks in 1/30. */
  inverse() {
    return new Rational(this.den, this.num);
  }

  /** `pts`, counted in this time base, as seconds. */
  seconds(pts) {
    return (Number(pts) * this.num) / this.den;
  }

  /** The timestamp in this time base nearest to `seconds`. */
  pts(seconds) {
    return roundHalfAway((seconds * this.den) / this.num);
  }

  /** At this rate, the frames or samples `seconds` takes, a part counted
   * whole: 2 s at 48000/1 is 96000, and 1 s at 30000/1001 is 30. */
  count(seconds) {
    const exact = (seconds * this.num) / this.den;
    return Math.max(0, Math.ceil(exact - 1e-9));
  }

  /** At this rate, how long `count` frames or samples last, in seconds. */
  duration(count) {
    return (count * this.den) / this.num;
  }

  /** `pts` in this time base, counted in `to` instead: exact, rounded to the
   * nearest unit and away from zero on a tie, as ffmpeg's `av_rescale_q`. */
  rescale(pts, to) {
    let num = BigInt(pts) * BigInt(this.num) * BigInt(to.den);
    let den = BigInt(this.den) * BigInt(to.num);
    if (den < 0n) {
      num = -num;
      den = -den;
    }
    const half = den / 2n;
    return Number(num >= 0n ? (num + half) / den : (num - half) / den);
  }

  equals(other) {
    return other != null && this.num === other.num && this.den === other.den;
  }

  /** The fraction nearest to `value` whose denominator is at most
   * `maxDen`: 30 is 30/1 and 29.97 is 2997/100. */
  static approximate(value, maxDen) {
    const limit = Math.max(1, maxDen);
    const top = 2 ** 31 - 1;
    const negative = value < 0;
    const x = Math.abs(value);
    let [p0, q0, p1, q1] = [0, 1, 1, 0];
    let rest = x;
    for (;;) {
      const whole = Math.floor(rest);
      const [p2, q2] = [whole * p1 + p0, whole * q1 + q0];
      if (q2 > limit || p2 > top) {
        const k = Math.floor((limit - q0) / Math.max(1, q1));
        const [pk, qk] = [k * p1 + p0, k * q1 + q0];
        const close = (p, q) => Math.abs(p / q - x);
        if (qk > 0 && pk <= top && close(pk, qk) < close(p1, q1)) {
          p1 = pk;
          q1 = qk;
        }
        break;
      }
      [p0, q0, p1, q1] = [p1, q1, p2, q2];
      const frac = rest - whole;
      if (frac < 1e-9 || Math.abs(p1 / q1 - x) < 1e-12) break;
      rest = 1 / frac;
    }
    return new Rational(negative ? -p1 : p1, Math.max(1, q1));
  }
}

/** The time base of a self-clocked node: microseconds. */
Rational.MICROS = new Rational(1, 1_000_000);

function roundHalfAway(value) {
  return Math.sign(value) * Math.round(Math.abs(value));
}

export function rational(value) {
  return value == null ? undefined : new Rational(value.num, value.den);
}
