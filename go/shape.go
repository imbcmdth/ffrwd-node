package node

import (
	"fmt"
	"strings"
)

// Kind is what a port carries.
type Kind uint8

const (
	Video Kind = iota
	Audio
	// Messages; for codec "json", one row each.
	Data
	Packets
)

func (k Kind) String() string {
	return [...]string{"video", "audio", "data", "packets"}[k]
}

func (k Kind) frames() bool {
	return k == Video || k == Audio
}

// RowsUse is what a node does with the rows that arrive on an input.
type RowsUse uint8

const (
	RowsIgnore RowsUse = iota
	// Read while handling the tick they arrive with, and kept nowhere.
	RowsPerFrame
	// Folded into state later ticks depend on: they reach the instance's
	// Fold, earlier ticks' first on a worker that did not process them.
	RowsState
)

// Wants is how much of a stream an input needs.
type Wants uint8

const (
	WantsAll Wants = iota
	// Packets: keyframes alone.
	WantsKeyframes
	// Packets: the first of each stream.
	WantsFirst
	// Frames: their times and the stream's info, never their bytes.
	WantsTiming
)

type AnchorKind uint8

const (
	// The source's pts are on the clock's epoch.
	AnchorSharedClock AnchorKind = iota
	// Held: scheduled Lead seconds ahead of the clock once the host holds
	// that much of the source. By interval: the first message is placed at
	// the tick it arrives on, and every later one keeps that offset.
	AnchorFirstFrame
	// Shared clock for a feed whose tags carry Tag set to 1, first frame
	// otherwise.
	AnchorTagged
)

// Anchor is what maps a stream's pts onto the clock.
type Anchor struct {
	Kind AnchorKind
	Tag  string
}

var (
	SharedClock = Anchor{Kind: AnchorSharedClock}
	FirstFrame  = Anchor{Kind: AnchorFirstFrame}
)

// Tagged is the anchor of a feed timed when its tags carry name set to 1.
func Tagged(name string) Anchor {
	return Anchor{Kind: AnchorTagged, Tag: name}
}

// Hold pairs a frame input by time: the newest frame at or before the tick.
type Hold struct {
	Anchor    Anchor
	Lead      float64
	Linger    *float64
	Timeout   *float64
	Group     *string
	PortParam *string
}

// Interval pairs a message input by time: every message stamped in the
// tick's interval, Ahead seconds past it included.
type Interval struct {
	Latency *float64
	Ahead   float64
	Anchor  Anchor
	// The hold group whose connection the stream arrives on; its anchor is
	// then the shared clock.
	Group *string
}

type PairingKind uint8

const (
	// The clock's pts exactly: same source, one frame per tick.
	PairingLockstep PairingKind = iota
	PairingHold
	PairingInterval
	// As it arrives, unpaired.
	PairingArrival
)

// Pairing is how an input's stream lines up with the clock; Hold or
// Interval is set for those kinds.
type Pairing struct {
	Kind     PairingKind
	Hold     *Hold
	Interval *Interval
}

// Accepts is what an input accepts; an empty list accepts anything of the
// kind.
type Accepts struct {
	PixelFormats  []string
	SampleFormats []string
	SampleRates   []uint32
	ChannelCounts []uint32
	Codecs        []string
	Wants         Wants
	// Conformed by the host to this input's size, or rate and layout.
	Like *string
}

// InputPort is one input as the host is handed it.
type InputPort struct {
	Name     string
	Kind     Kind
	Required bool
	Many     bool
	Pairing  Pairing
	Rows     RowsUse
	Window   uint32
	Stride   uint32
	Accepts  Accepts
	// Data inputs: the JSON schema of the rows read here.
	Schema *string
	// Whether this input is the clock.
	Clock bool
}

// Like is the format of an input, with one field overridden.
type Like struct {
	// The input it follows; nil is the clock input.
	Port         *string
	PixelFormat  *string
	SampleFormat *string
}

