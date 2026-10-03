package node

import (
	"encoding/json"
	"fmt"
	"reflect"
	"slices"
)

type PayloadKind uint8

const (
	// New bytes in the port's format.
	PayloadFrame PayloadKind = iota
	// An input frame's bytes leaving uncopied: frame Index of stream ID this
	// tick.
	PayloadSame
	// One message; empty is a progress mark, never delivered.
	PayloadMessage
	PayloadPacket
)

// Payload is one thing leaving on a port. Its pts and duration are in the
// port's time base; Packet is set for a packet alone.
type Payload struct {
	Kind     PayloadKind
	Pts      int64
	Duration *int64
	Data     []byte
	ID       uint32
	Index    uint32
	Packet   *Packet
}

type Emission struct {
	Port    string
	Payload Payload
}

// Emitted is what one tick produced.
type Emitted struct {
	Items []Emission
	// Rows for the run's rows output.
	Reports  []string
	Finished bool
}

// Messages is the messages that left on port, as pts and text.
func (e Emitted) Messages(port string) []TimedText {
	var messages []TimedText
	for _, payload := range e.On(port) {
		if payload.Kind == PayloadMessage {
			messages = append(messages, TimedText{payload.Pts, string(payload.Data)})
		}
	}
	return messages
}

type TimedText struct {
	Pts  int64
	Text string
}

// On is the payloads that left on port, in order.
func (e Emitted) On(port string) []Payload {
	var payloads []Payload
	for _, item := range e.Items {
		if item.Port == port {
			payloads = append(payloads, item.Payload)
		}
	}
	return payloads
}

type outPort struct {
	name             string
	kind             Kind
	timeBase         *Rational
	lastPts, lastDts *int64
}

// Out is where a tick's emissions go. Each one is checked as it is made:
// the port is one the shape declares and of the payload's kind, and its pts
// never go back on that port, within a call or across calls, nor a packet's
// dts.
type Out struct {
	ports   []*outPort
	clock   Rational
	emitted Emitted
	timing  map[uint32]string
}

// NewOut is where emissions on shape's outputs go; streams in timing, by id,
// are read for their timing alone.
func NewOut(shape *NodeShape, timing map[uint32]string) *Out {
	out := &Out{clock: Micros, timing: timing}
	for _, port := range shape.Outputs {
		out.ports = append(out.ports, &outPort{name: port.Name, kind: port.Kind, timeBase: port.TimeBase})
	}
	return out
}

// Begin starts a call whose clock counts in clock.
func (o *Out) Begin(clock Rational) {
	o.clock = clock
}

// Take is what the call emitted, leaving nothing behind.
func (o *Out) Take() Emitted {
	emitted := o.emitted
	o.emitted = Emitted{}
	return emitted
}

func (o *Out) find(name string) *outPort {
	for _, port := range o.ports {
		if port.name == name {
			return port
		}
	}
	return nil
}

func (o *Out) port(name string, kinds ...Kind) (*outPort, error) {
	port := o.find(name)
	if port == nil {
		return nil, fmt.Errorf("`%s` is not an output of this node", name)
	}
	if !slices.Contains(kinds, port.kind) {
		return nil, fmt.Errorf("`%s` is a %s output", name, port.kind)
	}
	return port, nil
}

func (o *Out) stamp(name string, pts int64, kinds ...Kind) error {
	port, err := o.port(name, kinds...)
	if err != nil {
		return err
	}
	if port.lastPts != nil && pts < *port.lastPts {
		return fmt.Errorf("`%s` would go back from pts %d to %d; a port's pts never decrease", name, *port.lastPts, pts)
	}
	port.lastPts = &pts
	return nil
}

func (o *Out) push(port string, payload Payload) {
	o.emitted.Items = append(o.emitted.Items, Emission{port, payload})
}

// TimeBase is the time base port is counted in: its own, or the clock's.
func (o *Out) TimeBase(port string) (Rational, error) {
	found := o.find(port)
	if found == nil {
		return Rational{}, fmt.Errorf("`%s` is not an output of this node", port)
	}
	if found.timeBase != nil {
		return *found.timeBase, nil
	}
	return o.clock, nil
}

// Pts is seconds as a pts on port.
func (o *Out) Pts(port string, seconds float64) (int64, error) {
	timeBase, err := o.TimeBase(port)
	if err != nil {
		return 0, err
	}
	return timeBase.Pts(seconds), nil
}

