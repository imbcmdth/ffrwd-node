package node

import (
	"math"
	"testing"
)

func expect[T comparable](t *testing.T, got, want T) {
	t.Helper()
	if got != want {
		t.Fatalf("got %v, want %v", got, want)
	}
}

func TestSecondsAndBack(t *testing.T) {
	tb := R(1, 15360)
	expect(t, tb.Seconds(15360), 1.0)
	expect(t, tb.Pts(1), 15360)
	expect(t, tb.Pts(tb.Seconds(1024)), 1024)
	ntsc := R(1001, 30000)
	expect(t, ntsc.Pts(ntsc.Seconds(12345)), 12345)
}

func TestRescaleRoundsToNearestAwayFromZero(t *testing.T) {
	expect(t, R(1, 15360).Rescale(15360, R(1, 48000)), 48000)
	expect(t, R(1, 15360).Rescale(1024, R(1, 15)), 1)
	expect(t, R(1, 2).Rescale(1, R(1, 1)), 1)
	expect(t, R(1, 2).Rescale(-1, R(1, 1)), -1)
	expect(t, R(1, 3).Rescale(1, R(1, 1)), 0)
	expect(t, R(1001, 30000).Rescale(30, Micros), 1_001_000)
}

func TestARateIsATimeBaseInverted(t *testing.T) {
	expect(t, R(30, 1).Inverse(), R(1, 30))
	expect(t, R(30000, 1001).Inverse().Seconds(30), 1.001)
}

func TestSecondsCountFramesAndSamplesAtARate(t *testing.T) {
	expect(t, R(48000, 1).Count(2), 96000)
	expect(t, R(16000, 1).Count(30), 480000)
	expect(t, R(30000, 1001).Count(1), 30)
	expect(t, R(30, 1).Count(0.1), 3)
	expect(t, R(25, 1).Count(0), 0)
	expect(t, R(30000, 1001).Duration(1), 1001.0/30000.0)
	expect(t, R(2, 1).Duration(1), 0.5)
	ntsc := R(30000, 1001)
	expect(t, ntsc.Count(ntsc.Duration(300)), 300)
}

func TestApproximateFindsTheFractionAParamMeant(t *testing.T) {
	expect(t, Approximate(30, 1001), R(30, 1))
	expect(t, Approximate(29.97, 1001), R(2997, 100))
	expect(t, Approximate(0.5, 1001), R(1, 2))
	expect(t, Approximate(30000.0/1001.0, 1001), R(30000, 1001))
	expect(t, Approximate(-2.5, 10), R(-5, 2))
	expect(t, Approximate(math.Pi, 1000), R(355, 113))
}
