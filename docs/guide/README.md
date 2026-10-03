# Writing ffrwd modules

A module is how a query does something ffmpeg cannot: read a picture with a
model, draw what a detector found, mix in a feed that connects while the
query runs, publish to a relay. A module that reads or writes streams is a
node. It has typed inputs, typed outputs and a clock, and the host calls it
once a tick with what each input holds for that tick. Its ports follow from
the call's params and from the inputs the call binds, and the compiler reads
them before anything runs.

This guide builds nodes, one kind to a chapter. Each chapter's examples are
whole modules that build, and each query in it is one `ffrwd compile` has
run.

1. [Your first node](01-first-node.md): build one, ask it what it is, call
   it from a query.
2. [A per-frame filter](02-filter.md): a picture in, a picture out, params,
   a second input in step with the first, and a frame handed on untouched.
3. [A detector](03-detector.md): rows out, one a tick, named by when the
   thing they describe began.
4. [A reader of rows](04-reader.md): rows paired with the picture by time,
   kept as state, and a picture read for its timing alone.
5. [A window](05-window.md): many frames or samples a tick, and outputs that
   leave late.
6. [A source](06-source.md): no inputs, a clock of its own.
7. [A sink](07-sink.md): coded packets in, nothing out but rows.
8. [Held inputs](08-held.md): feeds that come and go, by stream or by port.
9. [Staying pure](09-pure.md): what lets the host spread a node over
   workers, and what stops it.
10. [Testing and shipping](10-testing.md): the mock harness, shapes on the
    command line, the manifest and `ffrwd publish`.

Read the first chapter first. The rest stand alone, though the later ones
call nodes the earlier ones built.

Each example appears in one language at a time: pick yours above any of them
and the guide keeps that choice everywhere.

Each language has an SDK that carries the bindings and the call sequence,
reads params against their schema, builds shapes and checks emissions. The
modules run on ffrwd 0.29.

**Rust**

[ffrwd-node](https://github.com/imbcmdth/ffrwd-node), and
[ffrwd-frame](https://github.com/imbcmdth/ffrwd-frame) for crops and
resizes.

```
rustup target add wasm32-wasip2
cargo build --target wasm32-wasip2 --release
```

**C++**

[ffrwd-node's C++
SDK](https://github.com/imbcmdth/ffrwd-node/tree/main/cpp), with
[wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases) 34 and
[wit-bindgen](https://github.com/bytecodealliance/wit-bindgen/releases)
0.57.1. The examples' crops and resizes are their own.

```
sh build.sh modules    # in the SDK's cpp/, once
sh build.sh            # in the module's directory
```

**JavaScript**

[@ffrwd/node](https://github.com/imbcmdth/ffrwd-node/tree/main/js), built
into a component with ComponentizeJS. The examples' crops and resizes are
their own.

```
npm install
npm run build
```

**Go**

[github.com/imbcmdth/ffrwd-node/go](https://github.com/imbcmdth/ffrwd-node/tree/main/go),
with Go 1.25.5 or newer and componentize-go 0.4.1. The examples' crops and
resizes are their own.

```
componentize-go -d "$(go list -m -f '{{.Dir}}' github.com/imbcmdth/ffrwd-node/go)/wit" \
    -w ffrwd:av/node-module@0.19.1 build -o build/<name>.wasm
```

The queries read the test media in ffrwd's own repository: `testsrc.mp4`,
`av.mp4`, `av2.mp4` and `smptebars.mp4`, each four seconds at 320x240 and 15
frames a second.