// Last is the last pts that left on port, which the next may not be under.
func (o *Out) Last(port string) (int64, bool) {
	if found := o.find(port); found != nil && found.lastPts != nil {
		return *found.lastPts, true
	}
	return 0, false
}

// Frame sends new bytes on a video or audio port: one picture tightly
// packed, or a run of interleaved samples.
func (o *Out) Frame(port string, pts int64, duration *int64, data []byte) error {
	if err := o.stamp(port, pts, Video, Audio); err != nil {
		return err
	}
	o.push(port, Payload{Kind: PayloadFrame, Pts: pts, Duration: duration, Data: data})
	return nil
}

// Same sends frame index of stream id on port uncopied, at pts. Its format
// has to be the port's, and its input not one read for its timing alone.
func (o *Out) Same(port string, pts int64, duration *int64, id, index uint32) error {
	if input, timing := o.timing[id]; timing {
		return fmt.Errorf("a frame of `%s` cannot leave on `%s`: `%s` is read for its timing alone, and the "+
			"host carries none of its bytes", input, port, input)
	}
	if err := o.stamp(port, pts, Video, Audio); err != nil {
		return err
	}
	o.push(port, Payload{Kind: PayloadSame, Pts: pts, Duration: duration, ID: id, Index: index})
	return nil
}

// Pass sends frame of stream id on port unchanged at its own pts and
// duration: a filter passing a picture through.
func (o *Out) Pass(port string, id uint32, frame Frame) error {
	return o.Same(port, frame.Pts, frame.Duration, id, frame.Index)
}

// Message sends one message on a data port.
func (o *Out) Message(port string, pts int64, data []byte) error {
	if err := o.stamp(port, pts, Data); err != nil {
		return err
	}
	o.push(port, Payload{Kind: PayloadMessage, Pts: pts, Data: data})
	return nil
}

// Row sends row as one JSON message on port at pts.
func (o *Out) Row(port string, pts int64, row any) error {
	data, err := json.Marshal(row)
	if err != nil {
		return fmt.Errorf("a row for `%s`: %v", port, err)
	}
	return o.Message(port, pts, data)
}

// Rows sends each element of rows, a slice, as a message on port at pts.
func (o *Out) Rows(port string, pts int64, rows any) error {
	list := reflect.ValueOf(rows)
	if list.Kind() != reflect.Slice && list.Kind() != reflect.Array {
		return fmt.Errorf("the rows for `%s` are a slice, not %T", port, rows)
	}
	for i := range list.Len() {
		if err := o.Row(port, pts, list.Index(i).Interface()); err != nil {
			return err
		}
	}
	return nil
}

// Cue sends cue on port, stamped at its start.
func (o *Out) Cue(port string, cue Cue) error {
	pts, err := o.Pts(port, cue.StartT)
	if err != nil {
		return err
	}
	return o.Row(port, pts, cue)
}

// Progress is a progress mark: nothing more will leave on port stamped
// before pts. Never delivered.
func (o *Out) Progress(port string, pts int64) error {
	return o.Message(port, pts, []byte{})
}

// Packet sends one packet on a packets port, in decode order: its dts never
// decreases.
func (o *Out) Packet(port string, packet Packet) error {
	found, err := o.port(port, Packets)
	if err != nil {
		return err
	}
	if packet.Dts != nil {
		if found.lastDts != nil && *packet.Dts < *found.lastDts {
			return fmt.Errorf("`%s` would go back from dts %d to %d; packets keep decode order", port,
				*found.lastDts, *packet.Dts)
		}
		dts := *packet.Dts
		found.lastDts = &dts
	}
	o.push(port, Payload{Kind: PayloadPacket, Pts: packet.Pts, Duration: packet.Duration, Packet: &packet})
	return nil
}

// Report sends one row for the run's rows output, as a sink writes them.
func (o *Out) Report(row any) error {
	text, err := json.Marshal(row)
	if err != nil {
		return fmt.Errorf("a report row: %v", err)
	}
	o.emitted.Reports = append(o.emitted.Reports, string(text))
	return nil
}

// Finish says nothing more will leave: the host makes the last call and
// ends every output. A node on an input clock ends with it and need not
// say so.
func (o *Out) Finish() {
	o.emitted.Finished = true
}
