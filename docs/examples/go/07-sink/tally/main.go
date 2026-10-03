package main

import (
	"fmt"

	node "github.com/imbcmdth/ffrwd-node/go"
)

// Count is one row per stream, written on the last call.
type Count struct {
	Port      string  `json:"port"`
	Codec     string  `json:"codec"`
	Packets   uint64  `json:"packets"`
	Keyframes uint64  `json:"keyframes"`
	Bytes     uint64  `json:"bytes"`
	Seconds   float64 `json:"seconds"`
}

type Stream struct {
	id       uint32
	timeBase node.Rational
	count    Count
	first    *int64
	last     *int64
}

type Tally struct {
	streams []*Stream
}

var Definition = node.Definition[struct{}]{
	Name:       "tally",
	Version:    "0.1.0",
	RowsSchema: `{"type":"object","properties":{"port":{"type":"string"},"codec":{"type":"string"},"packets":{"type":"integer"},"keyframes":{"type":"integer"},"bytes":{"type":"integer"},"seconds":{"type":"number"}},"required":["port","codec","packets","keyframes","bytes","seconds"]}`,
	Shape: func(struct{}, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.PacketsInput("video").Optional().Many().Arrival()).
			Input(node.PacketsInput("audio").Optional().Many().Arrival()).
			Rate(node.R(10, 1)), nil
	},
	Init: func(_ struct{}, init *node.Init) (node.Instance, error) {
		var streams []*Stream
		for _, stream := range init.All() {
			if stream.Format == nil || stream.Format.Packets == nil {
				return nil, fmt.Errorf("`%s` carries no packets", stream.Port)
			}
			coded := stream.Format.Packets
			streams = append(streams, &Stream{
				id:       stream.ID,
				timeBase: coded.TimeBase,
				count:    Count{Port: stream.Port, Codec: coded.Codec},
			})
		}
		return &Tally{streams: streams}, nil
	},
}

func (t *Tally) Process(tick *node.Tick, out *node.Out) error {
	for _, stream := range t.streams {
		for _, packet := range tick.Packets(stream.id) {
			count := &stream.count
			count.Packets++
			if packet.Keyframe {
				count.Keyframes++
			}
			count.Bytes += uint64(len(packet.Data))
			end := packet.Pts
			if packet.Duration != nil {
				end += *packet.Duration
			}
			first, last := packet.Pts, end
			if stream.first != nil {
				first = min(*stream.first, packet.Pts)
			}
			if stream.last != nil {
				last = max(*stream.last, end)
			}
			stream.first, stream.last = &first, &last
		}
	}
	if tick.Last() {
		for _, stream := range t.streams {
			if stream.first != nil && stream.last != nil {
				stream.count.Seconds = stream.timeBase.Seconds(*stream.last - *stream.first)
			}
			if err := out.Report(stream.count); err != nil {
				return err
			}
		}
	}
	return nil
}

func init() { node.Export(Definition) }

func main() {}
