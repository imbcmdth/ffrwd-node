package main

import (
	"bytes"
	"errors"
	"math"

	node "github.com/imbcmdth/ffrwd-node/go"
	"github.com/imbcmdth/ffrwd-node/go/frame"
)

// eightBits is what frame.Planes divides by to hand back eight-bit values
// unchanged.
var eightBits = frame.Norm{Std: [3]float32{1.0 / 255, 1.0 / 255, 1.0 / 255}}

var down = [4]byte{48, 48, 48, 255}

type Params struct {
	Columns int    `json:"columns"`
	Width   uint32 `json:"width"`
	Height  uint32 `json:"height"`
}

type Tile struct {
	id     uint32
	width  int
	height int
	cell   frame.Rect
}

type Mosaic struct {
	tiles  []Tile
	width  int
	height int
}

// put is pixels, a tile's picture, resized into its cell of canvas.
func (m *Mosaic) put(canvas []byte, tile Tile, pixels []byte) error {
	picture, err := frame.NewRgba(pixels, tile.width, tile.height)
	if err != nil {
		return err
	}
	w, h := tile.cell.Width(), tile.cell.Height()
	whole := frame.Whole(tile.width, tile.height)
	rgb := frame.Planes(picture, whole, w, h, frame.Bilinear, eightBits)
	for y := range h {
		for x := range w {
			at := ((tile.cell.Y0+y)*m.width + tile.cell.X0 + x) * 4
			for channel := range 3 {
				value := rgb[channel*w*h+y*w+x]
				canvas[at+channel] = byte(min(max(math.Round(float64(value)), 0), 255))
			}
		}
	}
	return nil
}

func (m *Mosaic) fill(canvas []byte, cell frame.Rect, colour [4]byte) {
	for y := cell.Y0; y < cell.Y1; y++ {
		row := canvas[(y*m.width+cell.X0)*4 : (y*m.width+cell.X1)*4]
		for at := 0; at < len(row); at += 4 {
			copy(row[at:at+4], colour[:])
		}
	}
}

var Definition = node.Definition[Params]{
	Name:         "mosaic",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"columns":{"type":"integer","minimum":1,"default":2},"width":{"type":"integer","minimum":16,"default":1280},"height":{"type":"integer","minimum":16,"default":720}},"additionalProperties":false}`,
	Shape: func(params Params, _ *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").
				Many().
				Hold().
				Anchor(node.SharedClock).
				PixelFormats("rgba")).
			Output(node.VideoOutput("v").
				Size(params.Width, params.Height).
				PixelFormat("rgba")).
			RateOf("v").
			Pure(), nil
	},
	Init: func(params Params, init *node.Init) (node.Instance, error) {
		width, height := int(params.Width), int(params.Height)
		streams := init.Streams("v")
		columns := max(min(params.Columns, len(streams)), 1)
		rows := max((len(streams)+columns-1)/columns, 1)
		var tiles []Tile
		for n, stream := range streams {
			video := stream.VideoFormat()
			if video == nil {
				return nil, errors.New("`v` takes pictures")
			}
			column, row := n%columns, n/columns
			tiles = append(tiles, Tile{
				id:     stream.ID,
				width:  int(video.Width),
				height: int(video.Height),
				cell: frame.Rect{
					X0: column * width / columns,
					Y0: row * height / rows,
					X1: (column + 1) * width / columns,
					Y1: (row + 1) * height / rows,
				},
			})
		}
		return &Mosaic{tiles: tiles, width: width, height: height}, nil
	},
}

func (m *Mosaic) Process(tick *node.Tick, out *node.Out) error {
	canvas := bytes.Repeat([]byte{0, 0, 0, 255}, m.width*m.height)
	shown := false
	for _, tile := range m.tiles {
		in, ok := tick.Frame(tile.id)
		switch {
		case ok:
			if err := m.put(canvas, tile, tick.Fetch(tile.id, in.Index)); err != nil {
				return err
			}
			shown = true
		case tick.Feed(tile.id) == nil:
			m.fill(canvas, tile.cell, down)
		}
	}
	if !shown && tick.Last() {
		return nil
	}
	one := int64(1)
	return out.Frame("v", tick.Pts(), &one, canvas)
}

func init() { node.Export(Definition) }

func main() {}
