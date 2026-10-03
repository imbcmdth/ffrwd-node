package node

import (
	"reflect"
	"strings"
	"testing"
)

func filter() Shape {
	return NewShape().Input(VideoInput("v").Clock().PixelFormats("rgba")).Output(LikeOutput("v"))
}

func resolved(t *testing.T, shape Shape, bound *Bound) *NodeShape {
	t.Helper()
	got, err := shape.Resolve(bound)
	if err != nil {
		t.Fatal(err)
	}
	return got
}

func refuses(t *testing.T, shape Shape, bound *Bound, said ...string) {
	t.Helper()
	_, err := shape.Resolve(bound)
	if err == nil {
		t.Fatalf("resolved, wanted %q", said)
	}
	for _, part := range said {
		if !strings.Contains(err.Error(), part) {
			t.Fatalf("%v, wanted %q", err, part)
		}
	}
}

func TestTheClockComesFromTheInputMarked(t *testing.T) {
	shape := resolved(t, filter(), NewBound("v"))
	expect(t, shape.Clock, Clock{Kind: ClockInput, Port: "v"})
	expect(t, shape.Outputs[0].Kind, Video)
	expect(t, *shape.Outputs[0].Like.Port, "v")
}

func TestAPixelFormatAloneFollowsTheClock(t *testing.T) {
	shape := resolved(t, NewShape().Input(VideoInput("v").Clock()).Output(VideoOutput("mask").PixelFormat("gray")),
		NewBound("v"))
	expect(t, *shape.Outputs[0].Like.Port, "v")
	expect(t, *shape.Outputs[0].Like.PixelFormat, "gray")
}

func TestAnOutputFollowingAnUnboundInputIsLeftOut(t *testing.T) {
	shape := resolved(t, NewShape().
		Input(VideoInput("v").Clock()).
		Input(AudioInput("a").Optional()).
		Output(LikeOutput("v")).
		Output(LikeOutput("a")), NewBound("v"))
	expect(t, len(shape.Outputs), 1)
	expect(t, shape.Outputs[0].Name, "v")
}

func TestRefusalsNameThePort(t *testing.T) {
	refuses(t, NewShape().Input(VideoInput("v")), NewBound("v"), "no clock")
	refuses(t, NewShape().Input(VideoInput("v").Clock().Optional()), NewBound("v"), "`v`")
	refuses(t, filter().Input(RowsInput("boxes").Hold()), NewBound("v", "boxes"), "`boxes`")
	refuses(t, filter().Input(VideoInput("w").Interval()), NewBound("v", "w"), "`w`")
	refuses(t, filter().Input(RowsInput("boxes").IgnoreRows()), NewBound("v", "boxes"), "`boxes`")
	refuses(t, NewShape().Input(AudioInput("a").Clock().Window(4, 5)), NewBound("a"), "stride")
	refuses(t, NewShape().Input(VideoInput("v").Many().Hold()).RateOf("v").Output(VideoOutput("out").Following("v")),
		NewBound("v"), "many")
	refuses(t, NewShape().Rate(R(30, 1)).Input(VideoInput("v")).Output(VideoOutput("out").Size(64, 64).PixelFormat("rgba")),
		NewBound("v"), "lockstep")
}

func TestAGeneratorGivesItsOwnFormat(t *testing.T) {
	ticker := NewShape().
		Rate(R(30, 1)).
		Output(VideoOutput("video").Size(1280, 720).PixelFormat("rgba").Row(0)).
		RelationRow("{}").
		Bounded(false)
	shape := resolved(t, ticker, NewBound())
	expect(t, *shape.Outputs[0].Format.Video, VideoFormat{Width: 1280, Height: 720, PixFmt: "rgba"})
	refuses(t, NewShape().Rate(R(30, 1)).Output(VideoOutput("video")), NewBound(), "give it a format")
	refuses(t, NewShape().Rate(R(30, 1)).Output(VideoOutput("video").Size(8, 8)), NewBound(), "no pixel format")
}

func TestHoldAndIntervalFieldsChain(t *testing.T) {
	feed := VideoInput("feed").Optional().Hold().Lead(0.5).PortParam("port").Spec()
	expect(t, feed.Pairing.Kind, PairingHold)
	expect(t, feed.Pairing.Hold.Anchor, FirstFrame)
	expect(t, feed.Pairing.Hold.Lead, 0.5)
	expect(t, *feed.Pairing.Hold.PortParam, "port")

	words := RowsInput("words").Latency(2).Ahead(0.5).State().Spec()
	expect(t, words.Pairing.Kind, PairingInterval)
	expect(t, *words.Pairing.Interval.Latency, 2.0)
	expect(t, words.Pairing.Interval.Ahead, 0.5)
	expect(t, words.Pairing.Interval.Anchor, SharedClock)
	expect(t, words.Rows, RowsState)
}