// OutputPort is one output as the host is handed it.
type OutputPort struct {
	Name string
	Kind Kind
	// A format of its own; with neither this nor Like, the clock input's.
	Format *Format
	Like   *Like
	// Nil is the clock's.
	TimeBase *Rational
	// How far behind the end of its tick's interval a stamp may fall, in
	// seconds.
	Latency float64
	Schema  *string
	// A source's relation row this output belongs to.
	Row       *uint32
	namedLike bool
}

func ptr[T any](v T) *T {
	return &v
}

// Input builds one input port: VideoInput("v").Clock().PixelFormats("rgba").
type Input struct {
	port InputPort
}

func newInput(name string, kind Kind) Input {
	rows := RowsIgnore
	if kind == Data {
		rows = RowsPerFrame
	}
	return Input{InputPort{Name: name, Kind: kind, Required: true, Rows: rows, Window: 1, Stride: 1}}
}

func VideoInput(name string) Input { return newInput(name, Video) }
func AudioInput(name string) Input { return newInput(name, Audio) }

// RowsInput is a data input of JSON rows, read per frame unless told
// otherwise.
func RowsInput(name string) Input    { return newInput(name, Data) }
func PacketsInput(name string) Input { return newInput(name, Packets) }

// Spec is the port as built so far.
func (in Input) Spec() InputPort { return in.port }

// Clock makes the input the clock: the frames each call sees. Lockstep,
// single and required.
func (in Input) Clock() Input {
	in.port.Clock = true
	in.port.Pairing = Pairing{Kind: PairingLockstep}
	return in
}

// Optional lets the call leave the input out.
func (in Input) Optional() Input {
	in.port.Required = false
	return in
}

// Many takes any number of streams on this one port.
func (in Input) Many() Input {
	in.port.Many = true
	return in
}

// Window is the frames (video) or samples (audio) a clock call sees, and
// how many it consumes: 15, 1 is a sliding window of fifteen frames.
func (in Input) Window(window, stride uint32) Input {
	in.port.Window, in.port.Stride = window, stride
	return in
}

func (in Input) Lockstep() Input {
	in.port.Pairing = Pairing{Kind: PairingLockstep}
	return in
}

func (in Input) Arrival() Input {
	in.port.Pairing = Pairing{Kind: PairingArrival}
	return in
}

// Hold pairs the input by holding the newest frame: anchored on its first
// frame, no lead, no linger, no timeout until the methods below say
// otherwise.
func (in Input) Hold() Input {
	if in.port.Pairing.Kind != PairingHold {
		in.port.Pairing = Pairing{Kind: PairingHold, Hold: &Hold{Anchor: FirstFrame}}
	}
	return in
}

// Interval pairs the input by interval: every message stamped in the tick's
// interval, settled once the producer's progress has passed it.
func (in Input) Interval() Input {
	if in.port.Pairing.Kind != PairingInterval {
		in.port.Pairing = Pairing{Kind: PairingInterval, Interval: &Interval{Anchor: SharedClock}}
	}
	return in
}

func (in Input) held(set func(*Hold)) Input {
	in = in.Hold()
	hold := *in.port.Pairing.Hold
	set(&hold)
	in.port.Pairing.Hold = &hold
	return in
}

func (in Input) intervalled(set func(*Interval)) Input {
	in = in.Interval()
	interval := *in.port.Pairing.Interval
	set(&interval)
	in.port.Pairing.Interval = &interval
	return in
}

func (in Input) timed() Input {
	switch {
	case in.port.Pairing.Kind == PairingHold || in.port.Pairing.Kind == PairingInterval:
		return in
	case in.port.Kind.frames():
		return in.Hold()
	default:
		return in.Interval()
	}
}

// Anchor is what maps the stream's pts onto the clock, held or by interval.
func (in Input) Anchor(anchor Anchor) Input {
	in = in.timed()
	if in.port.Pairing.Kind == PairingHold {
		return in.held(func(h *Hold) { h.Anchor = anchor })
	}
	return in.intervalled(func(i *Interval) { i.Anchor = anchor })
}

func (in Input) Lead(seconds float64) Input {
	return in.held(func(h *Hold) { h.Lead = seconds })
}

func (in Input) Linger(seconds float64) Input {
	return in.held(func(h *Hold) { h.Linger = &seconds })
}

