// Package node writes an ffrwd node module in Go: describe the node in a
// Definition, hand it to Export from the module's init, and build with
// componentize-go. The package carries the ffrwd:av@0.19.1 bindings and
// does what every module would otherwise write for itself: the call
// sequence, params read against their schema, shapes from builders, time in
// any time base, state rows folded, emissions checked to never go back, and
// errors as the run's message.
//
// Everything but the bindings builds on the host too, so a node's own tests
// run there through the mock package.
package node

import (
	"encoding/json"
	"fmt"
	"unicode/utf8"
)

// Instance is an opened node: what Definition.Init answers. Process runs
// once a tick; the last call is the one with tick.Last() set. An instance
// may also implement ParamsSetter and Folder.
type Instance interface {
	Process(tick *Tick, out *Out) error
}

// ParamsSetter takes new params between ticks, whose shape is the
// instance's. Params equal to the ones in force never reach it, and without
// it a change of params is refused.
type ParamsSetter[P any] interface {
	SetParams(params P) error
}

// Folder takes one row of a state input, before the Process that follows:
// the rows of ticks this instance did not process first, oldest first, then
// this tick's. A node declaring a state input implements it.
type Folder interface {
	Fold(row StateRow) error
}

// Definition is a node: its describe, its shape, and how it opens. P is its
// params, decoded with encoding/json from the call's JSON once that is read
// against ParamsSchema.
type Definition[P any] struct {
	Name    string
	Version string
	// The JSON schema of the params: what the compiler checks a call
	// against, and whose defaults fill in what a call leaves out. NoParams
	// when empty.
	ParamsSchema string
	// The JSON schema of one row Out.Report writes; empty when the node
	// writes none.
	RowsSchema string
	// Ordered param names: the language of the node's JSON outputs is the
	// first of these the call sets.
	RowsLanguage []string
	// The ports and clock for these params and for the inputs the call
	// binds. Called at compile time, and again at init with the same hints.
	Shape func(params P, bound *Bound) (Shape, error)
	// Opens an instance on the streams bound to its inputs.
	Init func(params P, init *Init) (Instance, error)
}

// Meta is what describe reports. A node's format lists stay empty: its
// ports say what they accept.
type Meta struct {
	Name, Version, ParamsSchema, RowsSchema string
	RowsLanguage                            []string
}

// Init is what Definition.Init is handed: the streams bound to the node's
// inputs and the outputs the query reads.
type Init struct {
	streams []BoundStream
	latched []string
	shape   *NodeShape
}

// All is every bound stream: ports in the shape's order, a port's streams
// in the order the query named them.
func (i *Init) All() []BoundStream { return i.streams }

// Streams is the streams bound to input port.
func (i *Init) Streams(port string) []BoundStream {
	var streams []BoundStream
	for _, stream := range i.streams {
		if stream.Port == port {
			streams = append(streams, stream)
		}
	}
	return streams
}

// Optional is the one stream bound to input port, or nil when the call left
// it out.
func (i *Init) Optional(port string) *BoundStream {
	for n := range i.streams {
		if i.streams[n].Port == port {
			return &i.streams[n]
		}
	}
	return nil
}

// Stream is the one stream bound to required input port.
func (i *Init) Stream(port string) (*BoundStream, error) {
	if stream := i.Optional(port); stream != nil {
		return stream, nil
	}
	return nil, fmt.Errorf("no stream is bound to `%s`", port)
}

// Latched says whether the query reads output port; one it does not may be
// left unmade.
func (i *Init) Latched(port string) bool {
	for _, name := range i.latched {
		if name == port {
			return true
		}
	}
	return false
}

// Shape is the instance's shape, resolved for what the call binds.
func (i *Init) Shape() *NodeShape { return i.shape }

// StateRow is one row arriving on a state input.
type StateRow struct {
	Port string
	ID   uint32
	// When it arrived, in the stream's time base.
	Pts int64
	// The same, in seconds.
	Seconds float64
	// The row: one JSON object.
	JSON string
}

// Decode reads the row into v.
func (r StateRow) Decode(v any) error {
	if err := Parse(r.JSON, v); err != nil {
		return fmt.Errorf("on `%s`: %v", r.Port, err)
	}
	return nil
}

func (d Definition[P]) schema() string {
	if d.ParamsSchema == "" {
		return NoParams
	}
	return d.ParamsSchema
}

func (d Definition[P]) read(params string) (P, []byte, error) {
	var parsed P
	object, err := ReadParams(d.schema(), params)
	if err != nil {
		return parsed, nil, err
	}
	if err := json.Unmarshal(object, &parsed); err != nil {
		return parsed, nil, fmt.Errorf("the params do not read: %v", err)
	}
	return parsed, object, nil
}

// Describe is what describe reports.
func (d Definition[P]) Describe() Meta {
	return Meta{
		Name:         d.Name,
		Version:      d.Version,
		ParamsSchema: d.schema(),
		RowsSchema:   d.RowsSchema,
		RowsLanguage: append([]string{}, d.RowsLanguage...),
	}
}

// ResolveShape is the shape for params and the inputs bound binds, as the
// host is handed it.
func (d Definition[P]) ResolveShape(params string, bound *Bound) (*NodeShape, error) {
	parsed, _, err := d.read(params)
	if err != nil {
		return nil, err
	}
	shape, err := d.Shape(parsed, bound)
	if err != nil {
		return nil, err
	}
	return shape.Resolve(bound)
}

