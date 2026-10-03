package main

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Window float64  `json:"window"`
	Hop    *float64 `json:"hop"`
}

type Level struct {
	a        uint32
	channels int
	rate     float64
}

// loudness is how loud samples are, as a cue's text: their RMS in dB of
// full scale.
func loudness(samples []float32) string {
	power := 0.0
	for _, sample := range samples {
		power += float64(sample) * float64(sample)
	}
	power /= float64(max(len(samples), 1))
	db := 10 * math.Log10(power)
	if db < -90 {
		return "silence"
	}
	return fmt.Sprintf("%.0f dB", db)
}

var Definition = node.Definition[Params]{
	Name:         "level",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"window":{"type":"number","exclusiveMinimum":0,"maximum":60,"default":2},"hop":{"type":["number","null"],"exclusiveMinimum":0,"maximum":60}},"additionalProperties":false}`,
	Shape: func(params Params, bound *node.Bound) (node.Shape, error) {
		rate, ok := bound.RateOf("a")
		if !ok {
			return node.Shape{}, errors.New("level counts its window in samples, and the call gives `a` no sample rate")
		}
		hop := params.Window
		if params.Hop != nil {
			hop = *params.Hop
		}
		window := uint32(rate.Count(params.Window))
		stride := uint32(rate.Count(hop))
		return node.NewShape().
			Input(node.AudioInput("a").
				Clock().
				Window(window, stride).
				SampleFormats("f32")).
			Output(node.RowsOutput("cues").Schema(node.SchemaOf[node.Cue]())).
			Pure(), nil
	},
	Init: func(_ Params, init *node.Init) (node.Instance, error) {
		a, err := init.Stream("a")
		if err != nil {
			return nil, err
		}
		audio := a.AudioFormat()
		if audio == nil {
			return nil, errors.New("`a` is an audio input")
		}
		return &Level{a: a.ID, channels: int(max(audio.Channels, 1)), rate: float64(audio.SampleRate)}, nil
	},
}

func (l *Level) Process(tick *node.Tick, out *node.Out) error {
	run, ok := tick.Frame(l.a)
	if !ok {
		return nil
	}
	bytes := tick.Fetch(l.a, run.Index)
	samples := make([]float32, len(bytes)/4)
	for n := range samples {
		samples[n] = math.Float32frombits(binary.LittleEndian.Uint32(bytes[n*4:]))
	}
	start := tick.TimeBase().Seconds(run.Pts)
	end := start + float64(len(samples)/l.channels)/l.rate
	cue := node.Cue{StartT: start, EndT: end, Text: loudness(samples)}
	return out.Row("cues", run.Pts, cue)
}

func init() { node.Export(Definition) }

func main() {}