func (in Input) Timeout(seconds float64) Input {
	return in.held(func(h *Hold) { h.Timeout = &seconds })
}

// Group puts the input in a hold group: held inputs of one group arrive on
// one connection from one source, with one offset. A data input naming a
// hold group arrives on that connection, paired by interval.
func (in Input) Group(group string) Input {
	in = in.timed()
	if in.port.Pairing.Kind == PairingHold {
		return in.held(func(h *Hold) { h.Group = &group })
	}
	return in.intervalled(func(i *Interval) { i.Group = &group })
}

// PortParam is the param the host writes this input's loopback port into,
// or reads the port to listen on from.
func (in Input) PortParam(param string) Input {
	return in.held(func(h *Hold) { h.PortParam = &param })
}

// Latency is the most this interval input waits past the interval's end,
// in seconds.
func (in Input) Latency(seconds float64) Input {
	return in.intervalled(func(i *Interval) { i.Latency = &seconds })
}

// Ahead is the seconds past the interval's end whose messages come with it.
func (in Input) Ahead(seconds float64) Input {
	return in.intervalled(func(i *Interval) { i.Ahead = seconds })
}

func (in Input) PerFrame() Input {
	in.port.Rows = RowsPerFrame
	return in
}

func (in Input) State() Input {
	in.port.Rows = RowsState
	return in
}

func (in Input) IgnoreRows() Input {
	in.port.Rows = RowsIgnore
	return in
}

func (in Input) PixelFormats(formats ...string) Input {
	in.port.Accepts.PixelFormats = formats
	return in
}

func (in Input) SampleFormats(formats ...string) Input {
	in.port.Accepts.SampleFormats = formats
	return in
}

func (in Input) SampleRates(rates ...uint32) Input {
	in.port.Accepts.SampleRates = rates
	return in
}

func (in Input) ChannelCounts(counts ...uint32) Input {
	in.port.Accepts.ChannelCounts = counts
	return in
}

func (in Input) Codecs(codecs ...string) Input {
	in.port.Accepts.Codecs = codecs
	return in
}

func (in Input) Wants(wants Wants) Input {
	in.port.Accepts.Wants = wants
	return in
}

// Timing reads the input for its frames' times and its stream's info alone:
// the compiler hands the stream in whatever format is cheapest, and the
// host carries no pixels or samples for it. Fetch on it, or passing one of
// its frames on, is refused.
func (in Input) Timing() Input {
	return in.Wants(WantsTiming)
}

// Like conforms the input to input port's size (video), or rate and layout
// (audio).
func (in Input) Like(port string) Input {
	in.port.Accepts.Like = &port
	return in
}

// Schema is the JSON schema of the rows read here: SchemaOf[Box]().
func (in Input) Schema(schema string) Input {
	in.port.Schema = &schema
	return in
}

// Output builds one output port: LikeOutput("v"),
// VideoOutput("mask").PixelFormat("gray"), RowsOutput("spots").
type Output struct {
	port OutputPort
}

// VideoOutput is a video output: the clock input's format until Following,
// Size or PixelFormat say otherwise.
func VideoOutput(name string) Output { return Output{OutputPort{Name: name, Kind: Video}} }
func AudioOutput(name string) Output { return Output{OutputPort{Name: name, Kind: Audio}} }

// RowsOutput is a data output of JSON rows.
func RowsOutput(name string) Output {
	return Output{OutputPort{Name: name, Kind: Data, Format: &Format{Data: ptr("json")}}}
}

func PacketsOutput(name string) Output { return Output{OutputPort{Name: name, Kind: Packets}} }

// LikeOutput is an output named after input port, of its kind and in its
// format: a filter's v out for its v in.
func LikeOutput(port string) Output {
	return Output{OutputPort{Name: port, Kind: Video, Like: &Like{Port: &port}, namedLike: true}}
}

// Spec is the port as built so far.
func (out Output) Spec() OutputPort { return out.port }

func (out Output) like() *Like {
	if out.port.Like == nil {
		return &Like{}
	}
	like := *out.port.Like
	return &like
}

// Following puts the output in input port's format.
func (out Output) Following(port string) Output {
	like := out.like()
	like.Port = &port
	out.port.Like, out.port.Format = like, nil
	return out
}

