package main

import (
	"reflect"
	"testing"

	node "github.com/imbcmdth/ffrwd-node/go"
	"github.com/imbcmdth/ffrwd-node/go/mock"
)

func open(t *testing.T, params string) *mock.Harness[Params] {
	t.Helper()
	h, err := mock.Open(Definition, params, node.VideoStream("v", 0, 2, 1, "rgba", node.R(1, 30)))
	if err != nil {
		t.Fatal(err)
	}
	return h
}

func only(t *testing.T, h *mock.Harness[Params], tick *mock.Tick) node.Payload {
	t.Helper()
	emitted, err := h.Process(tick)
	if err != nil {
		t.Fatal(err)
	}
	payloads := emitted.On("v")
	if len(payloads) != 1 {
		t.Fatalf("%d payloads", len(payloads))
	}
	return payloads[0]
}

func TestAQuarterLeftOfEveryColour(t *testing.T) {
	h := open(t, `{"amount":0.75}`)
	payload := only(t, h, h.Tick(0).WithFrame(0, 0, []byte{200, 100, 0, 255, 8, 4, 2, 9}))
	if payload.Kind != node.PayloadFrame || !reflect.DeepEqual(payload.Data, []byte{50, 25, 0, 255, 2, 1, 0, 9}) {
		t.Fatalf("%+v", payload)
	}
}

func TestNothingToDimPassesThePicture(t *testing.T) {
	h := open(t, `{"amount":0}`)
	payload := only(t, h, h.Tick(3).WithFrame(0, 3, make([]byte, 8)))
	if payload.Kind != node.PayloadSame || payload.Pts != 3 || payload.Index != 0 {
		t.Fatalf("%+v", payload)
	}
}

func TestTheAmountChangesWhileItRuns(t *testing.T) {
	h := open(t, "")
	if err := h.SetParams(`{"amount":1}`); err != nil {
		t.Fatal(err)
	}
	white := []byte{255, 255, 255, 255, 255, 255, 255, 255}
	payload := only(t, h, h.Tick(0).WithFrame(0, 0, white))
	if !reflect.DeepEqual(payload.Data, []byte{0, 0, 0, 255, 0, 0, 0, 255}) {
		t.Fatalf("%v", payload.Data)
	}
	if err := h.SetParams(`{"amount":2}`); err == nil {
		t.Fatal("an amount of 2 was taken")
	}
}
