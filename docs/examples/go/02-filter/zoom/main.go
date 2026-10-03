package main

import (
	"errors"
	"math"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Amount float64 `json:"amount"`
	X      float64 `json:"x"`
	Y      float64 `json:"y"`
}

type Zoom struct {
	v      uint32
	width  int
	height int
	params Params
}

// crop is the part of the picture that fills the frame: 1 / amount of each
// side, centred on x, y as far as the picture allows.
func (z *Zoom) crop() Rect {
	width, height := float64(z.width), float64(z.height)
	w := math.Max(math.Round(width/z.params.Amount), 1)
	h := math.Max(math.Round(height/z.params.Amount), 1)
	x0 := int(min(max(z.params.X*width-w/2, 0), width-w))
	y0 := int(min(max(z.params.Y*height-h/2, 0), height-h))
	return Rect{X0: x0, Y0: y0, X1: x0 + int(w), Y1: y0 + int(h)}
}

// opaque is interleaved red, green and blue back to opaque rgba.
func opaque(rgb []byte) []byte {
	pixels := make([]byte, 0, len(rgb)/3*4)
	for at := 0; at+2 < len(rgb); at += 3 {
		pixels = append(pixels, rgb[at], rgb[at+1], rgb[at+2], 255)
	}
	return pixels
}

var Definition = node.Definition[Params]{
	Name:         "zoom",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"amount":{"type":"number","minimum":1,"maximum":16,"default":2},"x":{"type":"number","minimum":0,"maximum":1,"default":0.5},"y":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}`,
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
		video := v.VideoFormat()
		if video == nil {
			return nil, errors.New("`v` is a video input")
		}
		return &Zoom{v: v.ID, width: int(video.Width), height: int(video.Height), params: params}, nil
	},
}

func (z *Zoom) SetParams(params Params) error {
	z.params = params
	return nil
}

func (z *Zoom) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(z.v)
	if !ok {
		return nil
	}
	if z.params.Amount == 1 {
		return out.Pass("v", z.v, frame)
	}
	pixels := tick.Fetch(z.v, frame.Index)
	rgb, err := resize(pixels, z.width, z.height, z.crop(), z.width, z.height)
	if err != nil {
		return err
	}
	return out.Frame("v", frame.Pts, frame.Duration, opaque(rgb))
}

func init() { node.Export(Definition) }

func main() {}
