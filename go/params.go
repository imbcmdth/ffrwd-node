package node

import (
	"bytes"
	"encoding/json"
	"fmt"
	"math"
	"reflect"
	"sort"
	"strconv"
	"strings"
	"unicode/utf8"
)

// NoParams is the schema of a node that takes no params.
const NoParams = `{"type":"object","properties":{},"additionalProperties":false}`

// ReadParams reads params against schema: an empty string is {}, a param
// set to null is a param not set, every param is checked against the
// schema, the schema's defaults fill in what is not set, and a whole number
// a param declared integer is given as 30.0 reads as 30. It answers the
// object as JSON, which the node's params type is then decoded from.
//
// The keywords checked are type, enum, const, minimum, maximum,
// exclusiveMinimum, exclusiveMaximum, minLength, maxLength, minItems,
// maxItems, items, properties, required and additionalProperties; the
// compiler checks the call against the whole schema before a node sees it.
func ReadParams(schema, params string) ([]byte, error) {
	parsedSchema, err := decode(schema)
	if err != nil {
		return nil, fmt.Errorf("the params schema is not JSON: %v", err)
	}
	text := strings.TrimSpace(params)
	var value any = map[string]any{}
	if text != "" {
		if value, err = decode(text); err != nil {
			return nil, fmt.Errorf("the params are not JSON: %v", err)
		}
	}
	object, ok := value.(map[string]any)
	if !ok {
		return nil, fmt.Errorf("the params are a JSON object, not %s", text)
	}
	for key, field := range object {
		if field == nil {
			delete(object, key)
		}
	}
	if value, err = check(object, parsedSchema, ""); err != nil {
		return nil, err
	}
	object = value.(map[string]any)
	if properties, ok := field(parsedSchema, "properties").(map[string]any); ok {
		for name, property := range properties {
			described, _ := property.(map[string]any)
			if defaulted, ok := described["default"]; ok {
				if _, set := object[name]; !set {
					object[name] = defaulted
				}
			}
		}
	}
	return json.Marshal(object)
}

func decode(text string) (any, error) {
	decoder := json.NewDecoder(strings.NewReader(text))
	decoder.UseNumber()
	var value any
	if err := decoder.Decode(&value); err != nil {
		return nil, err
	}
	if decoder.More() {
		return nil, fmt.Errorf("trailing characters after the value")
	}
	return value, nil
}

func field(schema any, key string) any {
	if object, ok := schema.(map[string]any); ok {
		return object[key]
	}
	return nil
}

func named(path string) string {
	if path == "" {
		return "the params"
	}
	return "`" + path + "`"
}

func joined(path, name string) string {
	if path == "" {
		return name
	}
	return path + "." + name
}

func shown(value any) string {
	text, _ := json.Marshal(value)
	return string(text)
}

func counted(n uint64, what string) string {
	if n == 1 {
		return "1 " + what
	}
	return fmt.Sprintf("%d %ss", n, what)
}

func number(value any) (float64, bool) {
	n, ok := value.(json.Number)
	if !ok {
		return 0, false
	}
	f, err := n.Float64()
	return f, err == nil
}

func whole(value any) bool {
	f, ok := number(value)
	return ok && f == math.Trunc(f) && !math.IsInf(f, 0)
}

func typeMatches(value any, kind string) bool {
	switch kind {
	case "null":
		return value == nil
	case "boolean":
		_, ok := value.(bool)
		return ok
	case "string":
		_, ok := value.(string)
		return ok
	case "array":
		_, ok := value.([]any)
		return ok
	case "object":
		_, ok := value.(map[string]any)
		return ok
	case "number":
		_, ok := number(value)
		return ok
	case "integer":
		return whole(value)
	}
	return true
}

func equal(a, b any) bool {
	if fa, ok := number(a); ok {
		fb, ok := number(b)
		return ok && fa == fb
	}
	return reflect.DeepEqual(a, b)
}

func bound(schema map[string]any, key string) (float64, bool) {
	return number(schema[key])
}

func count(schema map[string]any, key string) (uint64, bool) {
	n, ok := schema[key].(json.Number)
	if !ok {
		return 0, false
	}
	v, err := strconv.ParseUint(n.String(), 10, 64)
	return v, err == nil
}

func shownBound(f float64) string {
	return strconv.FormatFloat(f, 'f', -1, 64)
}

