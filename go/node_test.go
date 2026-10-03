package node_test

import (
	"errors"
	"fmt"
	"reflect"
	"strings"
	"testing"

	node "github.com/imbcmdth/ffrwd-node/go"
	"github.com/imbcmdth/ffrwd-node/go/mock"
)

func ok[T any](value T, err error) T {
	if err != nil {
		panic(err)
	}
	return value
}

func fails(t *testing.T, err error, said ...string) {
	t.Helper()
	if err == nil {
		t.Fatalf("succeeded, wanted %q", said)
	}
	for _, part := range said {
		if !strings.Contains(err.Error(), part) {
			t.Fatalf("%v, wanted %q", err, part)
		}
	}
}

func texts(messages []node.TimedText) []string {
	var all []string
	for _, message := range messages {
		all = append(all, fmt.Sprintf("%d %s", message.Pts, message.Text))
	}
	return all
}

type labelled struct {
	Label string `json:"label"`
}

type folding struct {
	v      uint32
	folded [][2]string
}

func (f *folding) Fold(row node.StateRow) error {
	var note struct {
		Note any `json:"note"`
	}
	if err := row.Decode(&note); err != nil {
		return err
	}
	f.folded = append(f.folded, [2]string{fmt.Sprint(row.Pts), fmt.Sprint(note.Note)})
	return nil
}

func (f *folding) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(f.v)
	if !ok {
		return errors.New("no frame")
	}
	return out.Row("seen", frame.Pts, len(f.folded))
}

var foldingNode = node.Definition[labelled]{
	Name:         "folding",
	Version:      "0.0.0",
	ParamsSchema: `{"type":"object","properties":{"label":{"type":"string","default":"a"}},"additionalProperties":false}`,
	Shape: func(labelled, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock()).
			Input(node.RowsInput("notes").Interval().State()).
			Output(node.RowsOutput("seen")), nil
	},
	Init: func(_ labelled, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		return &folding{v: v.ID}, nil
	},
}

func foldingHarness(t *testing.T) *mock.Harness[labelled] {
	tb := node.R(1, 10)
	return ok(mock.Open(foldingNode, "", node.VideoStream("v", 0, 2, 2, "rgba", tb), node.RowsStream("notes", 1, tb)))
}

func TestEarlierRowsFoldBeforeTheTicksOwn(t *testing.T) {
	h := foldingHarness(t)
	tick := h.Tick(5).
		WithFrame(0, 5, make([]byte, 16)).
		WithEarlier(1, 1, `{"note":1}`, `{"note":2}`).
		WithEarlier(1, 3, `{"note":3}`).
		WithMessage(1, 5, []byte(`{"note":4}`))
	emitted := ok(h.Process(tick))
	got := h.Instance().(*folding).folded
	if !reflect.DeepEqual(got, [][2]string{{"1", "1"}, {"1", "2"}, {"3", "3"}, {"5", "4"}}) {
		t.Fatal(got)
	}
	expect(t, texts(emitted.Messages("seen")), []string{"5 4"})
}

func TestParamsInForceAreTakenAndOthersRefused(t *testing.T) {
	h := foldingHarness(t)
	if h.SetParams(`{"label":"a"}`) != nil || h.SetParams("") != nil {
		t.Fatal("params in force were refused")
	}
	fails(t, h.SetParams(`{"label":"b"}`), "cannot change its params")
}

func TestDescribeIsTheDefinition(t *testing.T) {
	meta := foldingNode.Describe()
	expect(t, []string{meta.Name, meta.Version}, []string{"folding", "0.0.0"})
	if !strings.Contains(meta.ParamsSchema, "label") || meta.RowsSchema != "" {
		t.Fatal(meta)
	}
}

func TestAShapeIsResolvedForTheHost(t *testing.T) {
	shape := ok(foldingNode.ResolveShape("", node.NewBound("v")))
	port, ok := shape.ClockInput()
	expect(t, []any{port, ok}, []any{"v", true})
	_, err := foldingNode.ResolveShape(`{"label":3}`, node.NewBound())
	fails(t, err, "`label` is string")
}

func expect[T any](t *testing.T, got, want T) {
	t.Helper()
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v, want %v", got, want)
	}
}

type every struct {
	Every uint64 `json:"every"`
}

type counter struct {
	v     uint32
	every uint64
}

func (c *counter) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(c.v)
	if !ok {
		return nil
	}
	return out.Row("ids", frame.Pts, map[string]uint64{"id": tick.Ordinal() / c.every})
}

var counterNode = node.Definition[every]{
	Name:         "counter",
	Version:      "0.0.0",
	ParamsSchema: `{"type":"object","properties":{"every":{"type":"integer","minimum":1,"default":3}},"additionalProperties":false}`,
	Shape: func(every, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock()).
			Output(node.RowsOutput("ids").Schema(node.SchemaOf[struct {
				ID uint64 `json:"id"`
			}]())).
			Pure(), nil
	},
	Init: func(params every, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		return &counter{v.ID, params.Every}, nil
	},
}

