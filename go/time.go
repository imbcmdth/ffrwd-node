package node

import (
	"math"
	"math/big"
)

// Rational is a ratio of two whole numbers: a time base, where a timestamp
// counts units of Num/Den of a second, or a rate, where it is so many per
// second.
type Rational struct {
	Num int32
	Den int32
}

// Micros is the time base of a self-clocked node: microseconds.
var Micros = Rational{1, 1_000_000}

// R is a Rational of num/den.
func R(num, den int32) Rational {
	return Rational{num, den}
}

// Inverse is the time base of a rate: 30/1 frames a second ticks in 1/30.
func (r Rational) Inverse() Rational {
	return Rational{r.Den, r.Num}
}

// Seconds is pts, counted in this time base, as seconds.
func (r Rational) Seconds(pts int64) float64 {
	return float64(pts) * float64(r.Num) / float64(r.Den)
}

// Pts is the timestamp in this time base nearest to seconds.
func (r Rational) Pts(seconds float64) int64 {
	return int64(math.Round(seconds * float64(r.Den) / float64(r.Num)))
}

// Count is, at this rate, the frames or samples seconds takes, a part
// counted whole: 2 s at 48000/1 is 96000, and 1 s at 30000/1001 is 30.
func (r Rational) Count(seconds float64) uint64 {
	exact := seconds * float64(r.Num) / float64(r.Den)
	return uint64(math.Max(0, math.Ceil(exact-1e-9)))
}

// Duration is, at this rate, how long count frames or samples last, in
// seconds.
func (r Rational) Duration(count uint64) float64 {
	return float64(count) * float64(r.Den) / float64(r.Num)
}

// Rescale is pts in this time base counted in to instead: exact, rounded to
// the nearest unit and away from zero on a tie, as ffmpeg's av_rescale_q.
func (r Rational) Rescale(pts int64, to Rational) int64 {
	num := new(big.Int).Mul(big.NewInt(pts), big.NewInt(int64(r.Num)))
	num.Mul(num, big.NewInt(int64(to.Den)))
	den := big.NewInt(int64(r.Den) * int64(to.Num))
	if den.Sign() < 0 {
		num.Neg(num)
		den.Neg(den)
	}
	half := new(big.Int).Quo(den, big.NewInt(2))
	if num.Sign() >= 0 {
		num.Add(num, half)
	} else {
		num.Sub(num, half)
	}
	rounded := num.Quo(num, den)
	if !rounded.IsInt64() {
		if rounded.Sign() < 0 {
			return math.MinInt64
		}
		return math.MaxInt64
	}
	return rounded.Int64()
}

// Approximate is the fraction nearest to value whose denominator is at most
// maxDen: a rate a param gives in frames a second, as the clock takes it.
// 30 is 30/1 and 29.97 is 2997/100.
func Approximate(value float64, maxDen int32) Rational {
	limit := int64(max(maxDen, 1))
	negative := value < 0
	x := math.Abs(value)
	p0, q0, p1, q1 := int64(0), int64(1), int64(1), int64(0)
	rest := x
	for {
		whole := math.Floor(rest)
		a := int64(whole)
		p2, q2 := a*p1+p0, a*q1+q0
		if q2 > limit || p2 > math.MaxInt32 {
			k := (limit - q0) / max(q1, 1)
			pk, qk := k*p1+p0, k*q1+q0
			close := func(p, q int64) float64 { return math.Abs(float64(p)/float64(q) - x) }
			if qk > 0 && pk <= math.MaxInt32 && close(pk, qk) < close(p1, q1) {
				p1, q1 = pk, qk
			}
			break
		}
		p0, q0, p1, q1 = p1, q1, p2, q2
		frac := rest - whole
		if frac < 1e-9 || math.Abs(float64(p1)/float64(q1)-x) < 1e-12 {
			break
		}
		rest = 1 / frac
	}
	if negative {
		p1 = -p1
	}
	return Rational{int32(p1), int32(max(q1, 1))}
}
