# 10. Testing and shipping

A node is tested on the machine it is written on, without the host and
without any media. The host is `ffrwd-wasm`, the program that runs a module
beside ffmpeg. A node is shipped as a package that anyone can install. This
chapter takes the package that [chapter 1](01-first-node.md) starts from and
turns it into `acme/levels`, whose node stretches a picture's levels. The
chapter then tests the node, checks the node's shape from the command line,
and publishes the package.

## The node

`levels` moves the value `black` to 0 and the value `white` to 255, and
spreads every value in between across that range. A call with `black` at or
above `white` has no meaning. So the node's shape, which is the node's
answer about its ports and clock for a given call, refuses such a call. A
query that makes such a call is therefore refused when the query compiles.

@rust 10-testing/levels/src/lib.rs 1-69

@cpp cpp/10-testing/levels/src/levels.cpp

@js js/10-testing/levels/src/levels.js

@go go/10-testing/levels/main.go

## The mock harness

Everything in a module except the bindings to the host builds on the machine
the node is written on. So the node's tests run with the language's own test
runner. The SDK, which is the library the module is built with, includes a
mock harness. The mock harness is a stand-in for the host, and opens a node
the way the host does. The harness reads the params against the schema, asks
the node for its shape with the streams the test gives it, and opens an
instance of the node on those streams. An instance is one running copy of
the node. The test builds each tick by hand, where a tick is one call to the
node. A tick can carry:

- frames, with their bytes, their durations and the rows that arrive with
  them;
- messages on a data input, and the rows of earlier ticks on a state input,
  which is an input whose rows the node keeps across ticks;
- packets on a packets input;
- a held input's feed record, and the list of feeds that ended;
- the tick's ordinal, which is the tick's number in the run, and whether the
  tick is the last one.

Each stream the test binds carries what the compiler would have known about
the stream: its id, its format, its time base and, for asking the shape, its
rate. When the harness processes a tick, the harness returns what the node
emitted, port by port: new frames, frames handed on, messages and packets.
The harness checks every emission the way the host does. So a pts that goes
backwards fails the test.

@rust 10-testing/levels/src/lib.rs 71-120

@cpp cpp/10-testing/levels/src/levels_test.cpp

@js js/10-testing/levels/test/levels.test.js

@go go/10-testing/levels/main_test.go

**Rust**

```
cargo test
```

**C++**

```
sh build.sh test
```

**JavaScript**

```
npm test
```

**Go**

```
go test ./...
```

## Shapes on the command line

`ffrwd-wasm --shape` accepts the inputs that a call binds in two forms. In
the first form, input names alone bind one stream each, with no known rate.
Writing a name a second time binds one more stream to that port:

@command levels-names.shape.txt

In the second form, the list that the compiler passes gives every stream its
rate:

@command levels-rates.shape.txt

The shape of `levels` does not depend on the rate, so both forms print the
same shape:

@json levels-rates.shape.txt

The shape of a node that counts in frames or samples does depend on the
rate. `level`, from [chapter 5](05-window.md), refuses names alone:

@outs level-names.shape.txt

`--params` gives the params of a call. When the node refuses the call, the
refusal names the node:

@outs levels-refused.shape.txt

The compiler asks for the shape of every call in the same way. So the
compiler reports the refusal at the place in the query where the call is
written:

@sql 10-testing/run/refused.sql

@out 10-testing-refused.sql.compile.txt

## The package

The package around the node is the package from chapter 1, with its export
and its recipe renamed to `levels`. The package holds:

- `ffrwd.json`, the manifest;
- `ffrwd.lock`, the record of what the package installed, which only
  `install` writes;
- the build files and the node's source;
- `src/levels.sql`, which declares the module as the package's export;
- `recipes/levels.sql`, a recipe: a query that calls the export, and that
  can be run by name;
- `README.md`, which the registry shows;
- `.ffrwdignore` and `.gitignore`.

@toml 10-testing/levels/Cargo.toml

@cpp cpp/10-testing/levels/build.sh sh

**JavaScript**

The package has a `build.js` like the one in chapter 1, beside this
`package.json`:

@code json js/10-testing/levels/package.json

**Go**

The package has a `build.sh` that runs the `componentize-go` command from
chapter 1, beside this `go.mod`:

@code - go/10-testing/levels/go.mod

The export names the built module by the module's path from the root of the
package:

@rust 10-testing/levels/src/levels.sql sql

@cpp cpp/10-testing/levels/src/levels.sql sql

@js js/10-testing/levels/src/levels.sql sql

@go go/10-testing/levels/src/levels.sql sql

A recipe calls the export by its full name, which is the two halves of the
package's name followed by the export's name:

@sql 10-testing/levels/recipes/levels.sql

Run from inside the package, `ffrwd run levels -v source=in.mp4 -v
dest=out.mp4` runs the recipe.

The manifest names the export and the recipe, declares a dependency on the
interface that the module uses to talk to the host, and says how to test the
package:

@rust 10-testing/levels/ffrwd.json json

@cpp cpp/10-testing/levels/ffrwd.json json

@js js/10-testing/levels/ffrwd.json json

@go go/10-testing/levels/ffrwd.json json

`capabilities` lists what the module asks the host to grant: `nn` to run a
model, `http`, `udp`, `tcp` and `gpu`. `levels` asks for none of them.
`test` is a command that `ffrwd publish` runs before anything leaves the
machine.

## Publishing

`ffrwd login --token <token>` saves the token that this machine publishes
with. `ffrwd publish` can be run anywhere inside the package. Before `ffrwd
publish` sends a single byte, it checks the whole package on this machine:

- the manifest can be read, and its name is a valid package name;
- every export parses, and defines what the manifest says the export
  defines;
- every dependency resolves in the registry;
- the host describes every module, and the capabilities of the version being
  published are set from what each module uses, such as a model;
- every pinned model names an export that one of the modules declares;
- the manifest's `test` command runs, and the command's exit status decides
  whether the publish goes on. A manifest that declares no `test` command
  skips this check.

Then `ffrwd publish` packs the package and sends it. The archive holds the
manifest, every file the manifest names, every module that the package's
exports declare, the README, the licence, and whatever the manifest's
`files` list adds. So the built module ships, even though `.ffrwdignore`
names the directory the module is built in. The module is what the package
delivers, and the rest of the tree the module was built from stays on this
machine.

A published version never changes. Publishing the same bytes again changes
nothing. Publishing different bytes under the same version is refused, so
any change needs a new version. `"private": true` in the manifest publishes
the version as private.