func picture() node.BoundStream {
	return node.VideoStream("v", 0, 2, 2, "rgba", node.R(1, 15))
}

func TestOrdinalsNumberASplitRunAsOne(t *testing.T) {
	whole := ok(mock.Open(counterNode, "", picture()))
	var one []string
	for pts := range int64(9) {
		one = append(one, texts(ok(whole.Process(whole.Tick(pts).WithFrame(0, pts, make([]byte, 16)))).Messages("ids"))...)
	}
	workers := []*mock.Harness[every]{ok(mock.Open(counterNode, "", picture())), ok(mock.Open(counterNode, "", picture()))}
	var split []string
	for pts := range int64(9) {
		worker := workers[pts%2]
		tick := worker.Tick(pts).WithOrdinal(uint64(pts)).WithFrame(0, pts, make([]byte, 16))
		split = append(split, texts(ok(worker.Process(tick)).Messages("ids"))...)
	}
	expect(t, split, one)
	expect(t, one[2], `2 {"id":0}`)
	expect(t, one[3], `3 {"id":1}`)
}

func TestAHarnessNumbersItsTicksFrom0(t *testing.T) {
	h := ok(mock.Open(counterNode, `{"every":1}`, picture()))
	for _, c := range [][2]int64{{0, 0}, {1, 1}, {5, 2}} {
		emitted := ok(h.Process(h.Tick(c[0]).WithFrame(0, c[0], make([]byte, 16))))
		expect(t, emitted.Messages("ids")[0].Text, fmt.Sprintf(`{"id":%d}`, c[1]))
	}
	ok(h.Process(h.Tick(9).WithOrdinal(40).WithFrame(0, 9, make([]byte, 16))))
	emitted := ok(h.Process(h.Tick(10).WithFrame(0, 10, make([]byte, 16))))
	expect(t, emitted.Messages("ids")[0].Text, `{"id":41}`)
}

type peek struct {
	Fetch bool `json:"fetch"`
	Pass  bool `json:"pass"`
}

type mask struct {
	v      uint32
	size   int
	params peek
}

func (m *mask) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(m.v)
	if !ok {
		return nil
	}
	if m.params.Fetch {
		tick.Fetch(m.v, frame.Index)
	}
	if m.params.Pass {
		if err := out.Pass("v", m.v, frame); err != nil {
			return err
		}
	}
	return out.Frame("mask", frame.Pts, frame.Duration, make([]byte, m.size))
}

var maskNode = node.Definition[peek]{
	Name:         "mask",
	Version:      "0.0.0",
	ParamsSchema: `{"type":"object","properties":{"fetch":{"type":"boolean"},"pass":{"type":"boolean"}},"additionalProperties":false}`,
	Shape: func(peek, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().Timing()).
			Output(node.VideoOutput("mask").PixelFormat("gray")).
			Output(node.LikeOutput("v")).
			Pure(), nil
	},
	Init: func(params peek, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		video := v.VideoFormat()
		return &mask{v.ID, int(video.Width * video.Height), params}, nil
	},
}

func TestATimingInputHandsTimesAndSizeAndNoBytes(t *testing.T) {
	plain := ok(mock.Open(maskNode, "", picture()))
	expect(t, plain.Shape().Inputs[0].Accepts.Wants, node.WantsTiming)
	expect(t, len(ok(plain.Process(plain.Tick(4).WithFrame(0, 4, nil))).On("mask")), 1)
	for _, params := range []string{`{"fetch":true}`, `{"pass":true}`} {
		h := ok(mock.Open(maskNode, params, picture()))
		_, err := h.Process(h.Tick(0).WithFrame(0, 0, nil))
		fails(t, err, "`v`", "timing alone")
	}
}

type seconds struct {
	Seconds float64 `json:"seconds"`
}

type idle struct{}

func (idle) Process(*node.Tick, *node.Out) error { return nil }

const secondsSchema = `{"type":"object","properties":{"seconds":{"type":"number","exclusiveMinimum":0,"default":2}},"additionalProperties":false}`

var hearNode = node.Definition[seconds]{
	Name:         "hear",
	Version:      "0.0.0",
	ParamsSchema: secondsSchema,
	Shape: func(params seconds, bound *node.Bound) (node.Shape, error) {
		rate, ok := bound.RateOf("a")
		if !ok {
			return node.Shape{}, errors.New("`a` is bound at a rate the compiler cannot know")
		}
		window := uint32(rate.Count(params.Seconds))
		return node.NewShape().Input(node.AudioInput("a").Clock().Window(window, window)).Output(node.RowsOutput("heard")), nil
	},
	Init: func(seconds, *node.Init) (node.Instance, error) { return idle{}, nil },
}

