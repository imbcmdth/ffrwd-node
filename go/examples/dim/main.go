// Command dim is every picture darkened by amount, 0 leaving it as it was
// and 1 making it black. The same node as rust/examples/dim.rs and
// js/examples/dim.js.
package main

import (
	"math"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Amount float64 `json:"amount"`
}

type Dim struct {
	v      uint32
	amount float64
}

// darken scales every colour byte of an rgba picture by 1 - amount, alpha
// kept.
func darken(pixels []byte, amount float64) {
	keep := uint32(math.Round((1 - min(max(amount, 0), 1)) * 256))
	for at := 0; at+3 < len(pixels); at += 4 {
		for channel := at; channel < at+3; channel++ {
			pixels[channel] = byte(uint32(pixels[channel]) * keep >> 8)
		}
	}
}

var Definition = node.Definition[Params]{
	Name:         "dim",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"amount":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}`,
	Shape: func(Params, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Output(node.LikeOutput("v")).
			Pure().
			OneToOne(), nil
	},
	Init: func(params Params, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		return &Dim{v: v.ID, amount: params.Amount}, nil
	},
}

func (d *Dim) SetParams(params Params) error {
	d.amount = params.Amount
	return nil
}

func (d *Dim) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(d.v)
	if !ok {
		return nil
	}
	if d.amount == 0 {
		return out.Pass("v", d.v, frame)
	}
	pixels := tick.Fetch(d.v, frame.Index)
	darken(pixels, d.amount)
	return out.Frame("v", frame.Pts, frame.Duration, pixels)
}

func init() {
	node.Export(Definition)
}

func main() {}