func TestABuilderCopiedIsItsOwn(t *testing.T) {
	base := VideoInput("feed").Hold()
	early, late := base.Lead(1), base.Lead(2)
	expect(t, early.Spec().Pairing.Hold.Lead, 1.0)
	expect(t, late.Spec().Pairing.Hold.Lead, 2.0)
}

func TestATimingInputIsAFrameKind(t *testing.T) {
	mask := NewShape().Input(VideoInput("v").Clock().Timing()).Output(VideoOutput("mask").PixelFormat("gray"))
	expect(t, resolved(t, mask, NewBound("v")).Inputs[0].Accepts.Wants, WantsTiming)
	resolved(t, NewShape().Input(AudioInput("a").Clock()).Input(AudioInput("b").Timing().Like("a")), NewBound("a", "b"))
	refuses(t, filter().Input(RowsInput("boxes").Interval().Timing()), NewBound("v", "boxes"), "`boxes`", "timing")
}

func TestADataInputTakesAnAnchorAndAHoldGroup(t *testing.T) {
	follow := RowsInput("d").Anchor(FirstFrame).Spec()
	expect(t, follow.Pairing.Interval.Anchor, FirstFrame)
	feed := VideoInput("feed").Optional().Group("ad")
	expect(t, *feed.Spec().Pairing.Hold.Group, "ad")
	beside := RowsInput("cues").Latency(1).Group("ad")
	expect(t, *beside.Spec().Pairing.Interval.Group, "ad")
	expect(t, *beside.Spec().Pairing.Interval.Latency, 1.0)
	expect(t, beside.Spec().Pairing.Interval.Anchor, SharedClock)

	bound := NewBound("v", "feed", "cues")
	resolved(t, filter().Input(feed).Input(beside), bound)
	refuses(t, filter().Input(VideoInput("feed").Optional().Hold()).Input(beside), bound, "`cues`", "`ad`")
	refuses(t, filter().Input(feed).Input(beside.Anchor(Tagged("smart_timed"))), bound, "`cues`", "shared clock")
}

func TestAHeldFrameInputKeepsItsAnchorAndGroup(t *testing.T) {
	feed := VideoInput("feed").Anchor(Tagged("smart_timed")).Group("ad").Spec()
	expect(t, feed.Pairing.Kind, PairingHold)
	expect(t, feed.Pairing.Hold.Anchor, Tagged("smart_timed"))
	expect(t, *feed.Pairing.Hold.Group, "ad")
}

func TestBoundSaysHowManyStreamsAndAtWhatRate(t *testing.T) {
	bound := NewBound("v").Rate("v", R(30000, 1001)).Bind("inputs", ptr(R(25, 1)), nil)
	expect(t, bound.Has("v") && bound.Has("inputs") && !bound.Has("a"), true)
	expect(t, bound.Count("v"), 1)
	expect(t, bound.Count("inputs"), 2)
	expect(t, bound.Count("a"), 0)
	rate, _ := bound.RateOf("v")
	expect(t, rate, R(30000, 1001))
	rate, _ = bound.RateOf("inputs")
	expect(t, rate, R(25, 1))
	expect(t, bound.Streams("inputs")[1].Rate == nil, true)
	_, known := bound.RateOf("a")
	expect(t, known, false)
	expect(t, NewBound().Rate("a", R(48000, 1)).Count("a"), 1)
}

func TestBoundStreamsCarryTheHintsTheShapeWasAskedWith(t *testing.T) {
	tb := R(1, 15360)
	bound := BoundOf([]BoundStream{
		VideoStream("v", 0, 2, 2, "rgba", tb).WithRate(R(30000, 1001)),
		AudioStream("a", 1, 48000, 2, "f32"),
		VideoStream("inputs", 2, 2, 2, "rgba", tb).WithRate(R(25, 1)),
		VideoStream("inputs", 3, 2, 2, "rgba", tb),
		RowsStream("d", 4, tb),
	})
	asked := NewBound("v", "a").
		Rate("v", R(30000, 1001)).
		Rate("a", R(48000, 1)).
		Bind("inputs", ptr(R(25, 1)), nil).
		Bind("d", nil)
	if !reflect.DeepEqual(bound, asked) {
		t.Fatalf("%+v\n%+v", bound.Inputs(), asked.Inputs())
	}
}
