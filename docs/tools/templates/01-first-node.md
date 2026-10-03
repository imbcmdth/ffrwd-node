# 1. Your first node

This chapter builds a node that inverts the colours of a picture: one video
input, one video output, one frame out for every frame in. It builds the
module, asks the toolchain what the module declares, and calls the module
from a query.

## The project

A node module is a library compiled for the `wasm32-wasip2` target. The
module depends on the SDK, and the SDK carries the interface the host
speaks, so the project itself holds no interface files.

@toml 01-first-node/invert/Cargo.toml

@cpp cpp/01-first-node/invert/build.sh sh

@js js/01-first-node/invert/package.json json

@go go/01-first-node/invert/go.mod -

## The node

The host makes four kinds of call to a node.

- **Describe.** The host asks for the module's name, its version and the
  JSON Schema of its parameters. This node takes no parameters.
- **Shape.** The host asks for the node's ports and its clock. This node
  has one video input named `v`, which is also its clock and must arrive as
  rgba, and one output named `v` in the same format as the input.
- **Init.** The host opens an instance of the node. It binds a stream to
  each input the query connected and hands the node an id for each stream.
  The node keeps the id, because every later call names the stream by it.
- **Process.** Once per tick, the host hands the node the current frame of
  its clock input. The node fetches the frame's bytes, inverts every colour
  channel, and emits the new picture with the same pts and duration as the
  frame it came from.

The shape says two more things about this node. It is pure: each tick
depends only on what the host handed it, so the host may run the node on
several worker threads at once. And it is one to one: exactly one frame
leaves for every frame that arrives, at the same pts.

@rust 01-first-node/invert/src/lib.rs

@cpp cpp/01-first-node/invert/src/invert.cpp

@js js/01-first-node/invert/src/invert.js

@go go/01-first-node/invert/main.go

A frame's bytes are copied into the module only when the node fetches
them. Until then the node sees only the frame's pts, its duration, and its
position in the tick. On the last call, which comes after the input has
ended, the host may hand no frame at all; the node then returns with
nothing to emit.

The SDK checks every emission as the node makes it. The port must be one
the shape declares, the payload must be of that port's kind, and the pts on
a port must never go backwards. A node that breaks any of these ends the
run with an error that names the port.

## Build it

**Rust**

The module is written to `target/wasm32-wasip2/release/invert.wasm`:

```
cargo build --target wasm32-wasip2 --release
```

**C++**

With `WASI_SDK` set to the directory wasi-sdk is installed in, the module is
written to `build/invert.wasm`:

```
sh build.sh
```

**JavaScript**

Run `npm install` once, then `npm run build`, which runs `build.js`. The
module is written to `build/invert.wasm`:

@code js js/01-first-node/invert/build.js

**Go**

The module is written to `build/invert.wasm`:

```
componentize-go -d "$(go list -m -f '{{.Dir}}' github.com/imbcmdth/ffrwd-node/go)/wit" \
    -w ffrwd:av/node-module@0.19.1 build -o build/invert.wasm
```

## What the module declares

`ffrwd-wasm` is the host. Its `--describe` flag prints what a module
declares about itself:

@command invert.describe.txt

@body invert.describe.txt

`"node": true` marks the module as a node. The format lists are empty
because for a node the ports say what each one accepts, and the `inputs`
count means nothing for a node. The shape, below, says the rest.

## Its shape

The `--shape` flag asks the module for its ports and clock. The module
answers for a given call, so the flag takes the call's parameters and the
names of the inputs the call binds. `--bound v` binds one stream to the
input `v`:

@command invert.shape.txt

@body invert.shape.txt

The clock is the input `v`. Each tick is one frame of `v`: a window of one
frame, advancing one frame at a time. A clock input is always lockstep,
single and required. The output declares no format and no time base, so it
takes both from `v`. The compiler reads all of this before it plans the
query. A call that binds the wrong kind of stream to `v`, or leaves `v`
unbound, is refused at compile time.

## Calling it from a query

A query declares the module as a function. In the declaration, the first
string is the module's path and the second is the node's name. The
function's parameters are the node's ports and its params; here `v
video_stream` is the port `v`.

@sql 01-first-node/run/invert.sql

`ffrwd compile` prints the plan:

@out 01-first-node-invert.sql.compile.txt

The first ffmpeg decodes the picture to rgba, the format `v` accepts.
`ffrwd-wasm` runs the node: `[v=0:v]invert[v=out0]` binds the input's video
to the port `v` and gives a label to what the port `v` writes. `-bound` is
the list of inputs the compiler bound when it asked for the shape, with each
stream's frame rate. The host asks the module for its shape again with the
same list, so the node runs with the shape the query was planned against.
The last ffmpeg encodes the result together with the file's own sound.

`ffrwd run -f invert.sql` runs the query.

## Starting from a package

**Rust**

`ffrwd init --rust` writes a package whose module is ready to build:

```
$ ffrwd init --rust --name acme/invert
```

**C++**

`ffrwd init` writes no C++ module, so the package is written by hand. The
one used here is `docs/examples/cpp/01-first-node/scaffold` in
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

`ffrwd init` writes no JavaScript module, so the package is written by
hand. The one used here is `docs/examples/js/01-first-node/scaffold` in
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

`ffrwd init` writes no Go module, so the package is written by hand. The
one used here is `docs/examples/go/01-first-node/scaffold` in
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

The package holds the manifest `ffrwd.json`, an empty lock file
`ffrwd.lock`, the build files, and a node named `passthrough` that hands its
one video input back untouched. Beside them are `src/passthrough.sql`,
which declares the module as the package's export, `recipes/passthrough.sql`,
a query that calls it, and a `README.md`. The node's own work goes in its
process call. Chapter 10 turns a package like this one into one that is
ready to publish.
