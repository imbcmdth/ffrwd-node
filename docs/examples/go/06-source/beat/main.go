package main

import (
	"bytes"
	"fmt"
	"math"
	"time"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Every  float64 `json:"every"`
	Width  uint32  `json:"width"`
	Height uint32  `json:"height"`
}

type Beat struct {
	every  int64
	next   int64
	pixels int
}

var Definition = node.Definition[Params]{
	Name:         "beat",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"every":{"type":"number","minimum":0.01,"maximum":3600,"default":1},"width":{"type":"integer","minimum":16,"maximum":8192,"default":320},"height":{"type":"integer","minimum":16,"maximum":8192,"default":240}},"additionalProperties":false}`,
	Shape: func(params Params, _ *node.Bound) (node.Shape, error) {
		width, height := params.Width, params.Height
		return node.NewShape().
			SelfClocked().
			Output(node.VideoOutput("video").
				Size(width, height).
				PixelFormat("rgba").
				Row(0)).
			RelationRow(fmt.Sprintf(`{"width":%d,"height":%d}`, width, height)).
			Bounded(false), nil
	},
	Init: func(params Params, _ *node.Init) (node.Instance, error) {
		return &Beat{
			every:  int64(math.Round(params.Every * 1e6)),
			next:   0,
			pixels: int(params.Width * params.Height),
		}, nil
	},
}

func (b *Beat) Process(tick *node.Tick, out *node.Out) error {
	now := tick.Pts()
	if now < b.next {
		time.Sleep(time.Duration(b.next-now) * time.Microsecond)
	}
	wall := time.Now().Unix()
	grey := byte(wall % 8 * 32)
	frame := bytes.Repeat([]byte{grey, grey, grey, 255}, b.pixels)
	every := b.every
	if err := out.Frame("video", b.next, &every, frame); err != nil {
		return err
	}
	b.next += b.every
	return nil
}

func init() { node.Export(Definition) }

func main() {}
