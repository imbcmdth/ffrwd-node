package node

import "fmt"

// Source is what a tick reads from: the host's tick in a module, a
// mock.Tick in a test.
type Source interface {
	Pts() int64
	Ordinal() uint64
	TimeBase() Rational
	Last() bool
	Streams(port string) []uint32
	Info(id uint32) StreamInfo
	Feed(id uint32) *Feed
	EndedFeeds(id uint32) []Feed
	Frames(id uint32) []Frame
	Fetch(id, index uint32) []byte
	Messages(id uint32) []Message
	Packets(id uint32) []Packet
	EarlierRows(id uint32) []TimedRows
}

// Tick is one call's inputs, held by the host for exactly that call.
// Streams are named by the ids init gave them; an id it did not give, or an
// index past a stream's frames, is a fault that stops the run.
type Tick struct {
	source  Source
	ports   map[uint32]string
	timing  map[uint32]string
	refused *uint32
}

// Pts is the tick's time in TimeBase: an input clock's first frame this
// call, a rate clock's tick number, a self-clocked node's microseconds since
// its first call.
func (t *Tick) Pts() int64 { return t.source.Pts() }

// Ordinal is the tick's number in the run, from 0, counted over every
// instance of the node. A node that numbers things by frame counts with it
// and stays pure.
func (t *Tick) Ordinal() uint64 { return t.source.Ordinal() }

// TimeBase is the clock input's time base, the inverse of a rate clock's
// rate, or microseconds.
func (t *Tick) TimeBase() Rational { return t.source.TimeBase() }

// Seconds is the tick's time in seconds.
func (t *Tick) Seconds() float64 { return t.TimeBase().Seconds(t.Pts()) }

// Last says whether this is the instance's final call, which happens
// exactly once.
func (t *Tick) Last() bool { return t.source.Last() }

// Streams is the streams bound to input port, in init's order.
func (t *Tick) Streams(port string) []uint32 { return t.source.Streams(port) }

// Stream is the first stream bound to input port: the only one of a single
// port, and false for an optional port the call left out.
func (t *Tick) Stream(port string) (uint32, bool) {
	streams := t.Streams(port)
	if len(streams) == 0 {
		return 0, false
	}
	return streams[0], true
}

// Info is the stream as the host knows it this tick, time base included.
func (t *Tick) Info(id uint32) StreamInfo { return t.source.Info(id) }

// Feed is a hold input's feed as it stands this tick; nil while nothing
// shows.
func (t *Tick) Feed(id uint32) *Feed { return t.source.Feed(id) }

// EndedFeeds is a hold input's feeds that ended since this instance's
// previous call, oldest first, each with Ends set to the last tick it
// showed on.
func (t *Tick) EndedFeeds(id uint32) []Feed { return t.source.EndedFeeds(id) }

// Frames is the frames this tick hands on stream id, oldest first.
func (t *Tick) Frames(id uint32) []Frame { return t.source.Frames(id) }

// Frame is the newest frame this tick hands on stream id: the only one a
// window of one, a hold input or an audio input hands.
func (t *Tick) Frame(id uint32) (Frame, bool) {
	frames := t.Frames(id)
	if len(frames) == 0 {
		return Frame{}, false
	}
	return frames[len(frames)-1], true
}

// Fetch is frame index's bytes, copied on demand: pixels tightly packed, or
// interleaved samples. An input read for its timing alone has none: the
// call gets nothing back, and ends the run with the port named once it
// returns, where the host would fault.
func (t *Tick) Fetch(id, index uint32) []byte {
	if _, timing := t.timing[id]; timing {
		if t.refused == nil {
			t.refused = &id
		}
		return nil
	}
	return t.source.Fetch(id, index)
}

// Messages is the messages this tick hands on data stream id, in pts order.
func (t *Tick) Messages(id uint32) []Message { return t.source.Messages(id) }

// Packets is the packets this tick hands on packets stream id, in decode
// order.
func (t *Tick) Packets(id uint32) []Packet { return t.source.Packets(id) }

// EarlierRows is the rows of the ticks this instance did not process, on a
// state input. They are folded for the node, so it rarely reads them
// itself.
func (t *Tick) EarlierRows(id uint32) []TimedRows { return t.source.EarlierRows(id) }

// Port is the input port stream id is bound to.
func (t *Tick) Port(id uint32) string {
	if port, ok := t.ports[id]; ok {
		return port
	}
	return "?"
}

// ReadRows reads every row this tick hands on stream id as a T: a data stream's
// messages, or the rows riding a frame stream's frames.
func ReadRows[T any](t *Tick, id uint32) ([]T, error) {
	var rows []T
	read := func(decode func(*T) error) error {
		var row T
		if err := decode(&row); err != nil {
			return fmt.Errorf("on `%s`: %v", t.Port(id), err)
		}
		rows = append(rows, row)
		return nil
	}
	if messages := t.Messages(id); len(messages) > 0 {
		for _, message := range messages {
			if err := read(func(row *T) error { return message.Decode(row) }); err != nil {
				return nil, err
			}
		}
		return rows, nil
	}
	for _, frame := range t.Frames(id) {
		for _, text := range frame.Rows {
			if err := read(func(row *T) error { return Parse(text, row) }); err != nil {
				return nil, err
			}
		}
	}
	return rows, nil
}
