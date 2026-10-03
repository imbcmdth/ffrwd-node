package main

import (
	"reflect"
	"sort"
	"strings"
	"testing"

	node "github.com/imbcmdth/ffrwd-node/go"
	"github.com/imbcmdth/ffrwd-node/go/mock"
)

func open(t *testing.T) *mock.Harness[Params] {
	t.Helper()
	v := node.VideoStream("v", 0, 4, 4, "rgba", node.R(1, 15)).
		WithRate(node.R(15, 1))
	h, err := mock.Open(Definition, `{"every":3}`, v)
	if err != nil {
		t.Fatal(err)
	}
	return h
}

// lit is a picture lit at one pixel, which moves along the top row.
func lit(n int) []byte {
	pixels := make([]byte, 4*4*4)
	for at := (n % 4) * 4; at < (n%4)*4+4; at++ {
		pixels[at] = 255
	}
	return pixels
}

// rows is every row of ticks ticks handed to workers instances in turn, in
// pts order.
func rows(t *testing.T, ticks, workers int) []node.TimedText {
	t.Helper()
	instances := make([]*mock.Harness[Params], workers)
	for n := range instances {
		instances[n] = open(t)
	}
	var rows []node.TimedText
	for n := range ticks {
		worker := instances[n%workers]
		tick := worker.Tick(int64(n)).
			WithOrdinal(uint64(n)).
			WithFrame(0, int64(n), lit(n))
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
	return rows
}

func TestAnyNumberOfWorkersWriteTheSameRows(t *testing.T) {
	alone := rows(t, 10, 1)
	if len(alone) != 10 {
		t.Fatalf("%d rows", len(alone))
	}
	if got := rows(t, 10, 2); !reflect.DeepEqual(got, alone) {
		t.Fatalf("%v", got)
	}
	if got := rows(t, 10, 3); !reflect.DeepEqual(got, alone) {
		t.Fatalf("%v", got)
	}
}

func TestASightingStartsEverySoManyFrames(t *testing.T) {
	var ids []string
	for _, row := range rows(t, 7, 1) {
		ids = append(ids, row.Text)
	}
	if !strings.HasPrefix(ids[2], `{"start_t":0,"id":0,`) {
		t.Fatal(ids[2])
	}
	if !strings.HasPrefix(ids[3], `{"start_t":0.2,"id":1,`) {
		t.Fatal(ids[3])
	}
}
