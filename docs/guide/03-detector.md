# 3. A detector

A detector is a node that reads a picture and writes rows that describe what
it found. A row is one JSON object. A detector does not hand the picture
back. A node that draws the boxes, blurs them or writes them to a file reads
the picture from its original stream, and reads the rows from the detector.
This chapter builds `glow`, which finds the bright part of each frame and
writes a row that gives the box around that part.

## Rows out

An output of rows is a stream of messages. Each message is one JSON object,
and each is stamped with a pts. The rows about a frame are stamped with that
frame's pts, so a node that reads the same picture can pair each row with
the frame the row describes.

A detector writes one row per tick for each thing it sees. A tick is one
call to the node, and for `glow` there is one tick per frame. Each row says
only what is true at its tick. A thing that stays in view for a minute gets
a row on every tick of that minute, not a single row written a minute late.
That way, a node that reads the rows never waits for a row about a frame it
already has.

## start_t names the thing

Rows about one thing that is seen over many ticks carry two fields that name
the thing. `start_t` is the time at which the thing was first seen. `id` is
a number that tells apart two things first seen on the same tick. Any reader
can use `start_t` as a time. A span reducer, which merges the rows of one
thing into one row, groups the rows by `start_t`. A caption track starts its
cue at `start_t`.

The SDK, which is the library the module is built with, can keep track of
spans for the node. A span is the stretch of ticks over which one thing is
seen. On each tick, the node first tells the SDK the time of the tick, and
then tells the SDK each thing it saw. For each thing, the SDK returns the
span that the sighting belongs to. If no span is open, the SDK starts a new
span on this tick. A span ends when its thing goes unseen for more ticks
than the gap, which is the number of unseen ticks the node lets a span
survive. A span also ends when it has lasted as long as the longest span the
node allows. When the node writes a span into a row, the span adds its
`start_t` and its `id` to the row's fields.

**Rust**

```rust
use ffrwd_node::{Bound, Init, Input, Node, Out, Output, Result, Shape, Span, Spans, Tick};
use serde::{Deserialize, Serialize};

#[derive(Deserialize)]
struct Params {
    threshold: u8,
    gap: u64,
}

#[derive(Default, Serialize)]
struct Glow {
    #[serde(flatten)]
    span: Span,
    x: u32,
    y: u32,
    w: u32,
    h: u32,
}

struct GlowNode {
    v: u32,
    width: usize,
    threshold: u8,
    spans: Spans<()>,
}
```

**C++**

```cpp
#include <algorithm>
#include <array>
#include <optional>

#include "ffrwd/node.hpp"

struct Params {
    std::uint8_t threshold;
    std::uint64_t gap;
    FFRWD_FIELDS(threshold, gap)
};

struct Glow {
    ffrwd::Span span;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    FFRWD_FIELDS(span, x, y, w, h)
};
```

**JavaScript**

```js
import { defineNode, Input, Output, Shape, SPAN, Spans } from '@ffrwd/node';

const GLOW = { ...SPAN, x: 'integer', y: 'integer', w: 'integer', h: 'integer' };
```

**Go**

```go
package main

import (
	"errors"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Threshold uint8  `json:"threshold"`
	Gap       uint64 `json:"gap"`
}

type Glow struct {
	node.Span
	X uint32 `json:"x"`
	Y uint32 `json:"y"`
	W uint32 `json:"w"`
	H uint32 `json:"h"`
}

type GlowNode struct {
	v         uint32
	width     int
	threshold uint8
	spans     *node.Spans[struct{}]
}
```

## The node

**Rust**

```rust
impl Node for GlowNode {
    const NAME: &'static str = "glow";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"threshold":{"type":"integer","minimum":0,"maximum":255,"default":230},"gap":{"type":"integer","minimum":0,"default":2}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(_: &Params, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .output(Output::rows("glows").schema::<Glow>()))
    }

    fn init(params: Params, init: &Init) -> Result<GlowNode> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is a video input")?;
        Ok(GlowNode {
            v: v.id,
            width: video.width as usize,
            threshold: params.threshold,
            spans: Spans::new().gap(params.gap),
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        self.spans.tick(tick.time_base().seconds(frame.pts));
        let pixels = tick.fetch(self.v, frame.index);
        let Some([x, y, w, h]) = bright(&pixels, self.width, self.threshold) else {
            return Ok(());
        };
        let glow = Glow {
            span: self.spans.see(()),
            x,
            y,
            w,
            h,
        };
        Ok(out.row("glows", frame.pts, &glow)?)
    }
}

ffrwd_node::export!(GlowNode);
```

**C++**

