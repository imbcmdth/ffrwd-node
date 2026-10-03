# github.com/imbcmdth/ffrwd-node/go

The node world, `ffrwd:av@0.19.1`, for Go. Describe a node in a
`node.Definition`, hand it to `node.Export` from the module's `init`, and
build it with componentize-go. The package does what the Rust crate in
`../rust` does for a Rust module, with the same names in Go's spelling: the
call sequence, params read against their schema, shapes from builders, time
in any time base, rows of a state input folded, emissions checked as they
are made, and an error as the run's message.

Requires ffrwd 0.29, whose `ffrwd/wasm` is 0.19.1.

## A node

```go
package main

import node "github.com/imbcmdth/ffrwd-node/go"

type Params struct {
	Amount float64 `json:"amount"`
}

type Dim struct {
	v      uint32
	amount float64
}

var Definition = node.Definition[Params]{
	Name:         "dim",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"amount":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}`,
	Shape: func(Params, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Output(node.LikeOutput("v")).
			Pure().
			OneToOne(), nil
	},
	Init: func(params Params, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		return &Dim{v: v.ID, amount: params.Amount}, nil
	},
}

func (d *Dim) SetParams(params Params) error {
	d.amount = params.Amount
	return nil
}

func (d *Dim) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(d.v)
	if !ok {
		return nil
	}
	if d.amount == 0 {
		return out.Pass("v", d.v, frame)
	}
	pixels := tick.Fetch(d.v, frame.Index)
	darken(pixels, d.amount)
	return out.Frame("v", frame.Pts, frame.Duration, pixels)
}

func init() { node.Export(Definition) }

func main() {}
```

`Name`, `Version`, `ParamsSchema`, `RowsSchema` and `RowsLanguage` are
`describe`. `Shape` answers at compile time, and again at init, which the
package calls with the shape resolved. `Init` answers the instance, which
implements `Process` and, when it takes them, `SetParams(P) error` and
`Fold(node.StateRow) error`. Without `SetParams` a change of params is
refused; without `Fold` a node declaring a state input fails on its first
row.

**Params.** The call's JSON is read against `ParamsSchema` before any of
these sees it: an empty string is `{}`, a param set to null is not set, the
schema's defaults fill in what the call left out, a whole number given as
`30.0` to an `integer` param reads as `30`, and then `encoding/json` reads
the object as `P`. A param outside the schema, or one that breaks its
`type`, `enum`, bounds or lengths, is refused with the param named.

**Shapes.** `node.VideoInput`, `AudioInput`, `RowsInput` and `PacketsInput`
build inputs, `VideoOutput`, `AudioOutput`, `RowsOutput`, `PacketsOutput`
and `LikeOutput` outputs, and `NewShape` the shape, each method answering a
copy: `node.RowsInput("boxes").Interval().Latency(2).State().Schema(node.SchemaOf[Box]())`,
`node.VideoInput("feed").Optional().Hold().Lead(0.5).PortParam("port")`,
`node.VideoOutput("mask").PixelFormat("gray")`. `Spec()` is a builder's port
so far. Before the host sees a shape the package settles its clock, leaves
out every output that follows an unbound input, and refuses what the host
would refuse, naming the port.

**What the call binds.** `bound.Has("a")`, `bound.Count("inputs")` and
`bound.RateOf("v")`; a rate is a `node.Rational`, so `rate.Count(2)` is the
samples in two seconds and `rate.Duration(1)` the length of one frame.

**Time.** `Rational` has `Seconds`, `Pts`, `Rescale` exactly as ffmpeg
rounds, `Count`, `Duration` and `Inverse`, and `node.Approximate(29.97, 1001)`
is a rate a param gives as a number.

**Emitting.** `out` checks every emission as it is made: the port is one the
shape declares, of the payload's kind, and its pts never go back.
`out.Pass`, `Frame`, `Same`, `Message`, `Row`, `Rows`, `Cue`, `Progress`,
`Packet`, `Report` and `Finish`, and `out.Pts(port, seconds)` for a time on a
port.

**Rows.** `node.ReadRows[T](tick, id)` reads a data stream's messages, or
the rows riding a frame stream, as `T`. `node.SchemaOf[T]()` writes a row
type's schema from its fields, `integer` and `number` kept apart; a pointer
or `omitempty` field is not required. `node.Spans[K]` names per-tick rows by
the span they belong to, and a `node.Span` embedded in a row writes its
`start_t` and its `id`. `Cue` and `Cues` are a query's cues.

**Errors.** Return one; the run ends with its message. A panic in any call
ends it the same way.

## Crops and resizes

The package `frame` gives a Go module what the Rust crate
[ffrwd-frame](https://github.com/imbcmdth/ffrwd-frame) gives a Rust one,
under its names in Go's spelling: `frame.NewRgba` over the bytes a fetch
hands over, a `frame.Rect` of them (`frame.Whole`, or `frame.Padded` for a
detector's box widened and clamped to the frame), and `frame.Planes`,
`frame.Tensor` and `frame.Tensors`, which crop, resize with Pillow's
bilinear and normalize into the floats or the fp32 bytes a vision model
reads. The pixels are the crate's to the byte, and the tests hold them to
digests of its answers. A normalization of mean 0 and standard deviation
1/255 hands back plain eight-bit values. Pillow's bicubic and the yuv420p
conversion are the crate's alone.

```go
import "github.com/imbcmdth/ffrwd-node/go/frame"
```

## Building

Mainline Go 1.25.5 or newer, and componentize-go 0.4.1. On Windows the
`go install` wrapper of componentize-go asks its release for a `.tar.gz`
that is published only as `componentize-go-windows-amd64.zip`; take the
zip from the release and put its `componentize-go.exe` on PATH.

```
cd examples/dim
componentize-go -d ../../wit -w ffrwd:av/node-module@0.19.1 build -o ../../build/dim.wasm
```

or `sh build.sh`. A module of its own names the copy of the world in its
module cache:

```
componentize-go -d "$(go list -m -f '{{.Dir}}' github.com/imbcmdth/ffrwd-node/go)/wit" \
    -w ffrwd:av/node-module@0.19.1 build -o dim.wasm
