# 3. A detector

A detector reads a picture and says what it found, as rows. It does not hand
the picture back: a node that draws the boxes, blurs them or writes them to
a file reads the picture from where it already is, and the rows from the
detector. This chapter builds `glow`, which finds the bright part of each
frame and writes a box around it.

## Rows out

An output of rows is a stream of JSON objects, one message each, every one
stamped with a pts. The rows of a frame are stamped with that frame's pts,
so a reader of the same picture pairs them with the frame they describe.

A detector writes one row per tick for each thing it sees, and says only
what is true at that tick. A thing that stays in view for a minute is a row
on every tick of that minute, not one row written a minute late. A reader
never waits for a row about a frame it already has.

## start_t names the thing

Rows about one thing seen over many ticks carry the time it was first seen
as `start_t`, and a number, `id`, that keeps apart two things first seen on
the same tick. `start_t` is a time every reader can use: a span reducer
groups the rows by it, and a caption track starts its cue there.

The SDK can keep the spans. The node marks each tick, then says what it saw;
it gets back the span the sighting belongs to, started on this tick when
none is open. A span ends when its thing goes unseen for more ticks than the
gap allows, or when it has run as long as the longest the node sets. A span
written into a row adds its `start_t` and its `id` to the row's fields.

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

The box finder between them is plain pixel code; the example holds it whole.

The SDK writes the output's schema from the row's own fields: a float is a
`number`, a whole number an `integer`, and every field is required. Other
fields are allowed, so a reader that names only some of them still matches.

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/glow.wasm --bound v
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

`glow` keeps its spans from one tick to the next, which ties each tick to
the ticks before it. So its shape does not say it is pure, and the host runs
it as one instance, one tick at a time, in order. [Chapter 9](09-pure.md)
makes it pure.

## Calling it

A function returning rows alone declares them as an array of records. The
fields it names are what the query can read of them, a `WHERE` over the rows
for one. A node that reads the rows is matched against the output's own
schema instead ([chapter 4](04-reader.md)).

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

`[glows=out0]` labels the rows the port `glows` writes, and the label is a
data edge like any stream. Written to `.ndjson`, each row gets the `pts` it
was stamped with and its `time` in seconds beside its own fields:

```ndjson
{"start_t":0.0,"id":0,"x":108,"y":0,"w":52,"h":240,"pts":0,"time":0.0}
{"start_t":0.0,"id":0,"x":50,"y":0,"w":110,"h":240,"pts":4096,"time":0.06666666666666667}
{"start_t":0.0,"id":0,"x":50,"y":0,"w":110,"h":240,"pts":8192,"time":0.13333333333333333}
```

## Spans from rows

`ffrwd.merge_spans` turns rows written a tick at a time into one row per
span. Rows sharing a `start_t` are one span, which keeps the last row's
fields and ends at the end of the last tick that carried one. A tick with no
row for it is a gap inside the span, not its end. A span still open after
`max_span` seconds is written as it stands and carries on as a new one, so
`max_span` is also the latest a span row leaves.

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

The reducer is the host's own node, `rowmerge`, and runs beside the rows'
producer. The bars of `testsrc.mp4` stay bright the whole four seconds, so
every row `glow` wrote joins one span:

```ndjson
{"end_t":4.0,"h":240,"id":0,"start_t":0.0,"w":170,"x":108,"y":0}
```

## A row for the run

Rows on an output are a stream that other nodes read. A node may also write
rows for the run itself, which the run reports beside its own; a sink has
nothing else to say ([chapter 7](07-sink.md)). Those rows have a schema of
their own, which `--describe` reports as `rows_schema`. `glow` writes none,
so its `rows_schema` is null.