```cpp
struct GlowNode : ffrwd::Node<GlowNode, Params> {
    static constexpr std::string_view name = "glow";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"threshold":{"type":"integer","minimum":0,"maximum":255,"default":230},"gap":{"type":"integer","minimum":0,"default":2}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::size_t width = 0;
    std::uint8_t threshold = 0;
    ffrwd::Spans<> spans;

    static ffrwd::Result<ffrwd::Shape> shape(const Params&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .output(ffrwd::Output::rows("glows").schema<Glow>());
    }

    static ffrwd::Result<GlowNode> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        GlowNode node;
        node.v = v.id;
        node.width = video->width;
        node.threshold = params.threshold;
        node.spans = ffrwd::Spans<>().gap(params.gap);
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        spans.tick(tick.time_base().seconds(frame->pts));
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        auto box = bright(pixels, width, threshold);
        if (!box) return {};
        auto [x, y, w, h] = *box;
        return out.row("glows", frame->pts, Glow{spans.see(), x, y, w, h});
    }
};

FFRWD_EXPORT(GlowNode);
```

**JavaScript**

```js
export const node = defineNode({
  name: 'glow',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"threshold":{"type":"integer","minimum":0,"maximum":255,"default":230},' +
    '"gap":{"type":"integer","minimum":0,"default":2}},"additionalProperties":false}',

  shape() {
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .output(Output.rows('glows').schema(GLOW));
  },

  init({ threshold, gap }, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    const width = video.width;
    const spans = new Spans().gap(gap);
    return {
      process(tick, out) {
        const frame = tick.frame(v.id);
        if (frame === undefined) return;
        spans.tick(tick.timeBase().seconds(frame.pts));
        const pixels = tick.fetch(v.id, frame.index);
        const box = bright(pixels, width, threshold);
        if (box === undefined) return;
        const [x, y, w, h] = box;
        out.row('glows', frame.pts, { ...spans.see(), x, y, w, h });
      },
    };
  },
});
```

**Go**

```go
var Definition = node.Definition[Params]{
	Name:         "glow",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"threshold":{"type":"integer","minimum":0,"maximum":255,"default":230},"gap":{"type":"integer","minimum":0,"default":2}},"additionalProperties":false}`,
	Shape: func(Params, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Output(node.RowsOutput("glows").Schema(node.SchemaOf[Glow]())), nil
	},
	Init: func(params Params, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		video := v.VideoFormat()
		if video == nil {
			return nil, errors.New("`v` is a video input")
		}
		return &GlowNode{
			v:         v.ID,
			width:     int(video.Width),
			threshold: params.Threshold,
			spans:     node.NewSpans[struct{}]().Gap(params.Gap),
		}, nil
	},
}

func (g *GlowNode) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(g.v)
	if !ok {
		return nil
	}
	g.spans.Tick(tick.TimeBase().Seconds(frame.Pts))
	pixels := tick.Fetch(g.v, frame.Index)
	box, ok := bright(pixels, g.width, g.threshold)
	if !ok {
		return nil
	}
	glow := Glow{
		Span: g.spans.See(struct{}{}),
		X:    box[0],
		Y:    box[1],
		W:    box[2],
		H:    box[3],
	}
	return out.Row("glows", frame.Pts, glow)
}

func init() { node.Export(Definition) }

func main() {}
```

The lines between this excerpt and the previous one hold the box finder,
which is plain pixel code. The full example file contains the box finder.

The SDK writes the JSON schema of the output from the fields of the row. A
float field becomes a `number`, a whole-number field becomes an `integer`,
and every field is required. The schema also allows fields that it does not
name. So a node that reads only some of the fields still matches the output.

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/glow.wasm --bound v
```

**C++**

```
$ ffrwd-wasm --shape build/glow.wasm --bound v
```

**JavaScript**

```
$ ffrwd-wasm --shape build/glow.wasm --bound v
```

**Go**

```
$ ffrwd-wasm --shape build/glow.wasm --bound v
```

```json
{
  "format": {"codec": "json", "kind": "data"},
  "kind": "data",
  "latency": 0.0,
  "name": "glows",
  "row": null,
  "schema": "{\"properties\":{\"h\":{\"type\":\"integer\"},\"id\":{\"type\":\"integer\"},\"start_t\":{\"type\":\"number\"},\"w\":{\"type\":\"integer\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"}},\"required\":[\"h\",\"id\",\"start_t\",\"w\",\"x\",\"y\"],\"type\":\"object\"}",
  "time_base": null
}
```

`glow` keeps its open spans from one tick to the next, so the result of each
tick depends on the ticks before it. A node like that is not pure. A pure
node is one in which every tick depends only on what the host, the program
that runs the node, hands it for that tick. The shape of `glow`, which lists
its ports and its clock, therefore does not declare the node pure. The host
runs `glow` as a single instance, which is one running copy of the node, one
tick at a time, in order. [Chapter 9](09-pure.md) makes `glow` pure.

