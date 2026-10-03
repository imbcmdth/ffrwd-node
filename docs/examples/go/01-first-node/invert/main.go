package main

import node "github.com/imbcmdth/ffrwd-node/go"

type Invert struct {
	v uint32
}

var Definition = node.Definition[struct{}]{
	Name:    "invert",
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
		return &Invert{v: v.ID}, nil
	},
}

func (i *Invert) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(i.v)
	if !ok {
		return nil
	}
	pixels := tick.Fetch(i.v, frame.Index)
	for at := 0; at+3 < len(pixels); at += 4 {
		for channel := at; channel < at+3; channel++ {
			pixels[channel] = 255 - pixels[channel]
		}
	}
	return out.Frame("v", frame.Pts, frame.Duration, pixels)
}

func init() { node.Export(Definition) }

func main() {}
