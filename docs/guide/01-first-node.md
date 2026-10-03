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
ffrwd-node = { path = "../../../../rust" }
```

**C++**

```sh
#!/bin/sh
# Builds build/invert.wasm: the node compiled with wasi-sdk and linked with
# the ffrwd-node C++ library, which `sh build.sh modules` in the SDK's cpp/
# directory builds.
#
#   FFRWD_NODE        the SDK's cpp/ directory
#   FFRWD_NODE_BUILD  where its library was built, $FFRWD_NODE/build unless set
#   WASI_SDK          wasi-sdk 34 or newer
set -eu
cd "$(dirname "$0")"
sdk=${FFRWD_NODE:-../../../../../cpp}
lib=${FFRWD_NODE_BUILD:-$sdk/build}
wasi=${WASI_SDK:-C:/tools/wasi-sdk-34.0-x86_64-windows}
cxx="$wasi/bin/clang++ --target=wasm32-wasip2 -std=c++23 -fno-exceptions -fno-rtti"
mkdir -p build

$cxx -O2 -I"$sdk/include" -c src/invert.cpp -o build/invert.o
$cxx -mexec-model=reactor -Wl,--gc-sections -Wl,--strip-all -o build/invert.wasm build/invert.o \
    "$lib/wasm/libffrwd-node.a" "$lib/wasm/node_module.o" "$lib/gen/node_module_component_type.o"
```

**JavaScript**

```json
{
  "name": "invert",
  "version": "0.1.0",
  "private": true,
  "type": "module",
  "scripts": {
    "build": "node build.js"
  },
  "dependencies": {
    "@ffrwd/node": "file:../../../../../js"
  },
  "devDependencies": {
    "@bytecodealliance/componentize-js": "0.22.0",
    "esbuild": "^0.25.0"
  }
}
```

**Go**

```
module invert

go 1.25

require github.com/imbcmdth/ffrwd-node/go v0.0.0

require go.bytecodealliance.org/pkg v0.2.2 // indirect

replace github.com/imbcmdth/ffrwd-node/go => ../../../../../go
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

**C++**

```cpp
#include "ffrwd/node.hpp"

struct Invert : ffrwd::Node<Invert> {
    static constexpr std::string_view name = "invert";
    static constexpr std::string_view version = "0.1.0";

    std::uint32_t v = 0;

    static ffrwd::Result<ffrwd::Shape> shape(const ffrwd::NoParams&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .output(ffrwd::Output::like("v"))
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<Invert> init(ffrwd::NoParams, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        Invert node;
        node.v = v.id;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        for (std::size_t at = 0; at < pixels.size(); at += 4)
            for (std::size_t channel = at; channel < at + 3; ++channel)
                pixels[channel] = 255 - pixels[channel];
        return out.frame("v", frame->pts, frame->duration, std::move(pixels));
    }
};

FFRWD_EXPORT(Invert);
```

**JavaScript**

```js
import { defineNode, Input, Output, Shape } from '@ffrwd/node';

export const node = defineNode({
  name: 'invert',
  version: '0.1.0',

  shape() {
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .output(Output.like('v'))
      .pure()
      .oneToOne();
  },

  init(_, init) {
    const v = init.stream('v').id;
    return {
      process(tick, out) {
        const frame = tick.frame(v);
        if (frame === undefined) return;
        const pixels = tick.fetch(v, frame.index);
        for (let at = 0; at < pixels.length; at += 4) {
          pixels[at] = 255 - pixels[at];
          pixels[at + 1] = 255 - pixels[at + 1];
          pixels[at + 2] = 255 - pixels[at + 2];
        }
        out.frame('v', frame.pts, frame.duration, pixels);
      },
    };
  },
});
```

**Go**