## Calling it

When a function returns nothing but rows, its declaration gives the return
type as an array of records. The query can read only the fields that the
declaration names, for example in a `WHERE` over the rows. A node that reads
the rows is matched against the output's own schema instead of this
declaration ([chapter 4](04-reader.md)).

```sql
CREATE FUNCTION glow(v video_stream, threshold number DEFAULT 230, gap number DEFAULT 2)
RETURNS STRUCT(start_t number, id number, x number, y number, w number, h number)[]
  AS 'glow.wasm', 'glow' LANGUAGE wasm;

COPY (
  SELECT glow(f.video[1])
  FROM input('testsrc.mp4') f
) TO 'glows.ndjson'
```

```
$ ffrwd compile -f glow.sql
ffmpeg -i testsrc.mp4 -map 0:v:0 -c:0 rawvideo -pix_fmt:0 rgba -f nut pipe:1 | \
  ffrwd-wasm -f nut -i pipe:0 -pad \
  '{"color": {"range": "pc", "primaries": "unknown", "trc": "unknown", "space": "gbr"}}' \
  -m glow=glow.wasm -filter_complex '[v=0:v]glow=threshold=230:gap=2[glows=out0]' \
  -bound 'glow=[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]' -map '[out0]' \
  -f ndjson glows.ndjson
```

In the plan, `[glows=out0]` gives a label to the rows that the port `glows`
writes. The label names a data edge, which is a connection in the plan that
carries rows instead of pictures or sound. The plan connects a data edge
like any other stream. When the rows are written to an `.ndjson` file, each
row gains two fields beside its own: the `pts` it was stamped with, and its
`time` in seconds:

```ndjson
{"start_t":0.0,"id":0,"x":108,"y":0,"w":52,"h":240,"pts":0,"time":0.0}
{"start_t":0.0,"id":0,"x":50,"y":0,"w":110,"h":240,"pts":4096,"time":0.06666666666666667}
{"start_t":0.0,"id":0,"x":50,"y":0,"w":110,"h":240,"pts":8192,"time":0.13333333333333333}
```

## Spans from rows

`ffrwd.merge_spans` is a function that turns rows written one tick at a time
into one row per span. Rows that share a `start_t` belong to one span. The
span's row keeps the fields of the last row in the span. The span ends at
the end of the last tick that carried a row for it. A tick with no row for
the span is a gap inside the span, not the end of the span. When a span is
still open after `max_span` seconds, `merge_spans` writes the span's row as
it stands and continues with a new span. So `max_span` is also the longest
that a span's row can wait before it is written.

```sql
CREATE FUNCTION glow(v video_stream, threshold number DEFAULT 230, gap number DEFAULT 2)
RETURNS STRUCT(start_t number, id number, x number, y number, w number, h number)[]
  AS 'glow.wasm', 'glow' LANGUAGE wasm;

COPY (
  SELECT ffrwd.merge_spans(glow(f.video[1]), max_span => 10)
  FROM input('testsrc.mp4') f
) TO 'spans.ndjson'
```

```
$ ffrwd compile -f spans.sql
ffmpeg -i testsrc.mp4 -map 0:v:0 -c:0 rawvideo -pix_fmt:0 rgba -f nut pipe:1 | \
  ffrwd-wasm -f nut -i pipe:0 -pad \
  '{"color": {"range": "pc", "primaries": "unknown", "trc": "unknown", "space": "gbr"}}' \
  -m glow=glow.wasm -filter_complex \
  '[v=0:v]glow=threshold=230:gap=2[glows=n1];[n1]rowmerge=max_span=10[out0]' -bound \
  'glow=[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]' -map '[out0]' -f \
  ndjson spans.ndjson
```

In the plan, `merge_spans` is `rowmerge`, a node built into the host.
`rowmerge` runs in the same host as the node that writes the rows. The bars
of `testsrc.mp4` stay bright for all four seconds, so every row that `glow`
wrote joins one span:

```ndjson
{"end_t":4.0,"h":240,"id":0,"start_t":0.0,"w":170,"x":108,"y":0}
```

## A row for the run

The rows on an output port form a stream that other nodes read. A node may
also write run rows, which go to the run itself instead of to another node.
The run reports a node's run rows beside the rows it reports about itself. A
sink writes run rows, because a sink has no output port to write anything
else on ([chapter 7](07-sink.md)). Run rows have a schema of their own,
which `--describe` reports as `rows_schema`. `glow` writes no run rows, so
its `rows_schema` is null.
