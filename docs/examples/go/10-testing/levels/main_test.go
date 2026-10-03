package main

import (
	"reflect"
	"strings"
	"testing"

	node "github.com/imbcmdth/ffrwd-node/go"
	"github.com/imbcmdth/ffrwd-node/go/mock"
)

func open(params string) (*mock.Harness[Params], error) {
	v := node.VideoStream("v", 0, 2, 1, "rgba", node.R(1, 25))
	return mock.Open(Definition, params, v)
}

func TestBlackAndWhiteReachTheEnds(t *testing.T) {
	levels, err := open("")
	if err != nil {
		t.Fatal(err)
	}
	tick := levels.Tick(0).
		WithFrame(0, 0, []byte{16, 16, 16, 255, 235, 126, 235, 255})
	emitted, err := levels.Process(tick)
	if err != nil {
		t.Fatal(err)
	}
	payloads := emitted.On("v")
	if len(payloads) != 1 || payloads[0].Kind != node.PayloadFrame {
		t.Fatalf("no frame: %+v", emitted)
	}
	if want := []byte{0, 0, 0, 255, 255, 128, 255, 255}; !reflect.DeepEqual(payloads[0].Data, want) {
		t.Fatalf("%v", payloads[0].Data)
	}
}

func TestTheFullRangePassesTheFrameOn(t *testing.T) {
	levels, err := open(`{"black":0,"white":255}`)
	if err != nil {
		t.Fatal(err)
	}
	emitted, err := levels.Process(levels.Tick(0).WithFrame(0, 0, make([]byte, 8)))
	if err != nil {
		t.Fatal(err)
	}
	if payloads := emitted.On("v"); len(payloads) != 1 || payloads[0].Kind != node.PayloadSame {
		t.Fatalf("%+v", emitted)
	}
}

func TestParamsChangeBetweenTicks(t *testing.T) {
	levels, err := open("")
	if err != nil {
		t.Fatal(err)
	}
	if err := levels.SetParams(`{"black":0,"white":255}`); err != nil {
		t.Fatal(err)
	}
	emitted, err := levels.Process(levels.Tick(0).WithFrame(0, 0, make([]byte, 8)))
	if err != nil {
		t.Fatal(err)
	}
	if payloads := emitted.On("v"); len(payloads) != 1 || payloads[0].Kind != node.PayloadSame {
		t.Fatalf("%+v", emitted)
	}
}

func TestBlackOverWhiteIsRefused(t *testing.T) {
	_, err := open(`{"black":200,"white":100}`)
	if err == nil || !strings.Contains(err.Error(), "`black` under `white`") {
		t.Fatalf("%v", err)
	}
	if _, err := open(`{"black":-1}`); err == nil {
		t.Fatal("a black of -1 was taken")
	}
}
