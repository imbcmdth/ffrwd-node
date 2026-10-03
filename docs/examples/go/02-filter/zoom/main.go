package main

import (
	"errors"
	"math"

	node "github.com/imbcmdth/ffrwd-node/go"
	"github.com/imbcmdth/ffrwd-node/go/frame"
)

// eightBits is what frame.Planes divides by to hand back eight-bit values
// unchanged.
var eightBits = frame.Norm{Std: [3]float32{1.0 / 255, 1.0 / 255, 1.0 / 255}}

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
func (z *Zoom) crop() frame.Rect {
	width, height := float64(z.width), float64(z.height)
	w := math.Max(math.Round(width/z.params.Amount), 1)
	h := math.Max(math.Round(height/z.params.Amount), 1)
	x0 := int(min(max(z.params.X*width-w/2, 0), width-w))
	y0 := int(min(max(z.params.Y*height-h/2, 0), height-h))
	return frame.Rect{X0: x0, Y0: y0, X1: x0 + int(w), Y1: y0 + int(h)}
}

// interleave is planar red, green and blue back to opaque rgba.
func interleave(planes []float32, pixels int) []byte {
	rgba := make([]byte, 0, pixels*4)
	for at := range pixels {
		for _, c := range [4]float32{planes[at], planes[pixels+at], planes[2*pixels+at], 255} {
			rgba = append(rgba, byte(min(max(math.Round(float64(c)), 0), 255)))
		}
	}
	return rgba
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
	in, ok := tick.Frame(z.v)
	if !ok {
		return nil
	}
	if z.params.Amount == 1 {
		return out.Pass("v", z.v, in)
	}
	pixels := tick.Fetch(z.v, in.Index)
	picture, err := frame.NewRgba(pixels, z.width, z.height)
	if err != nil {
		return err
	}
	width, height := z.width, z.height
	rgb := frame.Planes(picture, z.crop(), width, height, frame.Bilinear, eightBits)
	zoomed := interleave(rgb, width*height)
	return out.Frame("v", in.Pts, in.Duration, zoomed)
}

func init() { node.Export(Definition) }

func main() {}
