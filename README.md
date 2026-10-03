# ffrwd-node

A node is what an ffrwd module is from `ffrwd:av` 0.19.1 on: typed input
ports, typed output ports and a clock. The host calls it once a tick with
what each input holds for that tick, and it emits on its outputs. Its ports
follow from the call's params and from which inputs the call binds, and the
query's compiler reads them before anything runs.

This repo is the SDKs that write one, a language each, all built against one
world: `wit/av.wit`, which is `ffrwd:av@0.19.1` byte for byte. Every SDK
carries the same things under the same names, spelled the way its language
spells them: the call sequence, params read against their schema, shapes
from builders, time in any time base, rows of a state input folded,
emissions checked as they are made, errors as the run's message, and a
harness that drives a node on the host for its tests. Every SDK ships the
same example, `dim`, which darkens each picture by `amount`; built, the
three answer `ffrwd-wasm --shape` with the same shape and write the same
frames.

Requires ffrwd 0.29, whose `ffrwd/wasm` is 0.19.1.

## Rust

`rust/` is the `ffrwd-node` crate: implement `Node` for a type, hand it to
`export!`, and build for `wasm32-wasip2`. A node module needs no wit and no
`build.rs` of its own. Its components are the smallest here and the fastest per frame.

```toml
ffrwd-node = { git = "https://github.com/imbcmdth/ffrwd-node", tag = "v0.2.0" }
```

## JavaScript

`js/` is `@ffrwd/node`: define a node as an object, export what
`defineNode` makes of it, and build it with ComponentizeJS through
`buildNode`. The component carries a JavaScript engine, so it starts in a
second and is far slower per pixel than Rust; it is the road for a node
whose value is a library nobody wants to port. It is not on npm, and npm
cannot install a directory of a git repo, so a module depends on a
checkout:

```json
"@ffrwd/node": "file:../ffrwd-node/js"
```

## Go

`go/` is `github.com/imbcmdth/ffrwd-node/go`: describe a node in a
`node.Definition`, hand it to `node.Export` from `init`, and build with
componentize-go on mainline Go. The package keeps the collector to the
points where the component model allows it to run, which a Go component
needs.

```
go get github.com/imbcmdth/ffrwd-node/go
```

## C++

`cpp/` is where the C++ SDK lands.

## Building and testing

| | test | build `dim` |
| --- | --- | --- |
| Rust, from `rust/` | `cargo test` | `cargo build --target wasm32-wasip2 --release --example dim` |
| JavaScript, from `js/` | `npm test` | `npm run example` |
| Go, from `go/` | `go test ./...` | `sh build.sh` |

`docs/` is where the guide goes.

## License

MIT.
