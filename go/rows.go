package node

import (
	"encoding/json"
	"reflect"
	"sort"
	"strings"
)

// SchemaOf is the JSON schema of a row type, from its fields as
// encoding/json writes them: a float is number, an integer integer, then
// string, boolean, array and nested object, each required. A pointer, an
// interface or a field marked omitempty takes any type and is not
// required; an embedded struct's fields are the row's own, as
// encoding/json flattens them. Other fields are allowed, as a reader naming
// only the fields it reads wants.
func SchemaOf[T any]() string {
	text, _ := json.Marshal(schemaFor(reflect.TypeFor[T]()))
	return string(text)
}

func schemaFor(t reflect.Type) map[string]any {
	switch t.Kind() {
	case reflect.Bool:
		return map[string]any{"type": "boolean"}
	case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64,
		reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64:
		return map[string]any{"type": "integer"}
	case reflect.Float32, reflect.Float64:
		return map[string]any{"type": "number"}
	case reflect.String:
		return map[string]any{"type": "string"}
	case reflect.Slice, reflect.Array:
		return map[string]any{"type": "array", "items": schemaFor(t.Elem())}
	case reflect.Map:
		return map[string]any{"type": "object"}
	case reflect.Struct:
		properties := map[string]any{}
		required := []string{}
		fields(t, properties, &required)
		sort.Strings(required)
		return map[string]any{"type": "object", "properties": properties, "required": required}
	}
	return map[string]any{}
}

func fields(t reflect.Type, properties map[string]any, required *[]string) {
	for i := range t.NumField() {
		field := t.Field(i)
		tag := field.Tag.Get("json")
		if tag == "-" || (!field.IsExported() && !field.Anonymous) {
			continue
		}
		name, options, _ := strings.Cut(tag, ",")
		if field.Anonymous && name == "" && field.Type.Kind() == reflect.Struct {
			fields(field.Type, properties, required)
			continue
		}
		if name == "" {
			name = field.Name
		}
		kind := field.Type.Kind()
		if kind == reflect.Pointer || kind == reflect.Interface || strings.Contains(options, "omitempty") {
			properties[name] = map[string]any{}
			continue
		}
		properties[name] = schemaFor(field.Type)
		*required = append(*required, name)
	}
}

// Cue is a caption: text from StartT to EndT, in seconds. What a query's
// cue[] is.
type Cue struct {
	StartT float64 `json:"start_t"`
	EndT   float64 `json:"end_t"`
	Text   string  `json:"text"`
}

// Covers says whether the cue is showing at t seconds: from its start up to,
// not including, its end.
func (c Cue) Covers(t float64) bool {
	return c.StartT <= t && t < c.EndT
}

// Cues holds cues from the tick they arrive on until they end.
type Cues struct {
	held []Cue
}

// Add keeps cue, in start order.
func (c *Cues) Add(cue Cue) {
	at := sort.Search(len(c.held), func(i int) bool { return c.held[i].StartT > cue.StartT })
	c.held = append(c.held[:at], append([]Cue{cue}, c.held[at:]...)...)
}

// At is the cues showing at t seconds, oldest first.
func (c *Cues) At(t float64) []Cue {
	var showing []Cue
	for _, cue := range c.held {
		if cue.Covers(t) {
			showing = append(showing, cue)
		}
	}
	return showing
}

// DropEnded forgets every cue that ended by t seconds.
func (c *Cues) DropEnded(t float64) {
	kept := c.held[:0]
	for _, cue := range c.held {
		if cue.EndT > t {
			kept = append(kept, cue)
		}
	}
	c.held = kept
}

func (c *Cues) Len() int { return len(c.held) }

// Span is one span a key is seen across, named by when it started and by
// its number. A row type that embeds it writes both, as start_t and id,
// which is how a reader of the rows tells apart two spans that start on one
// tick.
type Span struct {
	// The seconds of the tick it was first seen on.
	StartT float64 `json:"start_t"`
	// How many spans began before this one.
	ID uint64 `json:"id"`
	// How many ticks old it is: 0 on the tick it starts.
	Age uint64 `json:"-"`
}

type openSpan[K comparable] struct {
	key           K
	span          Span
	started, seen uint64
}

// Spans says which span a sighting belongs to, for a node writing a row per
// tick while something lasts. Call Tick once a tick, then See for each
// thing seen on it.
//
// A span ends when its key goes unseen for more than Gap ticks (0 by
// default), or once it is Longest ticks old; the next sighting starts a new
// one.
type Spans[K comparable] struct {
	open    []openSpan[K]
	gap     uint64
	longest uint64
	tick    uint64
	t       float64
	ticked  bool
	started uint64
}

// NewSpans is spans ended by one tick unseen, however long they run.
func NewSpans[K comparable]() *Spans[K] {
	return &Spans[K]{}
}

// Gap is how many ticks in a row a span's key may go unseen and the span
// still go on.
func (s *Spans[K]) Gap(ticks uint64) *Spans[K] {
	s.gap = ticks
	return s
}

// Longest is the most ticks one span lasts.
func (s *Spans[K]) Longest(ticks uint64) *Spans[K] {
	s.longest = max(ticks, 1)
	return s
}

// Tick starts the tick at t seconds, ending the spans that ran out.
func (s *Spans[K]) Tick(t float64) {
	if s.ticked {
		s.tick++
	}
	s.ticked, s.t = true, t
	kept := s.open[:0]
	for _, open := range s.open {
		unseen := s.tick - open.seen - 1
		age := s.tick - open.started
		if unseen <= s.gap && (s.longest == 0 || age < s.longest) {
			kept = append(kept, open)
		}
	}
	s.open = kept
}

// See is a sighting of key on this tick: the span it belongs to, started
// here when none is open for it.
func (s *Spans[K]) See(key K) Span {
	s.ticked = true
	for i := range s.open {
		if s.open[i].key == key {
			s.open[i].seen = s.tick
			span := s.open[i].span
			span.Age = s.tick - s.open[i].started
			return span
		}
	}
	span := Span{StartT: s.t, ID: s.started}
	s.started++
	s.open = append(s.open, openSpan[K]{key, span, s.tick, s.tick})
	return span
}

// Open is how many spans are open.
func (s *Spans[K]) Open() int { return len(s.open) }
