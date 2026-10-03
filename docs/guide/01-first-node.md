# 1. Your first node

A node that inverts a picture's colours: one video input, one video output,
one frame out for every frame in. Build it, ask the toolchain what it is,
and call it from a query.

## The project

A node module is a library built for `wasm32-wasip2`. It depends on the SDK,
which carries the interface the host speaks, so the project holds no
interface files of its own.

**Rust**

```toml
[package]
name = "invert"
version = "0.1.0"
edition = "2021"

[lib]
crate-type = ["cdylib"]

[dependencies]
ffrwd-node = { git = "https://github.com/imbcmdth/ffrwd-node", tag = "v0.2.0" }
```

## The node

A node answers four calls.

- **Its name and params.** What `describe` reports: the module's name, its
  version and the JSON Schema of its params. This one takes none.
- **Its shape.** The ports and the clock. One video input `v`, which is the
  clock and arrives as rgba, and one output named `v` in `v`'s format.
- **Opening an instance.** The host binds a stream to each input the call
  connects and hands the node an id per stream. The node keeps the id; every
  later call names the stream by it.
- **Each tick.** The tick hands the clock input's frame. The node fetches
  its bytes, inverts every colour channel and emits the new picture at the
  frame's own pts and duration.

The shape says two more things. The node is pure: each tick depends only on
what it was handed, so the host may run it on several workers at once. And
it is one to one: one frame leaves for every frame that arrives, at the same
pts.

**Rust**

```rust
use ffrwd_node::{Bound, Init, Input, NoParams, Node, Out, Output, Result, Shape, Tick};

struct Invert {
    v: u32,
}

impl Node for Invert {
    const NAME: &'static str = "invert";
    const VERSION: &'static str = "0.1.0";
    type Params = NoParams;

    fn shape(_: &NoParams, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .output(Output::like("v"))
            .pure()
            .one_to_one())
    }

    fn init(_: NoParams, init: &Init) -> Result<Invert> {
        Ok(Invert {
            v: init.stream("v")?.id,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        let mut pixels = tick.fetch(self.v, frame.index);
        for pixel in pixels.as_chunks_mut::<4>().0 {
            for channel in &mut pixel[..3] {
                *channel = 255 - *channel;
            }
        }
        Ok(out.frame("v", frame.pts, frame.duration, pixels)?)
    }
}

ffrwd_node::export!(Invert);
```

A frame's bytes enter the module only when the node fetches them. Until then
a frame is its pts, its duration and its place in the tick. The last call,
after the input has ended, may hand no frame at all, and the node returns
with nothing to emit.

Every emission is checked as it is made. The port has to be one the shape
declares and of the payload's kind, and its pts never go back. A node that
breaks either ends the run with the port named.

## Build it

**Rust**

```
cargo build --target wasm32-wasip2 --release
```

The module is `target/wasm32-wasip2/release/invert.wasm`.

## What it says about itself

`ffrwd-wasm` is the host. `--describe` prints what a module declares:

```
$ ffrwd-wasm --describe target/wasm32-wasip2/release/invert.wasm
{
  "world": "node-module",
  "name": "invert",
  "version": "0.1.0",
  "params_schema": {"additionalProperties": false, "properties": {}, "type": "object"},
  "rows_schema": null,
  "pixel_formats": [],
  "sample_formats": [],
  "sample_rates": [],
  "channel_counts": [],
  "rows_language": [],
  "packet_sink": false,
  "packet_filter": false,
  "source": false,
  "rows_module": false,
  "data_filter": false,
  "inputs": 1,
  "nn": false,
  "http": false,
  "udp": false,
  "tcp": false,
  "gpu": false,
  "node": true
}
```

`"node": true` marks it as a node. The format lists are empty because a
node's ports say what each accepts, and `inputs` means nothing here. The
shape says the rest.

## Its shape

`--shape` asks the module for its ports, given the params of a call and the
inputs it binds. `--bound v` binds one stream to `v`:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/invert.wasm --bound v
{
  "bounded": true,
  "clock": {"kind": "input", "port": "v"},
  "inputs": [
    {
      "accepts": {
        "channel_counts": [],
        "codecs": [],
        "like": null,
        "pixel_formats": ["rgba"],
        "sample_formats": [],
        "sample_rates": [],
        "wants": "all"
      },
      "kind": "video",
      "many": false,
      "name": "v",
      "pairing": {"kind": "lockstep"},
      "required": true,
      "rows": "ignore",
      "schema": null,
      "stride": 1,
      "window": 1
    }
  ],
  "one_to_one": true,
  "outputs": [
    {
      "format": {"kind": "like", "pixel_format": null, "port": "v", "sample_format": null},
      "kind": "video",
      "latency": 0.0,
      "name": "v",
      "row": null,
      "schema": null,
      "time_base": null
    }
  ],
  "pure": true,
  "relation": []
}
```

The clock is the input `v`. Each tick is one of its frames: a window of one,
moving one at a time. A clock input is always lockstep, single and required.
The output takes its format from `v`, and its time base too, since it names
none. The compiler reads all of this before it plans the query, so a call
that binds the wrong kind of stream, or leaves `v` out, is refused at
compile time.

## Calling it from a query

A query declares the module as a function. The first string is the module's
path, the second its name. The parameters are its ports and its params, and
`v video_stream` here is the port `v`.

```sql
CREATE FUNCTION invert(v video_stream) RETURNS video_stream
  AS 'invert.wasm', 'invert' LANGUAGE wasm;

COPY (
  SELECT invert(f.video[1]), f.audio[1]
  FROM input('av.mp4') f
) TO 'inverted.mp4' WITH (video_codec 'libx264', audio_codec 'aac')
```

`ffrwd compile` prints the plan:

```
$ ffrwd compile -f invert.sql
ffmpeg -i av.mp4 -map 0:v:0 -c:0 rawvideo -pix_fmt:0 rgba -f nut pipe:1 | ffrwd-wasm \
  -f nut -i pipe:0 -pad \
  '{"color": {"range": "pc", "primaries": "unknown", "trc": "unknown", "space": "gbr"}}' \
  -m invert=invert.wasm -filter_complex '[v=0:v]invert[v=out0]' -bound \
  'invert=[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]' -map '[out0]' -f \
  nut pipe:1 | ffmpeg -i av.mp4 -f nut -analyzeduration 0 -fpsprobesize 3 -i pipe:0 \
  -map 1:v:0 -map 0:a:0 -c:0 libx264 -c:1 aac inverted.mp4
```

The first ffmpeg decodes the picture to rgba, the format `v` accepts.
`ffrwd-wasm` runs the node: `[v=0:v]invert[v=out0]` binds the input's video
to the port `v` and labels what the port `v` writes. `-bound` is the list of
inputs the compiler asked the shape with, each stream with its rate, and the
host asks the shape again with the same list, so the node runs with the
shape the query was planned against. The last ffmpeg encodes the result
beside the file's own sound.

`ffrwd run -f invert.sql` runs it.

## Starting from a package

`ffrwd init --rust` writes a package whose module is ready to build:

**Rust**

```
$ ffrwd init --rust --name acme/invert
```

It writes the manifest `ffrwd.json` and an empty `ffrwd.lock`, a
`Cargo.toml` taking `ffrwd-node` and `ffrwd-frame`, and a node named
`passthrough` that hands its one video input back untouched. Beside them go
`src/passthrough.sql`, which declares the module as the package's export,
`recipes/passthrough.sql`, a query calling it, and a `README.md`. The node's
work goes in its tick. Chapter 10 turns such a package into one ready to
publish.