// PixelFormat is the output's pixel format: of the input it follows, the
// clock input when it follows none, or of the size Size gave.
func (out Output) PixelFormat(pixFmt string) Output {
	if out.port.Format != nil && out.port.Format.Video != nil {
		video := *out.port.Format.Video
		video.PixFmt = pixFmt
		out.port.Format = &Format{Video: &video}
		return out
	}
	like := out.like()
	like.PixelFormat = &pixFmt
	out.port.Like = like
	return out
}

// SampleFormat is the output's sample format, of the input it follows or
// the clock input.
func (out Output) SampleFormat(sampleFmt string) Output {
	if out.port.Format != nil && out.port.Format.Audio != nil {
		audio := *out.port.Format.Audio
		audio.SampleFmt = sampleFmt
		out.port.Format = &Format{Audio: &audio}
		return out
	}
	like := out.like()
	like.SampleFormat = &sampleFmt
	out.port.Like = like
	return out
}

// Size makes the output pictures of width x height, in the pixel format
// PixelFormat names.
func (out Output) Size(width, height uint32) Output {
	pixFmt := ""
	if out.port.Like != nil && out.port.Like.PixelFormat != nil {
		pixFmt = *out.port.Like.PixelFormat
	}
	out.port.Like = nil
	out.port.Format = &Format{Video: &VideoFormat{Width: width, Height: height, PixFmt: pixFmt}}
	return out
}

func (out Output) VideoFormat(format VideoFormat) Output {
	out.port.Like, out.port.Format = nil, &Format{Video: &format}
	return out
}

func (out Output) AudioFormat(format AudioFormat) Output {
	out.port.Like, out.port.Format = nil, &Format{Audio: &format}
	return out
}

func (out Output) Coded(coded CodedStream) Output {
	out.port.Like, out.port.Format = nil, &Format{Packets: &coded}
	return out
}

func (out Output) TimeBase(timeBase Rational) Output {
	out.port.TimeBase = &timeBase
	return out
}

func (out Output) Latency(seconds float64) Output {
	out.port.Latency = seconds
	return out
}

// Schema is the JSON schema of the rows written here: SchemaOf[Spot]().
func (out Output) Schema(schema string) Output {
	out.port.Schema = &schema
	return out
}

func (out Output) Row(row uint32) Output {
	out.port.Row = &row
	return out
}

type ClockKind uint8

const (
	// No clock given yet: the shape takes its input marked Clock.
	ClockUnset ClockKind = iota
	// The input Port: one call per stride of its frames.
	ClockInput
	// A generator, Rate ticks a second.
	ClockRate
	// A rate clock at input Port's rate.
	ClockRateOf
	// The node emits when it has something.
	ClockSelf
)

// Clock is what drives a node's calls.
type Clock struct {
	Kind ClockKind
	Port string
	Rate Rational
}

// NodeShape is a node's ports and clock for one call, as the host is handed
// them: what Shape.Resolve answers.
type NodeShape struct {
	Inputs   []InputPort
	Outputs  []OutputPort
	Clock    Clock
	Pure     bool
	OneToOne bool
	Bounded  bool
	Relation []string
}

func (s *NodeShape) FindInput(name string) *InputPort {
	for i := range s.Inputs {
		if s.Inputs[i].Name == name {
			return &s.Inputs[i]
		}
	}
	return nil
}

func (s *NodeShape) FindOutput(name string) *OutputPort {
	for i := range s.Outputs {
		if s.Outputs[i].Name == name {
			return &s.Outputs[i]
		}
	}
	return nil
}

// ClockInput is the clock input's name, when an input is the clock.
func (s *NodeShape) ClockInput() (string, bool) {
	return s.Clock.Port, s.Clock.Kind == ClockInput
}

// Shape builds a node's ports and clock. NewShape has no ports, and is
// impure, not one-to-one, and bounded.
type Shape struct {
	shape NodeShape
}

func NewShape() Shape {
	return Shape{NodeShape{Bounded: true}}
}

