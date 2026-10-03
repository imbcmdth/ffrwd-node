package node

import (
	"encoding/json"
	"reflect"
	"slices"
	"strings"
	"testing"
)

type spot struct {
	StartT float64   `json:"start_t"`
	ID     uint64    `json:"id"`
	X      uint32    `json:"x"`
	Label  string    `json:"label"`
	Hidden *bool     `json:"hidden"`
	Vector []float32 `json:"vector"`
}

func TestASchemaFromARowType(t *testing.T) {
	var schema struct {
		Properties map[string]map[string]any `json:"properties"`
		Required   []string                  `json:"required"`
	}
	if err := json.Unmarshal([]byte(SchemaOf[spot]()), &schema); err != nil {
		t.Fatal(err)
	}
	for field, kind := range map[string]string{
		"start_t": "number", "id": "integer", "x": "integer", "label": "string", "vector": "array",
	} {
		expect(t, schema.Properties[field]["type"], any(kind))
	}
	expect(t, len(schema.Properties["hidden"]), 0)
	if !slices.Contains(schema.Required, "start_t") || slices.Contains(schema.Required, "hidden") {
		t.Fatal(schema.Required)
	}
}

func TestRowsParseAndSayWhichDidNot(t *testing.T) {
	var row spot
	if err := Parse(`{"start_t":1.5,"id":2,"x":3,"label":"a","vector":[]}`, &row); err != nil {
		t.Fatal(err)
	}
	expect(t, row.X, 3)
	err := Parse(`{"start_t":"soon"}`, &row)
	if err == nil || !strings.Contains(err.Error(), "soon") {
		t.Fatal(err)
	}
}

func TestASpanRunsWhileItsKeyIsSeen(t *testing.T) {
	spans := NewSpans[string]()
	var starts []float64
	for n, seen := range []bool{true, true, false, true, true} {
		spans.Tick(float64(n))
		if seen {
			starts = append(starts, spans.See("mark").StartT)
		}
	}
	expect(t, reflect.DeepEqual(starts, []float64{0, 0, 3, 3}), true)
}

func TestAGapKeepsTheSpan(t *testing.T) {
	spans := NewSpans[struct{}]().Gap(2)
	var starts []float64
	for n, seen := range []bool{true, false, false, true, false, false, false, true} {
		spans.Tick(float64(n) * 0.5)
		if seen {
			starts = append(starts, spans.See(struct{}{}).StartT)
		}
	}
	expect(t, reflect.DeepEqual(starts, []float64{0, 0, 3.5}), true)
}

func TestTheLongestSpanSplits(t *testing.T) {
	spans := NewSpans[struct{}]().Longest(3)
	var seen []Span
	for n := range 7 {
		spans.Tick(float64(n))
		seen = append(seen, spans.See(struct{}{}))
	}
	expect(t, reflect.DeepEqual(seen, []Span{
		{0, 0, 0}, {0, 0, 1}, {0, 0, 2}, {3, 1, 0}, {3, 1, 1}, {3, 1, 2}, {6, 2, 0},
	}), true)
}

func TestKeysHaveSpansOfTheirOwn(t *testing.T) {
	spans := NewSpans[string]()
	spans.Tick(0)
	expect(t, spans.See("a").StartT, 0)
	spans.Tick(1)
	expect(t, spans.See("a").StartT, 0)
	expect(t, spans.See("b").StartT, 1)
	expect(t, spans.Open(), 2)
	spans.Tick(2)
	expect(t, spans.Open(), 2)
	spans.See("b")
	spans.Tick(3)
	expect(t, spans.Open(), 1)
}

type sighting struct {
	Span
	Label string `json:"label"`
}

func TestSpansStartingOnOneTickWriteOneStartTAndTheirOwnIDs(t *testing.T) {
	spans := NewSpans[string]()
	spans.Tick(2.5)
	var rows []string
	for _, key := range []string{"a", "b"} {
		row, _ := json.Marshal(sighting{spans.See(key), key})
		rows = append(rows, string(row))
	}
	expect(t, strings.Join(rows, " "), `{"start_t":2.5,"id":0,"label":"a"} {"start_t":2.5,"id":1,"label":"b"}`)
	var schema struct {
		Properties map[string]map[string]any `json:"properties"`
	}
	_ = json.Unmarshal([]byte(SchemaOf[sighting]()), &schema)
	expect(t, schema.Properties["start_t"]["type"], any("number"))
	expect(t, schema.Properties["id"]["type"], any("integer"))
	var span Span
	if err := Parse(`{"start_t":2.5,"id":1,"label":"b"}`, &span); err != nil {
		t.Fatal(err)
	}
	expect(t, span, Span{StartT: 2.5, ID: 1})
}

func TestCuesShowWhileTheyCoverTheTime(t *testing.T) {
	var cues Cues
	cues.Add(Cue{2, 4, "second"})
	cues.Add(Cue{0, 3, "first"})
	at := func(t float64) string {
		var texts []string
		for _, cue := range cues.At(t) {
			texts = append(texts, cue.Text)
		}
		return strings.Join(texts, ",")
	}
	expect(t, at(2.5), "first,second")
	expect(t, at(3), "second")
	expect(t, at(4), "")
	cues.DropEnded(3)
	expect(t, cues.Len(), 1)
}
