# 1. Your first node

A node that inverts a picture's colours: one video input, one video output,
one frame out for every frame in. Build it, ask the toolchain what it is,
and call it from a query.

## The project

A node module is a library built for `wasm32-wasip2`. It depends on the SDK,
which carries the interface the host speaks, so the project holds no
interface files of its own.

@toml 01-first-node/invert/Cargo.toml

@cpp cpp/01-first-node/invert/build.sh sh

@js js/01-first-node/invert/package.json json

@go go/01-first-node/invert/go.mod -

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

@rust 01-first-node/invert/src/lib.rs

@cpp cpp/01-first-node/invert/src/invert.cpp

@js js/01-first-node/invert/src/invert.js

@go go/01-first-node/invert/main.go

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

@code js js/01-first-node/invert/build.js

**Go**

The module lands in `build/invert.wasm`:

```
componentize-go -d "$(go list -m -f '{{.Dir}}' github.com/imbcmdth/ffrwd-node/go)/wit" \
    -w ffrwd:av/node-module@0.19.1 build -o build/invert.wasm
```

## What it says about itself

`ffrwd-wasm` is the host. `--describe` prints what a module declares:

@command invert.describe.txt

@body invert.describe.txt

`"node": true` marks it as a node. The format lists are empty because a
node's ports say what each accepts, and `inputs` means nothing here. The
shape says the rest.

## Its shape

`--shape` asks the module for its ports, given the params of a call and the
inputs it binds. `--bound v` binds one stream to `v`:

@command invert.shape.txt

@body invert.shape.txt

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

@sql 01-first-node/run/invert.sql

`ffrwd compile` prints the plan:

@out 01-first-node-invert.sql.compile.txt

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
