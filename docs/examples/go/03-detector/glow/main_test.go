package main

import (
	"bytes"
	"reflect"
	"sort"
	"strings"
	"testing"

	node "github.com/imbcmdth/ffrwd-node/go"
	"github.com/imbcmdth/ffrwd-node/go/mock"
)

// starts is the start_t of every row of four lit ticks handed to workers
// instances in turn, in pts order.
func starts(t *testing.T, workers int) []string {
	t.Helper()
	open := func() *mock.Harness[Params] {
		v := node.VideoStream("v", 0, 2, 2, "rgba", node.R(1, 10))
		h, err := mock.Open(Definition, "", v)
		if err != nil {
			t.Fatal(err)
		}
		return h
	}
	instances := make([]*mock.Harness[Params], workers)
	for n := range instances {
		instances[n] = open()
	}
	var rows []node.TimedText
	for n := range 4 {
		worker := instances[n%workers]
		tick := worker.Tick(int64(n)).
			WithOrdinal(uint64(n)).
			WithFrame(0, int64(n), bytes.Repeat([]byte{255}, 16))
		emitted, err := worker.Process(tick)
		if err != nil {
			t.Fatal(err)
		}
		rows = append(rows, emitted.Messages("glows")...)
	}
	sort.Slice(rows, func(a, b int) bool {
		if rows[a].Pts != rows[b].Pts {
			return rows[a].Pts < rows[b].Pts
		}
		return rows[a].Text < rows[b].Text
	})
	var firsts []string
	for _, row := range rows {
		first, _, _ := strings.Cut(row.Text, ",")
		firsts = append(firsts, first)
	}
	return firsts
}

func TestSpansKeptAcrossTicksSplitWithTheWorkers(t *testing.T) {
	alone := []string{`{"start_t":0`, `{"start_t":0`, `{"start_t":0`, `{"start_t":0`}
	if got := starts(t, 1); !reflect.DeepEqual(got, alone) {
		t.Fatalf("%q", got)
	}
	split := []string{`{"start_t":0`, `{"start_t":0.1`, `{"start_t":0`, `{"start_t":0.1`}
	if got := starts(t, 2); !reflect.DeepEqual(got, split) {
		t.Fatalf("%q", got)
	}
}
