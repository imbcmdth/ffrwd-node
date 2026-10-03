// Package mock runs a node's calls on the host, for its unit tests: a
// Harness runs the same call sequence a module does, on a Tick built by
// hand.
package mock

import (
	"encoding/json"
	"fmt"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type bound struct {
	port string
	id   uint32
}

type frameData struct {
	frame node.Frame
	data  []byte
}

// Tick is one tick's inputs, built by hand. An id it was not told of, or an
// index past a stream's frames, panics, as the host stops the run.
type Tick struct {
	pts      int64
	ordinal  uint64
	timeBase node.Rational
	last     bool
	streams  []bound
	infos    map[uint32]node.StreamInfo
	frames   map[uint32][]frameData
	messages map[uint32][]node.Message
	packets  map[uint32][]node.Packet
	feeds    map[uint32]node.Feed
	ended    map[uint32][]node.Feed
	earlier  map[uint32][]node.TimedRows
}

// NewTick is a tick at pts of a clock counted in timeBase, with no streams,
// the run's first.
func NewTick(pts int64, timeBase node.Rational) *Tick {
	return &Tick{
		pts:      pts,
		timeBase: timeBase,
		infos:    map[uint32]node.StreamInfo{},
		frames:   map[uint32][]frameData{},
		messages: map[uint32][]node.Message{},
		packets:  map[uint32][]node.Packet{},
		feeds:    map[uint32]node.Feed{},
		ended:    map[uint32][]node.Feed{},
		earlier:  map[uint32][]node.TimedRows{},
	}
}

// WithOrdinal numbers the tick in the run: what a worker handed every other
// tick sees on its own.
func (t *Tick) WithOrdinal(ordinal uint64) *Tick {
	t.ordinal = ordinal
	return t
}

// Bind puts stream on its port.
func (t *Tick) Bind(stream node.BoundStream) *Tick {
	t.streams = append(t.streams, bound{stream.Port, stream.ID})
	t.infos[stream.ID] = stream.Info
	return t
}

// Final makes this the instance's final call.
func (t *Tick) Final() *Tick {
	t.last = true
	return t
}

// WithFrame adds a frame of data at pts on stream id, after the ones
// already there.
func (t *Tick) WithFrame(id uint32, pts int64, data []byte) *Tick {
	return t.WithFrameRows(id, pts, nil, nil, data)
}

// WithFrameRows adds a frame with a duration and rows riding it.
func (t *Tick) WithFrameRows(id uint32, pts int64, duration *int64, rows []string, data []byte) *Tick {
	frame := node.Frame{Pts: pts, Index: uint32(len(t.frames[id])), Duration: duration, Rows: rows}
	t.frames[id] = append(t.frames[id], frameData{frame, data})
	return t
}

// WithMessage adds a message on data stream id.
func (t *Tick) WithMessage(id uint32, pts int64, data []byte) *Tick {
	t.messages[id] = append(t.messages[id], node.Message{Pts: pts, Data: data})
	return t
}

// WithRow adds row as a JSON message on data stream id.
func (t *Tick) WithRow(id uint32, pts int64, row any) *Tick {
	data, err := json.Marshal(row)
	if err != nil {
		panic(err)
	}
	return t.WithMessage(id, pts, data)
}

func (t *Tick) WithPacket(id uint32, packet node.Packet) *Tick {
	t.packets[id] = append(t.packets[id], packet)
	return t
}

// WithFeed sets hold input id's feed as it stands on this tick.
func (t *Tick) WithFeed(id uint32, feed node.Feed) *Tick {
	t.feeds[id] = feed
	return t
}

// WithEnded adds a feed of hold input id that ended since the instance's
// previous call, after the ones already there.
func (t *Tick) WithEnded(id uint32, feed node.Feed) *Tick {
	t.ended[id] = append(t.ended[id], feed)
	return t
}

// WithEarlier adds rows a state input received on a tick this instance did
// not process.
func (t *Tick) WithEarlier(id uint32, pts int64, rows ...string) *Tick {
	t.earlier[id] = append(t.earlier[id], node.TimedRows{Pts: pts, Rows: rows})
	return t
}

func (t *Tick) known(id uint32) {
	if _, ok := t.infos[id]; !ok {
		panic(fmt.Sprintf("stream %d is not bound on this tick", id))
	}
}

func (t *Tick) Pts() int64              { return t.pts }
func (t *Tick) Ordinal() uint64         { return t.ordinal }
func (t *Tick) TimeBase() node.Rational { return t.timeBase }
func (t *Tick) Last() bool              { return t.last }

func (t *Tick) Streams(port string) []uint32 {
	var ids []uint32
	for _, stream := range t.streams {
		if stream.port == port {
			ids = append(ids, stream.id)
		}
	}
	return ids
}

func (t *Tick) Info(id uint32) node.StreamInfo {
	t.known(id)
	return t.infos[id]
}

func (t *Tick) Feed(id uint32) *node.Feed {
	t.known(id)
	if feed, ok := t.feeds[id]; ok {
		return &feed
	}
	return nil
}

func (t *Tick) EndedFeeds(id uint32) []node.Feed {
	t.known(id)
	return t.ended[id]
}

func (t *Tick) Frames(id uint32) []node.Frame {
	t.known(id)
	frames := make([]node.Frame, len(t.frames[id]))
	for i, held := range t.frames[id] {
		frames[i] = held.frame
	}
	return frames
}

func (t *Tick) Fetch(id, index uint32) []byte {
	t.known(id)
	if int(index) >= len(t.frames[id]) {
		panic(fmt.Sprintf("stream %d has no frame %d on this tick", id, index))
	}
	return append([]byte(nil), t.frames[id][index].data...)
}

func (t *Tick) Messages(id uint32) []node.Message {
	t.known(id)
	return t.messages[id]
}

func (t *Tick) Packets(id uint32) []node.Packet {
	t.known(id)
	return t.packets[id]
}

func (t *Tick) EarlierRows(id uint32) []node.TimedRows {
	t.known(id)
	return t.earlier[id]
}

// Harness is a node opened on the host: shape and init as the host calls
// them, every output latched, and ticks that come with the bound streams in
// place, numbered from 0 on.
type Harness[P any] struct {
	runner *node.Runner[P]
	bound  []node.BoundStream
	clock  *node.Rational
	next   uint64
}

// Open opens d with params on bound, shaped with each stream's hint as the
// compiler and init both shape it.
func Open[P any](d node.Definition[P], params string, bound ...node.BoundStream) (*Harness[P], error) {
	shape, err := d.ResolveShape(params, node.BoundOf(bound))
	if err != nil {
		return nil, err
	}
	var latched []string
	for _, output := range shape.Outputs {
		latched = append(latched, output.Name)
	}
	runner, err := d.Open(bound, latched, params)
	if err != nil {
		return nil, err
	}
	h := &Harness[P]{runner: runner, bound: bound}
	switch clock := runner.Shape().Clock; clock.Kind {
	case node.ClockInput:
		for _, stream := range bound {
			if stream.Port == clock.Port {
				h.clock = &stream.Info.TimeBase
				break
			}
		}
	case node.ClockRate:
		inverse := clock.Rate.Inverse()
		h.clock = &inverse
	case node.ClockSelf:
		h.clock = &node.Micros
	}
	return h, nil
}

// Clock sets the clock's time base, for a node whose clock is another
// input's rate.
func (h *Harness[P]) Clock(timeBase node.Rational) *Harness[P] {
	h.clock = &timeBase
	return h
}

// Tick is a tick at pts on the clock, every bound stream in place, numbered
// one past the last this harness processed.
func (h *Harness[P]) Tick(pts int64) *Tick {
	if h.clock == nil {
		panic("the clock's time base is the rate of an input; give it with Clock")
	}
	tick := NewTick(pts, *h.clock).WithOrdinal(h.next)
	for _, stream := range h.bound {
		tick.Bind(stream)
	}
	return tick
}

// Process is one process call.
func (h *Harness[P]) Process(tick *Tick) (node.Emitted, error) {
	h.next = tick.ordinal + 1
	return h.runner.Process(tick)
}

func (h *Harness[P]) SetParams(params string) error {
	return h.runner.SetParams(params)
}

// Instance is the instance the node's Init answered.
func (h *Harness[P]) Instance() node.Instance {
	return h.runner.Instance()
}

// Shape is the instance's shape, as the package resolved it at init.
func (h *Harness[P]) Shape() *node.NodeShape {
	return h.runner.Shape()
}
