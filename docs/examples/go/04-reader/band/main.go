package main

import (
	"errors"
	"math"
	"slices"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Fade float64 `json:"fade"`
}

type Band struct {
	v      uint32
	width  int
	height int
	fade   float64
	cues   []node.Cue
}

// opacity is how much of the band cue shows at t: rising over fade seconds
// before it starts, whole while it runs, falling over fade after it ends.
func opacity(cue node.Cue, t, fade float64) float64 {
	if fade == 0 {
		if cue.Covers(t) {
			return 1
		}
		return 0
	}
	rising := (t - (cue.StartT - fade)) / fade
	falling := (cue.EndT + fade - t) / fade
	return min(max(min(rising, falling), 0), 1)
}

var Definition = node.Definition[Params]{
	Name:         "band",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"fade":{"type":"number","minimum":0,"maximum":5,"default":0.5}},"additionalProperties":false}`,
	Shape: func(params Params, _ *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Input(node.RowsInput("cues").
				Interval().
				Latency(5).
				Ahead(params.Fade).
				State().
				Schema(node.SchemaOf[node.Cue]())).
			Output(node.LikeOutput("v")).
			Pure().
			OneToOne(), nil
	},
	Init: func(params Params, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		video := v.VideoFormat()
		if video == nil {
			return nil, errors.New("`v` is a video input")
		}
		return &Band{v: v.ID, width: int(video.Width), height: int(video.Height), fade: params.Fade}, nil
	},
}

func (b *Band) Fold(row node.StateRow) error {
	var cue node.Cue
	if err := row.Decode(&cue); err != nil {
		return err
	}
	b.cues = append(b.cues, cue)
	return nil
}

func (b *Band) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(b.v)
	if !ok {
		return nil
	}
	t := tick.TimeBase().Seconds(frame.Pts)
	b.cues = slices.DeleteFunc(b.cues, func(cue node.Cue) bool { return cue.EndT+b.fade <= t })
	shown := 0.0
	for _, cue := range b.cues {
		shown = max(shown, opacity(cue, t, b.fade))
	}
	if shown == 0 {
		return out.Pass("v", b.v, frame)
	}
	pixels := tick.Fetch(b.v, frame.Index)
	keep := 1 - 0.6*shown
	top := b.height * 4 / 5
	for at := top * b.width * 4; at+3 < len(pixels); at += 4 {
		for channel := at; channel < at+3; channel++ {
			pixels[channel] = uint8(math.Round(float64(pixels[channel]) * keep))
		}
	}
	return out.Frame("v", frame.Pts, frame.Duration, pixels)
}

func init() { node.Export(Definition) }

func main() {}