func (s Shape) Input(in Input) Shape {
	s.shape.Inputs = append(s.shape.Inputs[:len(s.shape.Inputs):len(s.shape.Inputs)], in.port)
	return s
}

func (s Shape) Output(out Output) Shape {
	s.shape.Outputs = append(s.shape.Outputs[:len(s.shape.Outputs):len(s.shape.Outputs)], out.port)
	return s
}

// Rate ticks the node rate times a second.
func (s Shape) Rate(rate Rational) Shape {
	s.shape.Clock = Clock{Kind: ClockRate, Rate: rate}
	return s
}

// RateOf ticks the node at input port's rate, which the compiler reads off
// its first stream.
func (s Shape) RateOf(port string) Shape {
	s.shape.Clock = Clock{Kind: ClockRateOf, Port: port}
	return s
}

func (s Shape) SelfClocked() Shape {
	s.shape.Clock = Clock{Kind: ClockSelf}
	return s
}

// Pure says every call depends only on what it was handed, so the host may
// spread the node over workers.
func (s Shape) Pure() Shape {
	s.shape.Pure = true
	return s
}

// OneToOne says one frame leaves per frame in on the clock's outputs, each
// at its tick's pts.
func (s Shape) OneToOne() Shape {
	s.shape.OneToOne = true
	return s
}

// Bounded says whether a rate or self-clocked node ends by itself; one that
// does not is planned as live.
func (s Shape) Bounded(bounded bool) Shape {
	s.shape.Bounded = bounded
	return s
}

// RelationRow adds one relation row of a source read in FROM, as a JSON
// object.
func (s Shape) RelationRow(row string) Shape {
	s.shape.Relation = append(s.shape.Relation[:len(s.shape.Relation):len(s.shape.Relation)], row)
	return s
}

// Resolve is the shape the host is handed for a call binding bound: the
// clock settled, outputs following an unbound input left out, and every
// rule the host would refuse it by checked here first, with the port named.
func (s Shape) Resolve(bound *Bound) (*NodeShape, error) {
	shape := s.shape
	shape.Inputs = append([]InputPort(nil), s.shape.Inputs...)
	shape.Relation = append([]string(nil), s.shape.Relation...)
	var clocks []string
	for _, in := range shape.Inputs {
		if in.Clock {
			clocks = append(clocks, in.Name)
		}
	}
	switch {
	case len(clocks) > 1:
		return nil, fmt.Errorf("only one input can be the clock, not %s", strings.Join(clocks, " and "))
	case len(clocks) == 0 && shape.Clock.Kind == ClockUnset:
		return nil, fmt.Errorf("the shape has no clock: mark an input Clock(), or give a rate")
	case len(clocks) == 1 && shape.Clock.Kind == ClockUnset:
		shape.Clock = Clock{Kind: ClockInput, Port: clocks[0]}
	case len(clocks) == 1 && (shape.Clock.Kind != ClockInput || shape.Clock.Port != clocks[0]):
		return nil, fmt.Errorf("`%s` is the clock, and so is the shape's rate", clocks[0])
	}
	if err := unique(shape.Inputs, func(in InputPort) string { return in.Name }, "inputs"); err != nil {
		return nil, err
	}
	if err := unique(shape.Outputs, func(out OutputPort) string { return out.Name }, "outputs"); err != nil {
		return nil, err
	}

	clock, inputClock := shape.ClockInput()
	for i := range shape.Inputs {
		if like := shape.Inputs[i].Accepts.Like; like != nil && !bound.Has(*like) {
			shape.Inputs[i].Accepts.Like = nil
		}
	}
	var outputs []OutputPort
	for _, out := range s.shape.Outputs {
		if out.Like != nil {
			like := *out.Like
			var port string
			switch {
			case like.Port != nil:
				port = *like.Port
			case inputClock:
				port = clock
			default:
				return nil, fmt.Errorf("output `%s` takes its format from the clock input, and the clock is not "+
					"an input: give it Following(port) or a format of its own", out.Name)
			}
			in := shape.FindInput(port)
			if in == nil {
				return nil, fmt.Errorf("output `%s` follows `%s`, which is not an input", out.Name, port)
			}
			if !bound.Has(port) {
				continue
			}
			if in.Many {
				return nil, fmt.Errorf("output `%s` follows `%s`, which takes many streams", out.Name, port)
			}
			if out.namedLike {
				out.Kind = in.Kind
			}
			like.Port = &port
			out.Like = &like
		}
		outputs = append(outputs, out)
	}
	shape.Outputs = outputs
	if err := shape.check(bound); err != nil {
		return nil, err
	}
	return &shape, nil
}