```go
package main

import node "github.com/imbcmdth/ffrwd-node/go"

type Invert struct {
	v uint32
}

var Definition = node.Definition[struct{}]{
	Name:    "invert",
	Version: "0.1.0",
	Shape: func(struct{}, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Output(node.LikeOutput("v")).
			Pure().
			OneToOne(), nil
	},
	Init: func(_ struct{}, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		return &Invert{v: v.ID}, nil
	},
}

func (i *Invert) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(i.v)
	if !ok {
		return nil
	}
	pixels := tick.Fetch(i.v, frame.Index)
	for at := 0; at+3 < len(pixels); at += 4 {
		for channel := at; channel < at+3; channel++ {
			pixels[channel] = 255 - pixels[channel]
		}
	}
	return out.Frame("v", frame.Pts, frame.Duration, pixels)
}

func init() { node.Export(Definition) }

func main() {}
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

The module lands in `target/wasm32-wasip2/release/invert.wasm`:

```
cargo build --target wasm32-wasip2 --release
```

**C++**

The module lands in `build/invert.wasm`:

```
sh build.sh
```

**JavaScript**

`npm install` once, then `npm run build`, which runs `build.js`. The module
lands in `build/invert.wasm`:

```js
import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/invert.js', out: 'build/invert.wasm' });
```

**Go**

The module lands in `build/invert.wasm`:

```
componentize-go -d "$(go list -m -f '{{.Dir}}' github.com/imbcmdth/ffrwd-node/go)/wit" \
    -w ffrwd:av/node-module@0.19.1 build -o build/invert.wasm
```

## What it says about itself

`ffrwd-wasm` is the host. `--describe` prints what a module declares:

**Rust**

```
$ ffrwd-wasm --describe target/wasm32-wasip2/release/invert.wasm
```

**C++**

```
$ ffrwd-wasm --describe build/invert.wasm
```

**JavaScript**

```
$ ffrwd-wasm --describe build/invert.wasm
```

**Go**

```
$ ffrwd-wasm --describe build/invert.wasm
```

```json
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

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/invert.wasm --bound v
```

**C++**

```
$ ffrwd-wasm --shape build/invert.wasm --bound v
```

**JavaScript**

```
$ ffrwd-wasm --shape build/invert.wasm --bound v
```

**Go**

```
$ ffrwd-wasm --shape build/invert.wasm --bound v
```

```json
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

**Rust**

`ffrwd init --rust` writes a package whose module is ready to build:

```
$ ffrwd init --rust --name acme/invert
```

**C++**

`ffrwd init` writes no C++ module, so the package is written by hand. This
one is `docs/examples/cpp/01-first-node/scaffold` in
[ffrwd-node](https://github.com/imbcmdth/ffrwd-node):

```
ffrwd.json
ffrwd.lock
build.sh
src/passthrough.cpp
src/passthrough.sql
recipes/passthrough.sql
README.md
.ffrwdignore
.gitignore
```

**JavaScript**

`ffrwd init` writes no JavaScript module, so the package is written by hand.
This one is `docs/examples/js/01-first-node/scaffold` in
[ffrwd-node](https://github.com/imbcmdth/ffrwd-node):

```
ffrwd.json
ffrwd.lock
package.json
build.js
src/invert.js
src/passthrough.sql
recipes/passthrough.sql
README.md
.ffrwdignore
.gitignore
```

**Go**

`ffrwd init` writes no Go module, so the package is written by hand. This
one is `docs/examples/go/01-first-node/scaffold` in
[ffrwd-node](https://github.com/imbcmdth/ffrwd-node):

```
ffrwd.json
ffrwd.lock
go.mod
go.sum
main.go
build.sh
src/passthrough.sql
recipes/passthrough.sql
README.md
.ffrwdignore
.gitignore
```

The package holds the manifest `ffrwd.json` and an empty `ffrwd.lock`, the
build files, and a node named `passthrough` that hands its one video input
back untouched. Beside them go `src/passthrough.sql`, which declares the
module as the package's export, `recipes/passthrough.sql`, a query calling
it, and a `README.md`. The node's work goes in its tick. Chapter 10 turns
such a package into one ready to publish.
