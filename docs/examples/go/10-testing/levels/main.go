package main

import (
	"errors"
	"math"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Black uint8 `json:"black"`
	White uint8 `json:"white"`
}

type Levels struct {
	v      uint32
	params Params
}

// stretch is value with black moved to 0 and white to 255.
func stretch(value uint8, params Params) uint8 {
	black, white := float64(params.Black), float64(params.White)
	return uint8(min(max(math.Round((float64(value)-black)*255/(white-black)), 0), 255))
}

var Definition = node.Definition[Params]{
	Name:         "levels",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"black":{"type":"integer","minimum":0,"maximum":254,"default":16},"white":{"type":"integer","minimum":1,"maximum":255,"default":235}},"additionalProperties":false}`,
	Shape: func(params Params, _ *node.Bound) (node.Shape, error) {
		if params.Black >= params.White {
			return node.Shape{}, errors.New("levels needs `black` under `white`")
		}
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
		return &Levels{v: v.ID, params: params}, nil
	},
}

func (l *Levels) SetParams(params Params) error {
	l.params = params
	return nil
}

func (l *Levels) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(l.v)
	if !ok {
		return nil
	}
	if l.params == (Params{Black: 0, White: 255}) {
		return out.Pass("v", l.v, frame)
	}
	pixels := tick.Fetch(l.v, frame.Index)
	for at := 0; at+3 < len(pixels); at += 4 {
		for channel := at; channel < at+3; channel++ {
			pixels[channel] = stretch(pixels[channel], l.params)
		}
	}
	return out.Frame("v", frame.Pts, frame.Duration, pixels)
}

func init() { node.Export(Definition) }

func main() {}