func check(value any, schemaValue any, path string) (any, error) {
	schema, ok := schemaValue.(map[string]any)
	if !ok {
		return value, nil
	}
	if kind, ok := schema["type"]; ok {
		var kinds []string
		switch kind := kind.(type) {
		case string:
			kinds = []string{kind}
		case []any:
			for _, k := range kind {
				if s, ok := k.(string); ok {
					kinds = append(kinds, s)
				}
			}
		}
		matched := len(kinds) == 0
		for _, k := range kinds {
			matched = matched || typeMatches(value, k)
		}
		if !matched {
			return nil, fmt.Errorf("%s is %s, not %s", named(path), strings.Join(kinds, " or "), shown(value))
		}
		integer, numbered := false, false
		for _, k := range kinds {
			integer = integer || k == "integer"
			numbered = numbered || k == "number"
		}
		if integer && !numbered && whole(value) {
			f, _ := number(value)
			value = json.Number(strconv.FormatInt(int64(f), 10))
		}
	}
	if allowed, ok := schema["enum"].([]any); ok {
		found := false
		listed := make([]string, len(allowed))
		for n, option := range allowed {
			found = found || equal(option, value)
			listed[n] = shown(option)
		}
		if !found {
			return nil, fmt.Errorf("%s is one of %s, not %s", named(path), strings.Join(listed, ", "), shown(value))
		}
	}
	if constant, ok := schema["const"]; ok && !equal(constant, value) {
		return nil, fmt.Errorf("%s is %s, not %s", named(path), shown(constant), shown(value))
	}
	if f, ok := number(value); ok {
		if min, ok := bound(schema, "minimum"); ok && f < min {
			return nil, fmt.Errorf("%s is at least %s, not %s", named(path), shownBound(min), shown(value))
		}
		if max, ok := bound(schema, "maximum"); ok && f > max {
			return nil, fmt.Errorf("%s is at most %s, not %s", named(path), shownBound(max), shown(value))
		}
		if min, ok := bound(schema, "exclusiveMinimum"); ok && f <= min {
			return nil, fmt.Errorf("%s is more than %s, not %s", named(path), shownBound(min), shown(value))
		}
		if max, ok := bound(schema, "exclusiveMaximum"); ok && f >= max {
			return nil, fmt.Errorf("%s is less than %s, not %s", named(path), shownBound(max), shown(value))
		}
	}
	if text, ok := value.(string); ok {
		chars := uint64(utf8.RuneCountInString(text))
		if min, ok := count(schema, "minLength"); ok && chars < min {
			return nil, fmt.Errorf("%s is at least %s", named(path), counted(min, "character"))
		}
		if max, ok := count(schema, "maxLength"); ok && chars > max {
			return nil, fmt.Errorf("%s is at most %s", named(path), counted(max, "character"))
		}
	}
	if items, ok := value.([]any); ok {
		n := uint64(len(items))
		if min, ok := count(schema, "minItems"); ok && n < min {
			return nil, fmt.Errorf("%s holds at least %s", named(path), counted(min, "item"))
		}
		if max, ok := count(schema, "maxItems"); ok && n > max {
			return nil, fmt.Errorf("%s holds at most %s", named(path), counted(max, "item"))
		}
		if itemSchema, ok := schema["items"]; ok {
			prefix := path
			if prefix == "" {
				prefix = "params"
			}
			for i, item := range items {
				checked, err := check(item, itemSchema, fmt.Sprintf("%s[%d]", prefix, i))
				if err != nil {
					return nil, err
				}
				items[i] = checked
			}
		}
	}
	if object, ok := value.(map[string]any); ok {
		properties, _ := schema["properties"].(map[string]any)
		if required, ok := schema["required"].([]any); ok {
			for _, name := range required {
				if name, ok := name.(string); ok {
					if _, set := object[name]; !set {
						return nil, fmt.Errorf("%s is required", named(joined(path, name)))
					}
				}
			}
		}
		closed := schema["additionalProperties"] == false
		names := make([]string, 0, len(object))
		for name := range object {
			names = append(names, name)
		}
		sort.Strings(names)
		for _, name := range names {
			if property, ok := properties[name]; ok {
				checked, err := check(object[name], property, joined(path, name))
				if err != nil {
					return nil, err
				}
				object[name] = checked
			} else if closed {
				known := make([]string, 0, len(properties))
				for key := range properties {
					known = append(known, "`"+key+"`")
				}
				sort.Strings(known)
				takes := "takes none"
				if len(known) > 0 {
					takes = "takes " + strings.Join(known, ", ")
				}
				whose := "the node " + takes
				if path != "" {
					whose = named(path) + " " + takes
				}
				return nil, fmt.Errorf("%s is not a param here; %s", named(joined(path, name)), whose)
			}
		}
	}
	return value, nil
}

// sameParams says whether two objects ReadParams answered hold the same
// params.
func sameParams(a, b []byte) bool {
	if bytes.Equal(a, b) {
		return true
	}
	va, errA := decode(string(a))
	vb, errB := decode(string(b))
	return errA == nil && errB == nil && sameValue(va, vb)
}

func sameValue(a, b any) bool {
	switch a := a.(type) {
	case map[string]any:
		b, ok := b.(map[string]any)
		if !ok || len(a) != len(b) {
			return false
		}
		for key, field := range a {
			other, ok := b[key]
			if !ok || !sameValue(field, other) {
				return false
			}
		}
		return true
	case []any:
		b, ok := b.([]any)
		if !ok || len(a) != len(b) {
			return false
		}
		for i := range a {
			if !sameValue(a[i], b[i]) {
				return false
			}
		}
		return true
	}
	return equal(a, b)
}
