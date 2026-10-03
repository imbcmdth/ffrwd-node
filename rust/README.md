# ffrwd-node

A node is what an ffrwd module is: typed input
ports, typed output ports and a clock. The host calls it once a tick with
what each input holds for that tick, and it emits on its outputs. Its ports
follow from the call's params and from which inputs the call binds, and the
query's compiler reads them before anything runs.

This crate is that world for Rust. Implement `Node` for a type, hand the type
to `export!`, and build for `wasm32-wasip2`. The crate carries the bindings
and the rest of what each module would otherwise write for itself: the call
sequence, params read against their schema, shapes from builders, time in
any time base, rows of a state input folded, emissions checked as they are
made, and errors as the run's message.

Requires ffrwd 0.29, whose `ffrwd/wasm` is 0.19.1.

## The trait

```rust
pub trait Node: Sized + 'static {
    const NAME: &'static str;
    const VERSION: &'static str;
    const PARAMS_SCHEMA: &'static str = NO_PARAMS;
    const ROWS_SCHEMA: &'static str = "";
    const ROWS_LANGUAGE: &'static [&'static str] = &[];
    type Params: DeserializeOwned;

    fn shape(params: &Self::Params, bound: &Bound) -> Result<Shape>;
    fn init(params: Self::Params, init: &Init) -> Result<Self>;
    fn set_params(&mut self, params: Self::Params) -> Result<()>; // refuses unless written
    fn fold(&mut self, row: StateRow) -> Result<()>; // refuses unless written
    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()>;
}
```

The constants are `describe`. `shape` answers at compile time, and again at
`init`, which the crate calls with the shape resolved. `process` runs once a
tick; the last call is the one with `tick.last()` set, and there is no
separate flush.

**Params.** The call's JSON is read against `PARAMS_SCHEMA` before any of
these sees it: an empty string is `{}`, a param set to null is not set, the
schema's defaults fill in what the call left out, a whole number given as
`30.0` to an `integer` param reads as `30`, and then serde reads the object as
`Params`. A param outside the schema, or one that breaks its `type`, `enum`,
bounds or lengths, is refused with the param named. Params equal to the ones
in force never reach `set_params`.

**Shapes.** `Shape`, `Input` and `Output` build the ports the way a query
reads them:

```rust
Shape::new()
    .input(Input::video("v").clock().window(15, 1).pixel_formats(&["rgba"]))
    .input(Input::rows("boxes").interval().latency(2.0).state().schema::<Box>())
    .input(Input::video("feed").optional().hold().lead(0.5).port_param("port").like("v"))
    .output(Output::video("mask").pixel_format("gray"))
    .output(Output::rows("spots").schema::<Spot>())
    .pure()
```

An output with no format of its own takes the clock input's, one given only a
pixel format follows the clock input with that override, and `Output::like("v")`
is an output named `v` in input `v`'s format. Before the host sees a shape
the crate settles its clock, leaves out every output that follows an input
the call does not bind, and refuses what the host would refuse (a clock that
is optional, many or not lockstep, a held data input, a frame input paired by
interval, a stride past its window, a data input read for its timing, a data
input on a group no hold input is in), naming the port.

**What the call binds.** `Bound` names the inputs a call binds, how many
streams each takes, and each stream's rate where the compiler knows it: a
frame rate for video, a sample rate for audio. `bound.has("a")`,
`bound.count("inputs")` and `bound.rate_of("v")` read it, and a rate turns
seconds into frames or samples and back: `rate.count(2.0)` is the samples in
two seconds, `rate.duration(1)` the length of one frame. The crate shapes the
node again at `init` from the streams bound there, each carrying the hint the
compiler's `shape` was asked with, so the instance's shape is the plan's.

**Time.** `Rational` is a time base or a rate: `seconds(pts)`, `pts(seconds)`,
`rescale(pts, to)` exactly as ffmpeg rounds, and `approximate(29.97, 1001)`
for a rate a param gives as a number. `tick.seconds()` is the tick's time,
and `out.pts(port, seconds)` a time on a port, in the port's own time base or
the clock's.

**State.** Before each `process`, every row on an input declared `state()`
reaches `fold`: the rows of ticks this instance did not process first, oldest
first, then the tick's own. A pure node spread over workers gets the same
state on each.

