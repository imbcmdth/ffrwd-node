package main

import (
	"errors"

	node "github.com/imbcmdth/ffrwd-node/go"
)

// Box is the fields boxmask reads. Any row carrying them will do.
type Box struct {
	X float64 `json:"x"`
	Y float64 `json:"y"`
	W float64 `json:"w"`
	H float64 `json:"h"`
}

type BoxMask struct {
	v      uint32
	boxes  uint32
	width  int
	height int
}

var Definition = node.Definition[struct{}]{
	Name:    "boxmask",
	Version: "0.1.0",
	Shape: func(struct{}, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().Timing()).
			Input(node.RowsInput("boxes").Schema(node.SchemaOf[Box]())).
			Output(node.LikeOutput("v").PixelFormat("gray")).
			Pure().
			OneToOne(), nil
	},
	Init: func(_ struct{}, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		video := v.VideoFormat()
		if video == nil {
			return nil, errors.New("`v` is a video input")
		}
		boxes, err := init.Stream("boxes")
		if err != nil {
			return nil, err
		}
		return &BoxMask{v: v.ID, boxes: boxes.ID, width: int(video.Width), height: int(video.Height)}, nil
	},
}

func (b *BoxMask) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(b.v)
	if !ok {
		return nil
	}
	mask := make([]byte, b.width*b.height)
	boxes, err := node.ReadRows[Box](tick, b.boxes)
	if err != nil {
		return err
	}
	for _, found := range boxes {
		x0 := min(int(max(found.X, 0)), b.width)
		y0 := min(int(max(found.Y, 0)), b.height)
		x1 := min(int(max(found.X+found.W, 0)), b.width)
		y1 := min(int(max(found.Y+found.H, 0)), b.height)
		for y := y0; y < y1; y++ {
			row := mask[y*b.width+x0 : y*b.width+max(x1, x0)]
			for x := range row {
				row[x] = 255
			}
		}
	}
	return out.Frame("v", frame.Pts, frame.Duration, mask)
}

func init() { node.Export(Definition) }

func main() {}
