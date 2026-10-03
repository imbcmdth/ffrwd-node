package main

import (
	"errors"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Threshold uint8  `json:"threshold"`
	Gap       uint64 `json:"gap"`
}

type Glow struct {
	node.Span
	X uint32 `json:"x"`
	Y uint32 `json:"y"`
	W uint32 `json:"w"`
	H uint32 `json:"h"`
}

type GlowNode struct {
	v         uint32
	width     int
	threshold uint8
	spans     *node.Spans[struct{}]
}

// bright is the box around every pixel of an rgba picture at least
// threshold bright.
func bright(pixels []byte, width int, threshold uint8) ([4]uint32, bool) {
	var x0, y0, x1, y1 int
	found := false
	for at := 0; at*4+3 < len(pixels); at++ {
		pixel := pixels[at*4:]
		luma := (54*uint32(pixel[0]) + 183*uint32(pixel[1]) + 19*uint32(pixel[2])) >> 8
		if luma >= uint32(threshold) {
			x, y := at%width, at/width
			if !found {
				x0, y0, x1, y1, found = x, y, x, y, true
			}
			x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
		}
	}
	return [4]uint32{uint32(x0), uint32(y0), uint32(x1 - x0 + 1), uint32(y1 - y0 + 1)}, found
}

var Definition = node.Definition[Params]{
	Name:         "glow",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"threshold":{"type":"integer","minimum":0,"maximum":255,"default":230},"gap":{"type":"integer","minimum":0,"default":2}},"additionalProperties":false}`,
	Shape: func(Params, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Output(node.RowsOutput("glows").Schema(node.SchemaOf[Glow]())), nil
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
		return &GlowNode{
			v:         v.ID,
			width:     int(video.Width),
			threshold: params.Threshold,
			spans:     node.NewSpans[struct{}]().Gap(params.Gap),
		}, nil
	},
}

func (g *GlowNode) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(g.v)
	if !ok {
		return nil
	}
	g.spans.Tick(tick.TimeBase().Seconds(frame.Pts))
	pixels := tick.Fetch(g.v, frame.Index)
	box, ok := bright(pixels, g.width, g.threshold)
	if !ok {
		return nil
	}
	glow := Glow{
		Span: g.spans.See(struct{}{}),
		X:    box[0],
		Y:    box[1],
		W:    box[2],
		H:    box[3],
	}
	return out.Row("glows", frame.Pts, glow)
}

func init() { node.Export(Definition) }

func main() {}