func unique[T any](ports []T, name func(T) string, what string) error {
	seen := map[string]bool{}
	for _, port := range ports {
		if seen[name(port)] {
			return fmt.Errorf("two %s are named `%s`", what, name(port))
		}
		seen[name(port)] = true
	}
	return nil
}

func (s *NodeShape) check(bound *Bound) error {
	switch s.Clock.Kind {
	case ClockInput:
		in := s.FindInput(s.Clock.Port)
		if in == nil {
			return fmt.Errorf("the clock is `%s`, which is not an input", s.Clock.Port)
		}
		if !in.Required || in.Many || in.Pairing.Kind != PairingLockstep {
			return fmt.Errorf("the clock `%s` has to be required, single and lockstep", s.Clock.Port)
		}
	case ClockRateOf:
		if s.FindInput(s.Clock.Port) == nil {
			return fmt.Errorf("the rate is `%s`'s, which is not an input", s.Clock.Port)
		}
	case ClockRate:
		if s.Clock.Rate.Num <= 0 || s.Clock.Rate.Den <= 0 {
			return fmt.Errorf("a rate of %d/%d never ticks", s.Clock.Rate.Num, s.Clock.Rate.Den)
		}
	}
	_, inputClock := s.ClockInput()
	for _, in := range s.Inputs {
		name := in.Name
		switch {
		case in.Pairing.Kind == PairingLockstep && !inputClock:
			return fmt.Errorf("`%s` is lockstep, and a node without an input clock has nothing to be in step "+
				"with: hold it, or pair it by interval", name)
		case in.Pairing.Kind == PairingHold && !in.Kind.frames():
			return fmt.Errorf("`%s` carries messages, so it cannot be held", name)
		case in.Pairing.Kind == PairingInterval && in.Kind.frames():
			return fmt.Errorf("`%s` carries frames, so it cannot pair by interval", name)
		case s.Clock.Kind == ClockSelf && in.Pairing.Kind != PairingArrival:
			return fmt.Errorf("`%s` feeds a self-clocked node, so it pairs by arrival", name)
		}
		if in.Kind == Data && in.Rows == RowsIgnore {
			return fmt.Errorf("`%s` carries rows, so it cannot ignore them", name)
		}
		if in.Accepts.Wants == WantsTiming && !in.Kind.frames() {
			return fmt.Errorf("`%s` carries no frames, so it cannot be read for its timing alone", name)
		}
		if in.Pairing.Kind == PairingInterval && in.Pairing.Interval.Group != nil {
			group := *in.Pairing.Interval.Group
			held := false
			for _, other := range s.Inputs {
				if other.Pairing.Kind == PairingHold && other.Pairing.Hold.Group != nil && *other.Pairing.Hold.Group == group {
					held = true
				}
			}
			if !held {
				return fmt.Errorf("`%s` arrives on hold group `%s`, and no hold input is in it", name, group)
			}
			if in.Pairing.Interval.Anchor != SharedClock {
				return fmt.Errorf("`%s` arrives on hold group `%s`, whose first picture fixes its offset, so its "+
					"anchor is the shared clock", name, group)
			}
		}
		if in.Stride == 0 || in.Stride > in.Window {
			return fmt.Errorf("`%s` has a window of %d and a stride of %d: the stride runs from 1 to the window",
				name, in.Window, in.Stride)
		}
		if like := in.Accepts.Like; like != nil {
			other := s.FindInput(*like)
			single := other != nil && !other.Many && other.Kind == in.Kind
			if !single || !bound.Has(*like) {
				return fmt.Errorf("`%s` is conformed to `%s`, which has to be a single bound input of its kind",
					name, *like)
			}
		}
	}
	for _, out := range s.Outputs {
		name := out.Name
		if out.Format != nil && out.Format.kind() != out.Kind {
			return fmt.Errorf("output `%s` is %s with a %s format", name, out.Kind, out.Format.kind())
		}
		if out.Format != nil && out.Format.Video != nil && out.Format.Video.PixFmt == "" {
			return fmt.Errorf("output `%s` has a size and no pixel format", name)
		}
		if out.Format == nil && out.Like == nil && !inputClock {
			return fmt.Errorf("output `%s` takes the clock input's format, and the clock is not an input: "+
				"give it a format", name)
		}
		if out.Like != nil && out.Like.Port != nil {
			if in := s.FindInput(*out.Like.Port); in != nil && in.Kind != out.Kind {
				return fmt.Errorf("output `%s` is %s and follows `%s`, which is %s", name, out.Kind, in.Name, in.Kind)
			}
		}
		if out.Row != nil && int(*out.Row) >= len(s.Relation) {
			return fmt.Errorf("output `%s` belongs to relation row %d, and there are %d", name, *out.Row,
				len(s.Relation))
		}
	}
	return nil
}