type stateStream struct {
	port string
	id   uint32
}

// Runner is the call sequence around an opened node, the same in a module
// and in a test: params read against the schema, the shape resolved, state
// rows folded, emissions checked.
type Runner[P any] struct {
	def      Definition[P]
	instance Instance
	shape    *NodeShape
	params   []byte
	ports    map[uint32]string
	timing   map[uint32]string
	state    []stateStream
	out      *Out
}

// Open opens an instance on bound, shaped as the compiler shaped it from
// each stream's hint.
func (d Definition[P]) Open(bound []BoundStream, latched []string, params string) (*Runner[P], error) {
	parsed, object, err := d.read(params)
	if err != nil {
		return nil, err
	}
	hints := BoundOf(bound)
	built, err := d.Shape(parsed, hints)
	if err != nil {
		return nil, err
	}
	shape, err := built.Resolve(hints)
	if err != nil {
		return nil, err
	}
	runner := &Runner[P]{def: d, shape: shape, params: object, ports: map[uint32]string{}, timing: map[uint32]string{}}
	for _, stream := range bound {
		runner.ports[stream.ID] = stream.Port
		if input := shape.FindInput(stream.Port); input != nil {
			if input.Accepts.Wants == WantsTiming {
				runner.timing[stream.ID] = stream.Port
			}
			if input.Rows == RowsState {
				runner.state = append(runner.state, stateStream{stream.Port, stream.ID})
			}
		}
	}
	instance, err := d.Init(parsed, &Init{streams: bound, latched: latched, shape: shape})
	if err != nil {
		return nil, err
	}
	runner.instance = instance
	runner.out = NewOut(shape, runner.timing)
	return runner, nil
}

// SetParams takes new params between ticks; equal ones are taken without
// asking the node.
func (r *Runner[P]) SetParams(params string) error {
	parsed, object, err := r.def.read(params)
	if err != nil {
		return err
	}
	if sameParams(object, r.params) {
		return nil
	}
	setter, ok := r.instance.(ParamsSetter[P])
	if !ok {
		return fmt.Errorf("%s cannot change its params while it runs", r.def.Name)
	}
	if err := setter.SetParams(parsed); err != nil {
		return err
	}
	r.params = object
	return nil
}

// Process runs one tick read from source.
func (r *Runner[P]) Process(source Source) (Emitted, error) {
	r.out.Take()
	tick := &Tick{source: source, ports: r.ports, timing: r.timing}
	r.out.Begin(tick.TimeBase())
	if err := r.fold(tick); err != nil {
		return Emitted{}, err
	}
	processed := r.instance.Process(tick, r.out)
	if tick.refused != nil {
		return Emitted{}, fmt.Errorf("%s fetched a frame of `%s`, which it reads for its timing alone: the host "+
			"carries none of its bytes", r.def.Name, tick.Port(*tick.refused))
	}
	if processed != nil {
		return Emitted{}, processed
	}
	return r.out.Take(), nil
}

func (r *Runner[P]) fold(tick *Tick) error {
	if len(r.state) == 0 {
		return nil
	}
	folder, ok := r.instance.(Folder)
	fold := func(row StateRow) error {
		if !ok {
			return fmt.Errorf("%s declares `%s` as state, so it folds that input's rows: give it a Fold",
				r.def.Name, row.Port)
		}
		return folder.Fold(row)
	}
	for _, state := range r.state {
		timeBase := tick.Info(state.id).TimeBase
		for _, timed := range tick.EarlierRows(state.id) {
			for _, text := range timed.Rows {
				if err := fold(StateRow{state.port, state.id, timed.Pts, timeBase.Seconds(timed.Pts), text}); err != nil {
					return err
				}
			}
		}
	}
	for _, state := range r.state {
		timeBase := tick.Info(state.id).TimeBase
		var arrived []StateRow
		for _, message := range tick.Messages(state.id) {
			if !utf8.Valid(message.Data) {
				return fmt.Errorf("a row on `%s` is not utf-8", state.port)
			}
			arrived = append(arrived, StateRow{state.port, state.id, message.Pts, timeBase.Seconds(message.Pts),
				string(message.Data)})
		}
		for _, frame := range tick.Frames(state.id) {
			for _, text := range frame.Rows {
				arrived = append(arrived, StateRow{state.port, state.id, frame.Pts, timeBase.Seconds(frame.Pts), text})
			}
		}
		for _, row := range arrived {
			if err := fold(row); err != nil {
				return err
			}
		}
	}
	return nil
}

// Instance is the instance Definition.Init answered.
func (r *Runner[P]) Instance() Instance { return r.instance }

// Shape is the instance's shape, resolved.
func (r *Runner[P]) Shape() *NodeShape { return r.shape }

type exportable interface {
	Describe() Meta
	ResolveShape(params string, bound *Bound) (*NodeShape, error)
	open(bound []BoundStream, latched []string, params string) (opened, error)
	name() string
}

type opened interface {
	SetParams(params string) error
	Process(source Source) (Emitted, error)
}

func (d Definition[P]) open(bound []BoundStream, latched []string, params string) (opened, error) {
	return d.Open(bound, latched, params)
}

func (d Definition[P]) name() string { return d.Name }

var exported exportable

// Export makes d the module's node. Call it once, from the module's init.
func Export[P any](d Definition[P]) {
	if d.Shape == nil || d.Init == nil {
		panic(fmt.Sprintf("%s: a node definition has a Shape and an Init", d.Name))
	}
	exported = d
}
