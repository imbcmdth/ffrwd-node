# 10. Testing and shipping

A node is tested on the machine it is written on, without a host and without
media, and shipped as a package anyone can install. This chapter turns the
package `ffrwd init --rust` writes into `acme/levels`, whose node stretches
a picture's levels, tests it, checks its shape from the command line and
publishes it.

## The node

`levels` moves `black` to 0 and `white` to 255 and spreads everything
between over the range. A call with `black` at or over `white` has no
meaning, so the shape refuses it, and the query that makes such a call is
refused when it compiles.

@rust 10-testing/levels/src/lib.rs 1-69

## The mock harness

Everything but the bindings builds on the machine the node is written on, so
its tests run with the language's own test runner. The SDK's mock harness
opens a node the way the host does: it reads the params against the schema,
asks the shape with the streams it is given and opens an instance on them.
Each tick is built by hand:

- frames with their bytes, their duration and the rows that arrive with
  them;
- messages on a data input, and earlier rows on a state input;
- packets on a packets input;
- a held input's feed record, and the feeds that ended;
- the tick's ordinal, and whether it is the last.

A bound stream carries what the compiler would have known: its id, its
format, its time base and, for the shape, its rate. Processing a tick hands
back what the node emitted, port by port: new frames, frames handed on,
messages and packets. The harness checks every emission as the host does, so
a pts that goes back fails the test.

@rust 10-testing/levels/src/lib.rs 71-120

**Rust**

```
cargo test
```

## Shapes on the command line

`ffrwd-wasm --shape` takes the inputs a call binds in two forms. The names
alone bind one stream each, at no known rate; a name written again binds one
more stream to that port:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/levels.wasm --bound v
```

The list the compiler passes gives every stream its rate:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/levels.wasm --bound '[{"input":"v","streams":[{"rate":{"num":25,"den":1}}]}]'
```

`levels` does not turn on the rate, so both print the same shape:

@json levels-rates.shape.txt

A node that counts in frames or samples does turn on it. `level`, from
[chapter 5](05-window.md), refuses the names alone:

@out level-names.shape.txt

`--params` gives a call's params, and a node's refusal comes back naming the
node:

@out levels-refused.shape.txt

The compiler asks the same question for every call, so the query hears the
refusal where the call is written:

@sql 10-testing/run/refused.sql

@out 10-testing-refused.sql.compile.txt

## The package

`ffrwd init --rust` writes the package around the node. After renaming its
export and recipe to `levels`, it holds:

- `ffrwd.json`, the manifest;
- `ffrwd.lock`, what the package installed, which nothing but `install`
  writes;
- the build files and the node's source;
- `src/levels.sql`, which declares the module as the package's export;
- `recipes/levels.sql`, a query that calls the export, run by name;
- `README.md`, which the registry shows;
- `.ffrwdignore` and `.gitignore`.

@toml 10-testing/levels/Cargo.toml

The export names the built module by its path from the package's root:

@sql 10-testing/levels/src/levels.sql

A recipe calls it by its full name, the package's two halves and the
export's:

@sql 10-testing/levels/recipes/levels.sql

`ffrwd run levels -v source=in.mp4 -v dest=out.mp4` runs it from inside the
package.

The manifest names the export and the recipe, depends on the interface the
module speaks, and says how to test the package:

@json-file 10-testing/levels/ffrwd.json

`capabilities` lists what the module asks the host to grant: `nn` for a
model, `http`, `udp`, `tcp`, `gpu`. `levels` asks for none. `test` is a
command `ffrwd publish` runs before anything leaves the machine.

## Publishing

`ffrwd login --token <token>` saves the token this machine publishes with.
`ffrwd publish`, run anywhere inside the package, checks the package whole,
on this machine, before it sends a byte:

- the manifest reads, and its name is one a package may have;
- every export parses and defines what the manifest says it does;
- every dependency resolves in the registry;
- the host describes every module, and what each one uses, a model for one,
  sets the version's capabilities;
- every model pinned names an export one of the modules declares;
- the manifest's `test` command runs, and its exit decides whether the
  publish goes on. A manifest that declares none is not checked.

Then it packs the package and sends it. The archive is the manifest, every
file the manifest names, every module its exports declare, the README and
the licence, and whatever the manifest's `files` adds. That is why the built
module ships out of `target/` although `.ffrwdignore` names the directory:
the module is the package, and the tree it was built from stays home.

A published version never changes. Publishing the same bytes again changes
nothing; publishing different bytes under the same version is refused, so a
change is a new version. `"private": true` in the manifest publishes the
version private.
