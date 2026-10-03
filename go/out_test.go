package node

import (
	"strings"
	"testing"
)

func newOut(t *testing.T) *Out {
	t.Helper()
	shape := resolved(t, NewShape().
		Input(VideoInput("v").Clock()).
		Input(PacketsInput("coded")).
		Output(LikeOutput("v")).
		Output(RowsOutput("spots").TimeBase(R(1, 1000))).
		Output(PacketsOutput("p").Following("coded")), NewBound("v", "coded"))
	out := NewOut(shape, nil)
	out.Begin(R(1, 15))
	return out
}

func fails(t *testing.T, err error, said string) {
	t.Helper()
	if err == nil || !strings.Contains(err.Error(), said) {
		t.Fatalf("%v, wanted %q", err, said)
	}
}

func TestAPortNeverGoesBack(t *testing.T) {
	out := newOut(t)
	one := ptr(int64(1))
	if out.Same("v", 2, one, 0, 0) != nil || out.Same("v", 2, one, 0, 0) != nil {
		t.Fatal("equal pts are refused")
	}
	fails(t, out.Same("v", 1, one, 0, 0), "from pts 2 to 1")
	out.Take()
	fails(t, out.Frame("v", 1, nil, nil), "never decrease")
	if err := out.Row("spots", 0, 1); err != nil {
		t.Fatal(err)
	}
	last, _ := out.Last("spots")
	expect(t, last, 0)
}

func TestPortsAndKindsAreChecked(t *testing.T) {
	out := newOut(t)
	fails(t, out.Row("nowhere", 0, 1), "not an output")
	fails(t, out.Row("v", 0, 1), "video output")
	fails(t, out.Frame("spots", 0, nil, nil), "data output")
}

func TestPacketsKeepDecodeOrder(t *testing.T) {
	out := newOut(t)
	packet := func(pts int64, dts *int64) Packet { return Packet{Pts: pts, Dts: dts} }
	for _, p := range []Packet{packet(3, nil), packet(3, ptr(int64(0))), packet(1, ptr(int64(1)))} {
		if err := out.Packet("p", p); err != nil {
			t.Fatal(err)
		}
	}
	fails(t, out.Packet("p", packet(2, ptr(int64(0)))), "decode order")
}

func TestSecondsLandInThePortTimeBase(t *testing.T) {
	out := newOut(t)
	pts, _ := out.Pts("v", 2)
	expect(t, pts, 30)
	pts, _ = out.Pts("spots", 2)
	expect(t, pts, 2000)
	if err := out.Cue("spots", Cue{1.5, 2, "hi"}); err != nil {
		t.Fatal(err)
	}
	emitted := out.Take()
	expect(t, emitted.Messages("spots")[0], TimedText{1500, `{"start_t":1.5,"end_t":2,"text":"hi"}`})
}
