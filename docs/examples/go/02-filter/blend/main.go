package main

import (
	"math"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Mix float64 `json:"mix"`
}

type Blend struct {
	v    uint32
	over uint32
	mix  float64
}

var Definition = node.Definition[Params]{
	Name:         "blend",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"mix":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}`,
	Shape: func(Params, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Input(node.VideoInput("over").
				Lockstep().
				Like("v").
				PixelFormats("rgba")).
			Output(node.LikeOutput("v")).
			Pure().
			OneToOne(), nil
	},
	Init: func(params Params, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		over, err := init.Stream("over")
		if err != nil {
			return nil, err
		}
		return &Blend{v: v.ID, over: over.ID, mix: params.Mix}, nil
	},
}

func (b *Blend) SetParams(params Params) error {
	b.mix = params.Mix
	return nil
}

func (b *Blend) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(b.v)
	if !ok {
		return nil
	}
	top, ok := tick.Frame(b.over)
	if !ok {
		return out.Pass("v", b.v, frame)
	}
	if b.mix == 0 {
		return out.Pass("v", b.v, frame)
	}
	if b.mix == 1 {
		return out.Same("v", frame.Pts, frame.Duration, b.over, top.Index)
	}
	pixels := tick.Fetch(b.v, frame.Index)
	over := tick.Fetch(b.over, top.Index)
	for at := range min(len(pixels), len(over)) {
		under := float64(pixels[at])
		mixed := under + (float64(over[at])-under)*b.mix
		pixels[at] = uint8(math.Round(mixed))
	}
	return out.Frame("v", frame.Pts, frame.Duration, pixels)
}

func init() { node.Export(Definition) }

func main() {}
