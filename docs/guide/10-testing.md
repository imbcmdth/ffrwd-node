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

**Rust**

```rust
use ffrwd_node::{Bound, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::Deserialize;

#[derive(Clone, Copy, Deserialize)]
struct Params {
    black: u8,
    white: u8,
}

struct Levels {
    v: u32,
    params: Params,
}

/// `value` with `black` moved to 0 and `white` to 255.
fn stretch(value: u8, params: Params) -> u8 {
    let (black, white) = (params.black as f64, params.white as f64);
    ((value as f64 - black) * 255.0 / (white - black))
        .round()
        .clamp(0.0, 255.0) as u8
}

impl Node for Levels {
    const NAME: &'static str = "levels";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"black":{"type":"integer","minimum":0,"maximum":254,"default":16},"white":{"type":"integer","minimum":1,"maximum":255,"default":235}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, _: &Bound) -> Result<Shape> {
        if params.black >= params.white {
            return Err("levels needs `black` under `white`".into());
        }
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .output(Output::like("v"))
            .pure()
            .one_to_one())
    }

    fn init(params: Params, init: &Init) -> Result<Levels> {
        Ok(Levels {
            v: init.stream("v")?.id,
            params,
        })
    }

    fn set_params(&mut self, params: Params) -> Result<()> {
        self.params = params;
        Ok(())
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        if (self.params.black, self.params.white) == (0, 255) {
            return Ok(out.pass("v", self.v, &frame)?);
        }
        let mut pixels = tick.fetch(self.v, frame.index);
        for pixel in pixels.as_chunks_mut::<4>().0 {
            for channel in &mut pixel[..3] {
                *channel = stretch(*channel, self.params);
            }
        }
        Ok(out.frame("v", frame.pts, frame.duration, pixels)?)
    }
}

ffrwd_node::export!(Levels);
```

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

**Rust**

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use ffrwd_node::mock::Harness;
    use ffrwd_node::{BoundStream, Payload, Rational};

    fn open(params: &str) -> Result<Harness<Levels>, String> {
        let v = BoundStream::video("v", 0, 2, 1, "rgba", Rational::new(1, 25));
        Harness::new(params, vec![v])
    }

    #[test]
    fn black_and_white_reach_the_ends() {
        let mut levels = open("").unwrap();
        let tick = levels
            .tick(0)
            .frame(0, 0, vec![16, 16, 16, 255, 235, 126, 235, 255]);
        let emitted = levels.process(&tick).unwrap();
        let [Payload::Frame { data, .. }] = emitted.on("v")[..] else {
            panic!("no frame: {emitted:?}")
        };
        assert_eq!(data, &[0, 0, 0, 255, 255, 128, 255, 255]);
    }

    #[test]
    fn the_full_range_passes_the_frame_on() {
        let mut levels = open(r#"{"black":0,"white":255}"#).unwrap();
        let emitted = levels
            .process(&levels.tick(0).frame(0, 0, vec![0; 8]))
            .unwrap();
        assert!(matches!(emitted.on("v")[..], [Payload::Same { .. }]));
    }

    #[test]
    fn params_change_between_ticks() {
        let mut levels = open("").unwrap();
        levels.set_params(r#"{"black":0,"white":255}"#).unwrap();
        let emitted = levels
            .process(&levels.tick(0).frame(0, 0, vec![0; 8]))
            .unwrap();
        assert!(matches!(emitted.on("v")[..], [Payload::Same { .. }]));
    }

    #[test]
    fn black_over_white_is_refused() {
        let err = open(r#"{"black":200,"white":100}"#).err().unwrap();
        assert!(err.contains("`black` under `white`"), "{err}");
        assert!(open(r#"{"black":-1}"#).is_err());
    }
}
```

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

A node that counts in frames or samples does turn on it. `level`, from
[chapter 5](05-window.md), refuses the names alone:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/level.wasm --bound a
ffrwd-wasm: asking target/wasm32-wasip2/release/level.wasm for its shape: level refused the shape: level counts its window in samples, and the call gives `a` no sample rate
```

`--params` gives a call's params, and a node's refusal comes back naming the
node:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/levels.wasm --params '{"black":200,"white":100}' --bound v
ffrwd-wasm: asking target/wasm32-wasip2/release/levels.wasm for its shape: levels refused the shape: levels needs `black` under `white`
```

The compiler asks the same question for every call, so the query hears the
refusal where the call is written:

```sql
CREATE FUNCTION levels(v video_stream, black number DEFAULT 16, white number DEFAULT 235)
RETURNS video_stream
  AS 'levels.wasm', 'levels' LANGUAGE wasm;

COPY (
  SELECT levels(f.video[1], 200, 100)
  FROM input('av.mp4') f
) TO 'levelled.mp4' WITH (video_codec 'libx264')
```

```
$ ffrwd compile -f refused.sql
error: line 6:10: UNSUPPORTED_SQL: levels(): the module 'levels.wasm' refused the shape for these params: ffrwd-wasm: asking levels.wasm for its shape: levels refused the shape: levels needs `black` under `white` (hint: check the arguments match what the module declares)
```

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

**Rust**

```toml
[package]
name = "levels"
version = "0.1.0"
edition = "2021"

[lib]
crate-type = ["cdylib"]

[dependencies]
ffrwd-node = { git = "https://github.com/imbcmdth/ffrwd-node", tag = "v0.2.0" }
ffrwd-frame = { git = "https://github.com/imbcmdth/ffrwd-frame", tag = "v0.1.1" }
serde = { version = "1", features = ["derive"] }

[profile.release]
opt-level = 3
lto = true
strip = true
```

The export names the built module by its path from the package's root:

```sql
-- Stretches the picture's levels so `black` becomes 0 and `white` 255.
CREATE FUNCTION levels(v video_stream, black number DEFAULT 16, white number DEFAULT 235)
RETURNS video_stream
  AS 'target/wasm32-wasip2/release/levels.wasm', 'levels' LANGUAGE wasm;
```

A recipe calls it by its full name, the package's two halves and the
export's:

```sql
-- Stretch a file's picture to full range, its audio carried through untouched.
-- variables: source (input media path), dest (output path)
-- example: ffrwd run levels -v source=in.mp4 -v dest=out.mp4
COPY (
  SELECT acme.levels.levels(f.video[1]), f.audio[1]
  FROM input(:'source') f
) TO :'dest'
```

`ffrwd run levels -v source=in.mp4 -v dest=out.mp4` runs it from inside the
package.

The manifest names the export and the recipe, depends on the interface the
module speaks, and says how to test the package:

```json
{
  "name": "acme/levels",
  "version": "0.1.0",
  "lib": {
    "levels": "src/levels.sql"
  },
  "bin": {
    "levels": "recipes/levels.sql"
  },
  "dependencies": {
    "ffrwd/wasm": "0.19.1"
  },
  "keywords": [
    "levels",
    "contrast"
  ],
  "capabilities": [],
  "test": "cargo test"
}
```

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