```

componentize-go can read a dependency's `componentize-go.toml` for this,
but 0.4.1 resolves the paths in it against the working directory rather
than the dependency's, so the module ships none.

The module carries `wit/av.wit`, since a Go module holds nothing outside
its own directory, and the bindings generated from it under
`internal/wit`. In the repo, `go test` checks that copy against the root's,
and `go generate` copies it over and regenerates the bindings.

## The collector

A Go component on the component model needs two things the bindings do not
do for it, and the package does both.

The collector is off (`debug.SetGCPercent(-1)`). The host lowers each call's
arguments through `cabi_realloc`, an allocation there can start a GC assist,
and the mark phase reads the clock, a wasi import; an import called from
inside `cabi_realloc` is a reentrancy the host traps ("cannot leave
component instance"). With the collector left on, `dim` traps on its third
640x480 frame. The heap is collected instead at the top of each `process`
call, where an import is allowed, once 256 MiB have been allocated since the
last collection.

`cabi_realloc` also pins everything it allocates in one pinner that only
`shape`, `init` and `set-params` release. Every frame `process` fetches
lands there, so a node that never changes its params would hold every frame
it ever read: about 3,500 frames of 640x480 before the instance runs out of
memory. `process` releases that pinner before anything else; what the
previous call fetched is held by Go references, or by nothing. With both,
one instance of `dim` runs 4,000 640x480 frames in about ten seconds.

This is the road the sidecar's `modules-go/big` took. Its TinyGo road
(wit-bindgen-go, `go.bytecodealliance.org/cm` pinned at v0.3.0,
`-gc=leaking -scheduler=none`) uses other bindings, and this module does not
build with TinyGo.

## Testing on the host

Everything but the bindings builds on the host, so a module's tests run
with `go test`:

```go
h, err := mock.Open(Definition, `{"amount":0.75}`, node.VideoStream("v", 0, 64, 48, "rgba", node.R(1, 15)))
emitted, err := h.Process(h.Tick(0).WithFrame(0, 0, pixels))
```

`mock.Open` opens a node the way the host does, and its ticks are built by
hand: `WithOrdinal`, `WithMessage`, `WithRow`, `WithFeed`, `WithEnded`,
`WithEarlier`, `Final`.

## Where it parts from the Rust crate

- A node is a `Definition` value and its instance whatever `Init` answers;
  `SetParams` and `Fold` are optional interfaces, not defaults that refuse.
- Builders answer copies, and a builder's port is read through `Spec()`;
  the resolved shape is a `NodeShape`.
- An option is a pointer; an enum is a typed constant.
- `ReadRows` and `SchemaOf` are functions, since Go methods take no type
  parameters.

## License

MIT.
