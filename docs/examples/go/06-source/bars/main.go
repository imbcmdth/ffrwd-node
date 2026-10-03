package main

import (
	"fmt"
	"math"

	node "github.com/imbcmdth/ffrwd-node/go"
)

var colours = [7][4]byte{
	{192, 192, 192, 255},
	{192, 192, 0, 255},
	{0, 192, 192, 255},
	{0, 192, 0, 255},
	{192, 0, 192, 255},
	{192, 0, 0, 255},
	{0, 0, 192, 255},
}

type Params struct {
	Width   uint32   `json:"width"`
	Height  uint32   `json:"height"`
	Fps     float64  `json:"fps"`
	Seconds *float64 `json:"seconds"`
}

type Bars struct {
	width   int
	height  int
	seconds *float64
}

// draw is seven bars, and a white line crossing them once a second.
func (b *Bars) draw(t float64) []byte {
	line := int((t - math.Trunc(t)) * float64(b.width))
	canvas := make([]byte, 0, b.width*b.height*4)
	for range b.height {
		for x := range b.width {
			colour := [4]byte{255, 255, 255, 255}
			if x != line {
				colour = colours[x*len(colours)/b.width]
			}
			canvas = append(canvas, colour[:]...)
		}
	}
	return canvas
}

var Definition = node.Definition[Params]{
	Name:         "bars",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"width":{"type":"integer","minimum":16,"maximum":8192,"default":1280},"height":{"type":"integer","minimum":16,"maximum":8192,"default":720},"fps":{"type":"number","exclusiveMinimum":0,"maximum":240,"default":30},"seconds":{"type":["number","null"],"exclusiveMinimum":0}},"additionalProperties":false}`,
	Shape: func(params Params, _ *node.Bound) (node.Shape, error) {
		width, height := params.Width, params.Height
		return node.NewShape().
			Rate(node.Approximate(params.Fps, 1001)).
			Output(node.VideoOutput("video").
				Size(width, height).
				PixelFormat("rgba").
				Row(0)).
			RelationRow(fmt.Sprintf(`{"width":%d,"height":%d}`, width, height)).
			Bounded(params.Seconds != nil).
			Pure(), nil
	},
	Init: func(params Params, _ *node.Init) (node.Instance, error) {
		return &Bars{width: int(params.Width), height: int(params.Height), seconds: params.Seconds}, nil
	},
}

func (b *Bars) Process(tick *node.Tick, out *node.Out) error {
	if b.seconds != nil && tick.Seconds() >= *b.seconds {
		out.Finish()
		return nil
	}
	canvas := b.draw(tick.Seconds())
	one := int64(1)
	return out.Frame("video", tick.Pts(), &one, canvas)
}

func init() { node.Export(Definition) }

func main() {}
