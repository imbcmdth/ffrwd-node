package main

import node "github.com/imbcmdth/ffrwd-node/go"

type Passthrough struct {
	v uint32
}

var Definition = node.Definition[struct{}]{
	Name:    "passthrough",
	Version: "0.1.0",
	Shape: func(struct{}, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Output(node.LikeOutput("v")).
			Pure().
			OneToOne(), nil
	},
	Init: func(_ struct{}, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		return &Passthrough{v: v.ID}, nil
	},
}

func (p *Passthrough) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(p.v)
	if !ok {
		return nil
	}
	// Your work goes here: `tick.Fetch` reads the picture, `out.Frame` sends a new one.
	return out.Pass("v", p.v, frame)
}

func init() { node.Export(Definition) }

func main() {}
