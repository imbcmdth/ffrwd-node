# Writing ffrwd modules

ffrwd compiles SQL into ffmpeg pipelines. When a query needs something
ffmpeg cannot do on its own, such as running a model over the picture,
drawing what a detector found, cutting to a feed that connects while the
query is running, or publishing to a relay, the query calls a module. A
module is a WebAssembly component that ffrwd runs beside ffmpeg.

A module that reads or writes streams is called a node. A node has typed
input ports, typed output ports, and a clock. The clock is what drives the
node: once per tick, which is usually once per frame of one chosen input,
the host gathers what every input holds for that tick and calls the node
with it. A node's ports are not fixed in advance. They depend on the
parameters of the call and on which inputs the query connects, and the
compiler asks the module for them before anything runs, so a query that
connects the wrong kind of stream is refused at compile time.

This guide builds one kind of node per chapter. Every example is a complete
module that builds, every query shown has been compiled with `ffrwd
compile`, and every output shown is what the tools printed.

1. [Your first node](01-first-node.md): build a module, ask the toolchain
   what it declares, and call it from a query.
2. [A per-frame filter](02-filter.md): one picture in, one picture out;
   parameters; a second input that runs in step with the first; passing a
   frame through untouched.
3. [A detector](03-detector.md): a node that writes rows instead of
   pictures, one row per tick, each named by the time the thing it describes
   began.
4. [A reader of rows](04-reader.md): a node that reads rows other nodes
   wrote, paired with the picture by time; rows kept as state; a picture
   read only for its timing.
5. [A window](05-window.md): a node that sees many frames or samples per
   tick, and outputs that are allowed to arrive late.
6. [A source](06-source.md): a node with no inputs and a clock of its own.
7. [A sink](07-sink.md): a node that takes encoded packets and writes
   nothing but rows.
8. [Held inputs](08-held.md): feeds that connect and disconnect while the
   query runs, bound to a stream or served on a network port.
9. [Staying pure](09-pure.md): what lets the host run a node on several
   threads at once, and what prevents it.
10. [Testing and shipping](10-testing.md): the mock harness, checking
    shapes from the command line, the package manifest, and `ffrwd
    publish`.

Read chapter 1 first. The other chapters can be read in any order, though
some later chapters call nodes that earlier chapters built.

Every code example is given in four languages: Rust, C++, JavaScript and Go.
Pick a language once, with the picker above any example, and the guide
shows that language everywhere.

Each language has an SDK. The SDK carries the bindings to the host, runs
the sequence of calls the host makes, reads parameters against their JSON
schema, builds the shape the module declares, and checks each emission
before it leaves the module. Modules built with these SDKs run on ffrwd 0.29
or later.

**Rust**

The crate is [ffrwd-node](https://github.com/imbcmdth/ffrwd-node). Crops
and resizes come from [ffrwd-frame](https://github.com/imbcmdth/ffrwd-frame).

```
rustup target add wasm32-wasip2
cargo build --target wasm32-wasip2 --release
```

**C++**

The SDK is [ffrwd-node's C++
library](https://github.com/imbcmdth/ffrwd-node/tree/main/cpp). It needs
[wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases) 34 and
[wit-bindgen](https://github.com/bytecodealliance/wit-bindgen/releases)
0.57.1, with the environment variable `WASI_SDK` set to the directory
wasi-sdk is installed in. Crops and resizes come from its `ffrwd/frame.hpp`
header.

```
sh build.sh modules    # in the SDK's cpp/, once
sh build.sh            # in the module's directory
```

**JavaScript**

The SDK is [@ffrwd/node](https://github.com/imbcmdth/ffrwd-node/tree/main/js).
ComponentizeJS compiles the module into a component. Crops and resizes come
from the `@ffrwd/node/frame` module.

```
npm install
npm run build
```

**Go**

The SDK is the module
[github.com/imbcmdth/ffrwd-node/go](https://github.com/imbcmdth/ffrwd-node/tree/main/go).
It needs Go 1.25.5 or newer and componentize-go 0.4.1. Crops and resizes
come from its `frame` package.

```
componentize-go -d "$(go list -m -f '{{.Dir}}' github.com/imbcmdth/ffrwd-node/go)/wit" \
    -w ffrwd:av/node-module@0.19.1 build -o build/<name>.wasm
```

The queries in this guide read the test media from ffrwd's own repository:
`testsrc.mp4`, `av.mp4`, `av2.mp4` and `smptebars.mp4`. Each is four seconds
long, 320x240, at 15 frames per second.
