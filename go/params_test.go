package node

import (
	"encoding/json"
	"strings"
	"testing"
)

const schema = `{
	"type": "object",
	"properties": {
		"every": {"type": "integer", "minimum": 1, "default": 30},
		"amount": {"type": "number", "minimum": 0, "maximum": 1, "default": 0.5},
		"text": {"type": "string", "minLength": 1},
		"mode": {"enum": ["over", "under"], "default": "over"},
		"fps": {"type": ["number", "null"]}
	},
	"required": ["text"],
	"additionalProperties": false
}`

type testParams struct {
	Every  uint32   `json:"every"`
	Amount float64  `json:"amount"`
	Text   string   `json:"text"`
	Mode   string   `json:"mode"`
	Fps    *float64 `json:"fps"`
}

func read(t *testing.T, params string) testParams {
	t.Helper()
	object, err := ReadParams(schema, params)
	if err != nil {
		t.Fatal(err)
	}
	var parsed testParams
	if err := json.Unmarshal(object, &parsed); err != nil {
		t.Fatal(err)
	}
	return parsed
}

func refused(t *testing.T, schema, params, said string) {
	t.Helper()
	_, err := ReadParams(schema, params)
	if err == nil || !strings.Contains(err.Error(), said) {
		t.Fatalf("%s: %v, wanted %q", params, err, said)
	}
}

func TestDefaultsFillWhatTheCallLeftOut(t *testing.T) {
	expect(t, read(t, `{"text":"hi"}`), testParams{Every: 30, Amount: 0.5, Text: "hi", Mode: "over"})
}

func TestAWholeFloatReadsAsAnInteger(t *testing.T) {
	expect(t, read(t, `{"text":"hi","every":15.0}`).Every, 15)
	refused(t, schema, `{"text":"hi","every":1.5}`, "`every` is integer")
}

func TestNullIsNotSet(t *testing.T) {
	params := read(t, `{"text":"hi","fps":null,"every":null}`)
	if params.Fps != nil || params.Every != 30 {
		t.Fatalf("%+v", params)
	}
}

func TestRefusalsNameTheParam(t *testing.T) {
	for _, c := range [][2]string{
		{`{"text":"hi","every":0}`, "`every` is at least 1"},
		{`{"text":"hi","amount":2}`, "`amount` is at most 1"},
		{`{"text":""}`, "`text` is at least 1 character"},
		{`{"text":"hi","mode":"sideways"}`, "`mode` is one of"},
		{`{"text":"hi","colour":"red"}`, "`colour` is not a param here"},
		{`{}`, "`text` is required"},
		{`[1]`, "a JSON object"},
		{`{"text":7}`, "`text` is string"},
	} {
		refused(t, schema, c[0], c[1])
	}
}

func TestNoParamsReadsNothing(t *testing.T) {
	for _, params := range []string{"", " {} "} {
		object, err := ReadParams(NoParams, params)
		if err != nil || string(object) != "{}" {
			t.Fatalf("%q: %s %v", params, object, err)
		}
	}
	refused(t, NoParams, `{"x":1}`, "the node takes none")
}