**Emitting.** `Out` checks every emission as it is made: the port is one the
shape declares, of the payload's kind, and its pts never go back, within a
call or across calls, nor a packet's dts. `out.pass("v", v, &frame)` hands an
input frame back uncopied, `out.frame` sends new bytes, `out.row` and
`out.rows` send rows, `out.progress` a progress mark, `out.report` a row for
the run's rows output, and `out.finish()` ends a generator.

**Rows.** `tick.rows::<T>(id)` reads a data stream's messages, or the rows
riding a frame stream, as `T`. `schema::<T>()` writes a row type's JSON schema
from what `T::default()` serializes to, `integer` and `number` kept apart.
`Spans` names per-tick rows by the span they belong to, with a gap a span
survives and a longest it may run; a `Span` flattened into a row writes its
`start_t` and its `id`, which `ffrwd.merge_spans` keys a span by, so two
things first seen on one tick stay two spans; `Cue` is a query's `cue`, and
`Cues` holds cues from the tick they arrive on until they end.

**Errors.** `Result` carries an `Error` that anything displaying converts
into, so `?` takes a `String`, a `&str` or serde's errors alike. An `Err`
ends the run with its message.

## What 0.19.1 added

- `tick.ordinal()` is the tick's number in the run, counted over every
  instance. `spot` can number its sightings `tick.ordinal() / every` and say
  `pure()`, and a run split across workers agrees on the ids.
- `Input::video("v").timing()` reads a picture for its times and size alone.
  The compiler hands it in whatever format is cheapest and the host carries
  no pixels for it, which is what a mask sized and timed by a picture needs.
  `tick.fetch` on it, or passing its frame on, ends the run with the port
  named.
- `bound.rate_of` and `bound.count` let a shape declare in seconds what it
  knows in frames: `clips`, which closes a stretch on the first frame at or
  past ten seconds, can declare `10.0 + rate.duration(1)` in place of the
  10.5 that covers 2 fps, and `hear` a window of `rate.count(2.0)` at
  whatever rate it is bound.
- `Input::rows("d").anchor(Anchor::FirstFrame)` places a data stream on
  another time origin at the tick its first message arrives on, as
  playout's `follow` re-stamped by hand.
- `Input::rows("cues").group("ad")` reads rows a feeder writes beside its
  picture and sound, on the connection of the hold group `ad`.
- `feed.start.known` is the tick a start was fixed on, and
  `tick.ended_feeds(id)` every feed that ended since the instance's previous
  call, foretold or not, so `compose`'s presence rows can come out the same
  on any worker.

## An example: `spot`

Rows alone from a picture: one a frame while a grey mark is in view, every
row of one sighting carrying the time it began as `start_t`, and a new
sighting every `every` frames. The full module, with a stand-in for a real
detector:

```rust
use ffrwd_node::{Bound, Init, Input, Node, Out, Output, Result, Shape, Span, Spans, Tick};
use serde::{Deserialize, Serialize};

#[derive(Deserialize)]
struct Params {
    every: u64,
}

#[derive(Default, Serialize)]
struct Spot {
    #[serde(flatten)]
    span: Span,
    x: u32,
    y: u32,
    w: u32,
    h: u32,
}

struct SpotNode {
    v: u32,
    width: usize,
    spans: Spans<()>,
}

impl Node for SpotNode {
    const NAME: &'static str = "spot";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"every":{"type":"integer","minimum":1,"default":30}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(_: &Params, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .output(Output::rows("spots").schema::<Spot>()))
    }

    fn init(params: Params, init: &Init) -> Result<SpotNode> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is a video input")?;
        Ok(SpotNode {
            v: v.id,
            width: video.width as usize,
            spans: Spans::new().longest(params.every),
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        self.spans.tick(tick.time_base().seconds(frame.pts));
        let Some([x, y, w, h]) = grey(&tick.fetch(self.v, frame.index), self.width) else {
            return Ok(());
        };
        let spot = Spot { span: self.spans.see(()), x, y, w, h };
        Ok(out.row("spots", frame.pts, &spot)?)
    }
}

/// The box around every mid-grey pixel of an rgba picture.
fn grey(pixels: &[u8], width: usize) -> Option<[u32; 4]> {
    let mut found: Option<[usize; 4]> = None;
    for (at, pixel) in pixels.chunks_exact(4).enumerate() {
        let (lo, hi) = (pixel[..3].iter().min()?, pixel[..3].iter().max()?);
        if hi - lo < 24 && *lo > 96 && *hi < 160 {
            let (x, y) = (at % width, at / width);
            let [x0, y0, x1, y1] = found.get_or_insert([x, y, x, y]);
            (*x0, *y0, *x1, *y1) = ((*x0).min(x), (*y0).min(y), (*x1).max(x), (*y1).max(y));
        }
    }
    found.map(|[x0, y0, x1, y1]| [x0 as u32, y0 as u32, (x1 - x0 + 1) as u32, (y1 - y0 + 1) as u32])
}

ffrwd_node::export!(SpotNode);
```