// Binding is one input a call binds: its streams, in the order the call
// names them.
type Binding struct {
	Input   string
	Streams []StreamHint
}

// Bound is the inputs a call binds, each with what the compiler knows of its
// streams: how many a many port takes, and their rates.
type Bound struct {
	bindings []Binding
}

// NewBound is names bound, one stream each, at rates unknown.
func NewBound(names ...string) *Bound {
	bound := &Bound{}
	for _, name := range names {
		bound.Bind(name, nil)
	}
	return bound
}

// BoundFrom is the bindings shape is handed.
func BoundFrom(bindings []Binding) *Bound {
	return &Bound{bindings}
}

// BoundOf is the streams init binds, each with its hint.
func BoundOf(streams []BoundStream) *Bound {
	bound := &Bound{}
	for _, stream := range streams {
		if binding := bound.find(stream.Port); binding != nil {
			binding.Streams = append(binding.Streams, stream.Hint)
		} else {
			bound.bindings = append(bound.bindings, Binding{stream.Port, []StreamHint{stream.Hint}})
		}
	}
	return bound
}

func (b *Bound) find(port string) *Binding {
	for i := range b.bindings {
		if b.bindings[i].Input == port {
			return &b.bindings[i]
		}
	}
	return nil
}

// Bind binds port to streams at these rates, nil where unknown, in place of
// whatever it was bound to.
func (b *Bound) Bind(port string, rates ...*Rational) *Bound {
	if len(rates) == 0 {
		rates = []*Rational{nil}
	}
	streams := make([]StreamHint, len(rates))
	for i, rate := range rates {
		streams[i] = StreamHint{Rate: rate}
	}
	if binding := b.find(port); binding != nil {
		binding.Streams = streams
	} else {
		b.bindings = append(b.bindings, Binding{port, streams})
	}
	return b
}

// Rate puts every stream of port at rate; one stream when it was not bound.
func (b *Bound) Rate(port string, rate Rational) *Bound {
	if !b.Has(port) {
		b.Bind(port, nil)
	}
	binding := b.find(port)
	for i := range binding.Streams {
		binding.Streams[i].Rate = ptr(rate)
	}
	return b
}

// Inputs is every input the call binds, in its order.
func (b *Bound) Inputs() []Binding { return b.bindings }

// Has says whether the call binds input port.
func (b *Bound) Has(port string) bool { return b.find(port) != nil }

// Streams is the stream hints bound to port; none when the call leaves it
// out.
func (b *Bound) Streams(port string) []StreamHint {
	if binding := b.find(port); binding != nil {
		return binding.Streams
	}
	return nil
}

// Count is how many streams the call binds to port.
func (b *Bound) Count(port string) int { return len(b.Streams(port)) }

// RateOf is the rate of port's first stream, the only one of a single port.
func (b *Bound) RateOf(port string) (Rational, bool) {
	streams := b.Streams(port)
	if len(streams) == 0 || streams[0].Rate == nil {
		return Rational{}, false
	}
	return *streams[0].Rate, true
}