var clipsNode = node.Definition[seconds]{
	Name:         "clips",
	Version:      "0.0.0",
	ParamsSchema: secondsSchema,
	Shape: func(params seconds, bound *node.Bound) (node.Shape, error) {
		frame := 0.5
		if rate, ok := bound.RateOf("v"); ok {
			frame = rate.Duration(1)
		}
		return node.NewShape().
			Input(node.VideoInput("v").Clock()).
			Output(node.RowsOutput("clips").Latency(params.Seconds + frame)), nil
	},
	Init: func(seconds, *node.Init) (node.Instance, error) { return idle{}, nil },
}

func TestAWindowInSecondsIsCountedAtTheBoundRate(t *testing.T) {
	shape := ok(hearNode.ResolveShape("", node.NewBound("a").Rate("a", node.R(48000, 1))))
	expect(t, shape.Inputs[0].Window, 96000)
	_, err := hearNode.ResolveShape("", node.NewBound("a"))
	fails(t, err, "`a`")
	h := ok(mock.Open(hearNode, `{"seconds":30}`, node.AudioStream("a", 0, 16000, 1, "f32")))
	expect(t, h.Shape().Inputs[0].Window, 480000)
}

func TestALatencyAFramePastACapIsExactWhereTheRateIsKnown(t *testing.T) {
	latency := func(bound *node.Bound) float64 {
		return ok(clipsNode.ResolveShape(`{"seconds":10}`, bound)).Outputs[0].Latency
	}
	expect(t, latency(node.NewBound("v").Rate("v", node.R(2, 1))), 10.5)
	expect(t, latency(node.NewBound("v").Rate("v", node.R(25, 1))), 10.0+1.0/25.0)
	expect(t, latency(node.NewBound("v")), 10.5)
}

func TestInitShapesTheNodeAsThePlanDid(t *testing.T) {
	at25 := picture().WithRate(node.R(25, 1))
	planned := ok(clipsNode.ResolveShape(`{"seconds":10}`, node.BoundOf([]node.BoundStream{at25})))
	h := ok(mock.Open(clipsNode, `{"seconds":10}`, at25))
	expect(t, h.Shape(), planned)
	expect(t, h.Shape().Outputs[0].Latency, 10.0+1.0/25.0)
}

var tileNode = node.Definition[struct{}]{
	Name:    "tile",
	Version: "0.0.0",
	Shape: func(_ struct{}, bound *node.Bound) (node.Shape, error) {
		cells := uint32(max(bound.Count("v"), 1))
		rate, ok := bound.RateOf("v")
		if !ok {
			rate = node.R(30, 1)
		}
		return node.NewShape().
			Input(node.VideoInput("v").Many().Hold()).
			Rate(rate).
			Output(node.VideoOutput("grid").Size(64*cells, 48).PixelFormat("rgba")), nil
	},
	Init: func(struct{}, *node.Init) (node.Instance, error) { return idle{}, nil },
}

func TestAManyPortSaysHowManyStreamsItTakes(t *testing.T) {
	r25, r30 := node.R(25, 1), node.R(30, 1)
	shape := ok(tileNode.ResolveShape("", node.NewBound().Bind("v", &r25, nil, &r30)))
	expect(t, shape.Outputs[0].Format.Video.Width, 192)
	expect(t, shape.Clock, node.Clock{Kind: node.ClockRate, Rate: r25})
}

type present struct {
	feed *uint32
}

func (p present) Process(tick *node.Tick, out *node.Out) error {
	if p.feed == nil {
		return nil
	}
	for _, ended := range tick.EndedFeeds(*p.feed) {
		if ended.Ends == nil {
			return errors.New("an ended feed says its end")
		}
		row := map[string]int64{"coming": ended.Start.Known, "ended": *ended.Ends}
		if err := out.Row("presence", tick.Pts(), row); err != nil {
			return err
		}
	}
	return nil
}

var presentNode = node.Definition[struct{}]{
	Name:    "present",
	Version: "0.0.0",
	Shape: func(struct{}, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock()).
			Input(node.VideoInput("feed").Optional().Hold().Lead(0.5)).
			Output(node.RowsOutput("presence")).
			Pure(), nil
	},
	Init: func(_ struct{}, init *node.Init) (node.Instance, error) {
		if feed := init.Optional("feed"); feed != nil {
			return present{&feed.ID}, nil
		}
		return present{}, nil
	},
}

func TestEndedFeedsSayWhenAStartWasKnownAndWhenItWent(t *testing.T) {
	feed := node.VideoStream("feed", 1, 2, 2, "rgba", node.R(1, 15))
	h := ok(mock.Open(presentNode, "", picture(), feed))
	ends := int64(31)
	gone := node.Feed{Start: node.FeedStart{At: 20, Known: 12}, Ends: &ends}
	expect(t, len(ok(h.Process(h.Tick(30).WithFrame(0, 30, nil))).Messages("presence")), 0)
	after := h.Tick(33).WithFrame(0, 33, nil).WithEnded(1, gone)
	expect(t, texts(ok(h.Process(after)).Messages("presence")), []string{`33 {"coming":12,"ended":31}`})
}