Its declaration and a call, from the cookbook:

```pgsql
CREATE FUNCTION spot(v video_stream, every number DEFAULT 30)
RETURNS STRUCT(start_t number, id number, x number, y number, w number, h number)[]
  AS 'target/wasm32-wasip2/release/spot.wasm', 'spot'
  LANGUAGE wasm;

SELECT ffrwd.merge_spans(spot(f.video[1]), max_span => 10) FROM input('testsrc.mp4') f
```

The nine modules cookbook recipes 145 to 154 name (`spot`, `ring`, `dim`,
`hear`, `burn`, `inset`, `tile`, `matte`, `ticker`) are written with this
crate and live beside the sidecar's other test modules.

## Building

```toml
[lib]
crate-type = ["cdylib"]

[dependencies]
ffrwd-node = { git = "https://github.com/imbcmdth/ffrwd-node", tag = "v0.2.0" }
serde = { version = "1", features = ["derive"] }
```

```
cargo build --target wasm32-wasip2 --release
```

The bindings are generated from the repo's `wit/av.wit`, which is
`ffrwd:av@0.19.1` byte for byte and the one copy every SDK here builds
against, so a node module needs no `build.rs` and no wit of its own. With
`FFRWD_WIT_DIR` set, `cargo test` here checks that copy against the `av.wit`
it names.

`examples/dim.rs` is the node every SDK in this repo ships as its example:

```
cargo test --example dim
cargo build --target wasm32-wasip2 --release --example dim
ffrwd-wasm --shape target/wasm32-wasip2/release/examples/dim.wasm --bound v
```

## Testing on the host

Everything but the bindings builds on the host, and `export!` expands to
nothing there, so a module's tests run with `cargo test`. `mock::Harness`
opens a node the way the host does and hands it ticks built by hand:

```rust
use ffrwd_node::mock::Harness;
use ffrwd_node::{BoundStream, Rational};

let v = BoundStream::video("v", 0, 64, 48, "rgba", Rational::new(1, 15));
let mut spot = Harness::<SpotNode>::new(r#"{"every":3}"#, vec![v])?;
let emitted = spot.process(&spot.tick(0).frame(0, 0, pixels))?;
assert_eq!(emitted.messages("spots").len(), 1);
```

A bound stream's hint is what the harness shapes with: an audio stream's
is its sample rate, and `BoundStream::video(..).rate(Rational::new(25, 1))`
gives a picture's. A harness numbers each tick one past the last it
processed; `.ordinal(n)` numbers it as a worker handed every other tick would
see it. `.feed(id, feed)` sets a hold input's feed and `.ended(id, feed)`
adds one that ended.

## What it does not do

- Pixels. A node that draws, crops or converts colour uses
  [ffrwd-frame](https://github.com/imbcmdth/ffrwd-frame).
- Other worlds. `values`, `encoder` and `decoder`, and imports such as
  `wasi:nn` or `wasi:webgpu`, take bindings of the module's own.
- All of JSON Schema. The params are checked for the keywords listed on
  `read_params`; the compiler checks a call against the whole schema before
  the node sees it.
- Rows on an input that is not `state()` are not folded, and a node reading
  them keeps them itself.
- Waiting. A self-clocked node's `process` may block until it has something
  to emit; the crate neither helps nor hinders.

## From 0.1

A 0.1 module builds on 0.2 unchanged. Rebuild it: it now speaks
`ffrwd:av@0.19.1`, which a 0.19.0 host does not load. `Node::shape` keeps its
signature, and `Bound` reads as it did with `has`. What a module's tests may
need:

- `Runner::shape(params, bound)` takes a `Bound`, or the names it took
  before (`&["v".to_owned()]`, a `Vec<String>`).
- `FeedStart` has `known`: write it, or end the literal with
  `..Default::default()`.
- `Interval` has `anchor` and `group`: end the literal with
  `..Interval::default()`.
- `Bound` is no longer a tuple of names: build one with `Bound::new` and
  `.rate(..)`.
- `BoundStream` has `hint`; its constructors fill it in, and a video
  stream's rate is given with `.rate(..)`.

## License

MIT.
