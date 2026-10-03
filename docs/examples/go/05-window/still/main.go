package main

import (
	"errors"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Shortest  float64 `json:"shortest"`
	Longest   float64 `json:"longest"`
	Tolerance float64 `json:"tolerance"`
}

type Still struct {
	StartT float64 `json:"start_t"`
	EndT   float64 `json:"end_t"`
}

// opening is where the stretch the picture is still in began: its pts and
// seconds.
type opening struct {
	pts    int64
	startT float64
}

type StillNode struct {
	v uint32
	// The bytes of a picture's luma plane, which come first in yuv420p.
	luma   int
	params Params
	open   *opening
}

// difference is how far apart two pictures' luma planes are: the mean
// difference of a pixel.
func difference(a, b []byte) float64 {
	var total uint64
	for n := range min(len(a), len(b)) {
		if a[n] > b[n] {
			total += uint64(a[n] - b[n])
		} else {
			total += uint64(b[n] - a[n])
		}
	}
	return float64(total) / float64(max(len(a), 1))
}

// close writes the open stretch as ending at endT, if it lasted long
// enough.
func (s *StillNode) close(endT float64, out *node.Out) error {
	open := s.open
	s.open = nil
	if open != nil && endT-open.startT >= s.params.Shortest {
		return out.Row("stills", open.pts, Still{StartT: open.startT, EndT: endT})
	}
	return nil
}

var Definition = node.Definition[Params]{
	Name:         "still",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"shortest":{"type":"number","minimum":0,"default":1},"longest":{"type":"number","exclusiveMinimum":0,"maximum":600,"default":10},"tolerance":{"type":"number","minimum":0,"maximum":255,"default":2}},"additionalProperties":false}`,
	Shape: func(params Params, bound *node.Bound) (node.Shape, error) {
		frame := 1.0
		if rate, ok := bound.RateOf("v"); ok {
			frame = rate.Duration(1)
		}
		return node.NewShape().
			Input(node.VideoInput("v").
				Clock().
				Window(2, 1).
				PixelFormats("yuv420p")).
			Output(node.RowsOutput("stills").
				Latency(params.Longest + frame).
				Schema(node.SchemaOf[Still]())), nil
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
		return &StillNode{v: v.ID, luma: int(video.Width * video.Height), params: params}, nil
	},
}

func (s *StillNode) Process(tick *node.Tick, out *node.Out) error {
	seconds := tick.TimeBase().Seconds
	frames := tick.Frames(s.v)
	if len(frames) != 2 {
		end := tick.Seconds()
		if last, ok := tick.Frame(s.v); ok {
			end = seconds(last.Pts)
		}
		return s.close(end, out)
	}
	before, after := frames[0], frames[1]
	moved := difference(
		tick.Fetch(s.v, before.Index)[:s.luma],
		tick.Fetch(s.v, after.Index)[:s.luma],
	)
	if moved > s.params.Tolerance {
		return s.close(seconds(after.Pts), out)
	}
	if s.open == nil {
		s.open = &opening{before.Pts, seconds(before.Pts)}
	}
	if seconds(after.Pts)-s.open.startT >= s.params.Longest {
		if err := s.close(seconds(after.Pts), out); err != nil {
			return err
		}
		s.open = &opening{after.Pts, seconds(after.Pts)}
	}
	return nil
}

func init() { node.Export(Definition) }

func main() {}
