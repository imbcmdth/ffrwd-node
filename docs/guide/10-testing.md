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

**C++**

```cpp
#include <algorithm>
#include <cmath>

#include "ffrwd/node.hpp"

struct Params {
    std::uint8_t black;
    std::uint8_t white;
    FFRWD_FIELDS(black, white)
};

/// `value` with `black` moved to 0 and `white` to 255.
std::uint8_t stretch(std::uint8_t value, Params params) {
    double black = params.black, white = params.white;
    return std::uint8_t(std::clamp(std::round((value - black) * 255.0 / (white - black)), 0.0, 255.0));
}

struct Levels : ffrwd::Node<Levels, Params> {
    static constexpr std::string_view name = "levels";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"black":{"type":"integer","minimum":0,"maximum":254,"default":16},"white":{"type":"integer","minimum":1,"maximum":255,"default":235}},"additionalProperties":false})";

    std::uint32_t v = 0;
    Params params;

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound&) {
        if (params.black >= params.white) return ffrwd::fail("levels needs `black` under `white`");
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .output(ffrwd::Output::like("v"))
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<Levels> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        Levels node;
        node.v = v.id;
        node.params = params;
        return node;
    }

    ffrwd::Status set_params(Params next) {
        params = next;
        return {};
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        if (params.black == 0 && params.white == 255) return out.pass("v", v, *frame);
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        for (std::size_t at = 0; at + 3 < pixels.size(); at += 4)
            for (std::size_t channel = at; channel < at + 3; ++channel)
                pixels[channel] = stretch(pixels[channel], params);
        return out.frame("v", frame->pts, frame->duration, std::move(pixels));
    }
};

FFRWD_EXPORT(Levels);
```

**JavaScript**

```js
import { defineNode, Input, Output, Shape } from '@ffrwd/node';

/** `value` with `black` moved to 0 and `white` to 255. */
function stretch(value, { black, white }) {
  return Math.min(Math.max(Math.round(((value - black) * 255) / (white - black)), 0), 255);
}

export const node = defineNode({
  name: 'levels',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"black":{"type":"integer","minimum":0,"maximum":254,"default":16},' +
    '"white":{"type":"integer","minimum":1,"maximum":255,"default":235}},"additionalProperties":false}',

  shape({ black, white }) {
    if (black >= white) throw new Error('levels needs `black` under `white`');
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .output(Output.like('v'))
      .pure()
      .oneToOne();
  },

  init(params, init) {
    const v = init.stream('v').id;
    return {
      setParams(changed) {
        params = changed;
      },
      process(tick, out) {
        const frame = tick.frame(v);
        if (frame === undefined) return;
        if (params.black === 0 && params.white === 255) return out.pass('v', v, frame);
        const pixels = tick.fetch(v, frame.index);
        for (let at = 0; at < pixels.length; at += 4) {
          for (let channel = at; channel < at + 3; channel += 1) pixels[channel] = stretch(pixels[channel], params);
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

import (
	"errors"
	"math"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Black uint8 `json:"black"`
	White uint8 `json:"white"`
}

type Levels struct {
	v      uint32
	params Params
}

// stretch is value with black moved to 0 and white to 255.
func stretch(value uint8, params Params) uint8 {
	black, white := float64(params.Black), float64(params.White)
	return uint8(min(max(math.Round((float64(value)-black)*255/(white-black)), 0), 255))
}

var Definition = node.Definition[Params]{
	Name:         "levels",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"black":{"type":"integer","minimum":0,"maximum":254,"default":16},"white":{"type":"integer","minimum":1,"maximum":255,"default":235}},"additionalProperties":false}`,
	Shape: func(params Params, _ *node.Bound) (node.Shape, error) {
		if params.Black >= params.White {
			return node.Shape{}, errors.New("levels needs `black` under `white`")
		}
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Output(node.LikeOutput("v")).
			Pure().
			OneToOne(), nil
	},
	Init: func(params Params, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		return &Levels{v: v.ID, params: params}, nil
	},
}

func (l *Levels) SetParams(params Params) error {
	l.params = params
	return nil
}

func (l *Levels) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(l.v)
	if !ok {
		return nil
	}
	if l.params == (Params{Black: 0, White: 255}) {
		return out.Pass("v", l.v, frame)
	}
	pixels := tick.Fetch(l.v, frame.Index)
	for at := 0; at+3 < len(pixels); at += 4 {
		for channel := at; channel < at+3; channel++ {
			pixels[channel] = stretch(pixels[channel], l.params)
		}
	}
	return out.Frame("v", frame.Pts, frame.Duration, pixels)
}

func init() { node.Export(Definition) }

func main() {}
```

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

**C++**

```cpp
#include "levels.cpp"

#include <variant>
#include <vector>

#include "check.hpp"
#include "ffrwd/mock.hpp"

ffrwd::Result<ffrwd::mock::Harness<Levels>> open(std::string_view params) {
    auto v = ffrwd::BoundStream::video("v", 0, 2, 1, "rgba", ffrwd::Rational(1, 25));
    return ffrwd::mock::Harness<Levels>::open(params, {v});
}

TEST(black_and_white_reach_the_ends) {
    auto levels = CHECK_OK(open(""));
    std::vector<std::uint8_t> pixels{16, 16, 16, 255, 235, 126, 235, 255};
    auto tick = levels.tick(0).frame(0, 0, ffrwd::Bytes(pixels));
    auto emitted = CHECK_OK(levels.process(tick));
    auto on = emitted.on("v");
    CHECK_EQ(on.size(), 1u);
    const auto* frame = std::get_if<ffrwd::FramePayload>(on.at(0));
    CHECK(frame && frame->data.vector() == (std::vector<std::uint8_t>{0, 0, 0, 255, 255, 128, 255, 255}));
}

TEST(the_full_range_passes_the_frame_on) {
    auto levels = CHECK_OK(open(R"({"black":0,"white":255})"));
    auto emitted = CHECK_OK(levels.process(levels.tick(0).frame(0, 0, ffrwd::Bytes(8))));
    auto on = emitted.on("v");
    CHECK(on.size() == 1 && std::holds_alternative<ffrwd::SamePayload>(*on[0]));
}

TEST(params_change_between_ticks) {
    auto levels = CHECK_OK(open(""));
    CHECK_OK(levels.set_params(R"({"black":0,"white":255})"));
    auto emitted = CHECK_OK(levels.process(levels.tick(0).frame(0, 0, ffrwd::Bytes(8))));
    auto on = emitted.on("v");
    CHECK(on.size() == 1 && std::holds_alternative<ffrwd::SamePayload>(*on[0]));
}

TEST(black_over_white_is_refused) {
    std::string error = CHECK_ERR(open(R"({"black":200,"white":100})"));
    CHECK_HAS(error, "`black` under `white`");
    CHECK(!open(R"({"black":-1})"));
}
```

**JavaScript**

```js
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { BoundStream, Rational } from '@ffrwd/node';
import { Harness } from '@ffrwd/node/mock';

import { node } from '../src/levels.js';

function open(params) {
  const v = BoundStream.video('v', 0, 2, 1, 'rgba', new Rational(1, 25));
  return new Harness(node, params, [v]);
}

test('black and white reach the ends', () => {
  const levels = open('');
  const tick = levels.tick(0).frame(0, 0, Uint8Array.of(16, 16, 16, 255, 235, 126, 235, 255));
  const emitted = levels.process(tick);
  const [payload] = emitted.on('v');
  assert.equal(payload.tag, 'frame');
  assert.deepEqual([...payload.val.data], [0, 0, 0, 255, 255, 128, 255, 255]);
});

test('the full range passes the frame on', () => {
  const levels = open('{"black":0,"white":255}');
  const emitted = levels.process(levels.tick(0).frame(0, 0, new Uint8Array(8)));
  assert.deepEqual(emitted.on('v').map((payload) => payload.tag), ['same']);
});

test('params change between ticks', () => {
  const levels = open('');
  levels.setParams('{"black":0,"white":255}');
  const emitted = levels.process(levels.tick(0).frame(0, 0, new Uint8Array(8)));
  assert.deepEqual(emitted.on('v').map((payload) => payload.tag), ['same']);
});

test('black over white is refused', () => {
  assert.throws(() => open('{"black":200,"white":100}'), /`black` under `white`/);
  assert.throws(() => open('{"black":-1}'));
});
```

**Go**

```go
package main

import (
	"reflect"
	"strings"
	"testing"

	node "github.com/imbcmdth/ffrwd-node/go"
	"github.com/imbcmdth/ffrwd-node/go/mock"
)

func open(params string) (*mock.Harness[Params], error) {
	v := node.VideoStream("v", 0, 2, 1, "rgba", node.R(1, 25))
	return mock.Open(Definition, params, v)
}

func TestBlackAndWhiteReachTheEnds(t *testing.T) {
	levels, err := open("")
	if err != nil {
		t.Fatal(err)
	}
	tick := levels.Tick(0).
		WithFrame(0, 0, []byte{16, 16, 16, 255, 235, 126, 235, 255})
	emitted, err := levels.Process(tick)
	if err != nil {
		t.Fatal(err)
	}
	payloads := emitted.On("v")
	if len(payloads) != 1 || payloads[0].Kind != node.PayloadFrame {
		t.Fatalf("no frame: %+v", emitted)
	}
	if want := []byte{0, 0, 0, 255, 255, 128, 255, 255}; !reflect.DeepEqual(payloads[0].Data, want) {
		t.Fatalf("%v", payloads[0].Data)
	}
}

func TestTheFullRangePassesTheFrameOn(t *testing.T) {
	levels, err := open(`{"black":0,"white":255}`)
	if err != nil {
		t.Fatal(err)
	}
	emitted, err := levels.Process(levels.Tick(0).WithFrame(0, 0, make([]byte, 8)))
	if err != nil {
		t.Fatal(err)
	}
	if payloads := emitted.On("v"); len(payloads) != 1 || payloads[0].Kind != node.PayloadSame {
		t.Fatalf("%+v", emitted)
	}
}

func TestParamsChangeBetweenTicks(t *testing.T) {
	levels, err := open("")
	if err != nil {
		t.Fatal(err)
	}
	if err := levels.SetParams(`{"black":0,"white":255}`); err != nil {
		t.Fatal(err)
	}
	emitted, err := levels.Process(levels.Tick(0).WithFrame(0, 0, make([]byte, 8)))
	if err != nil {
		t.Fatal(err)
	}
	if payloads := emitted.On("v"); len(payloads) != 1 || payloads[0].Kind != node.PayloadSame {
		t.Fatalf("%+v", emitted)
	}
}

func TestBlackOverWhiteIsRefused(t *testing.T) {
	_, err := open(`{"black":200,"white":100}`)
	if err == nil || !strings.Contains(err.Error(), "`black` under `white`") {
		t.Fatalf("%v", err)
	}
	if _, err := open(`{"black":-1}`); err == nil {
		t.Fatal("a black of -1 was taken")
	}
}
```

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

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/levels.wasm --bound v
```

**C++**

```
$ ffrwd-wasm --shape build/levels.wasm --bound v
```

**JavaScript**

```
$ ffrwd-wasm --shape build/levels.wasm --bound v
```

**Go**

```
$ ffrwd-wasm --shape build/levels.wasm --bound v
```

In the second form, the list that the compiler passes gives every stream its
rate:

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/levels.wasm --bound '[{"input":"v","streams":[{"rate":{"num":25,"den":1}}]}]'
```

**C++**

```
$ ffrwd-wasm --shape build/levels.wasm --bound '[{"input":"v","streams":[{"rate":{"num":25,"den":1}}]}]'
```

**JavaScript**

```
$ ffrwd-wasm --shape build/levels.wasm --bound '[{"input":"v","streams":[{"rate":{"num":25,"den":1}}]}]'
```

**Go**

```
$ ffrwd-wasm --shape build/levels.wasm --bound '[{"input":"v","streams":[{"rate":{"num":25,"den":1}}]}]'
```

The shape of `levels` does not depend on the rate, so both forms print the
same shape:

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

The shape of a node that counts in frames or samples does depend on the
rate. `level`, from [chapter 5](05-window.md), refuses names alone:

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/level.wasm --bound a
ffrwd-wasm: asking target/wasm32-wasip2/release/level.wasm for its shape: level refused the shape: level counts its window in samples, and the call gives `a` no sample rate
```

**C++**

```
$ ffrwd-wasm --shape build/level.wasm --bound a
ffrwd-wasm: asking build/level.wasm for its shape: level refused the shape: level counts its window in samples, and the call gives `a` no sample rate
```

**JavaScript**

```
$ ffrwd-wasm --shape build/level.wasm --bound a
ffrwd-wasm: asking build/level.wasm for its shape: level refused the shape: level counts its window in samples, and the call gives `a` no sample rate
```

**Go**

```
$ ffrwd-wasm --shape build/level.wasm --bound a
ffrwd-wasm: asking build/level.wasm for its shape: level refused the shape: level counts its window in samples, and the call gives `a` no sample rate
```

`--params` gives the params of a call. When the node refuses the call, the
refusal names the node:

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/levels.wasm --params '{"black":200,"white":100}' --bound v
ffrwd-wasm: asking target/wasm32-wasip2/release/levels.wasm for its shape: levels refused the shape: levels needs `black` under `white`
```

**C++**

```
$ ffrwd-wasm --shape build/levels.wasm --params '{"black":200,"white":100}' --bound v
ffrwd-wasm: asking build/levels.wasm for its shape: levels refused the shape: levels needs `black` under `white`
```

**JavaScript**

```
$ ffrwd-wasm --shape build/levels.wasm --params '{"black":200,"white":100}' --bound v
ffrwd-wasm: asking build/levels.wasm for its shape: levels refused the shape: levels needs `black` under `white`
```

**Go**

```
$ ffrwd-wasm --shape build/levels.wasm --params '{"black":200,"white":100}' --bound v
ffrwd-wasm: asking build/levels.wasm for its shape: levels refused the shape: levels needs `black` under `white`
```

The compiler asks for the shape of every call in the same way. So the
compiler reports the refusal at the place in the query where the call is
written:

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

**C++**

```sh
#!/bin/sh
# Builds build/levels.wasm: the node compiled with wasi-sdk and linked with
# the ffrwd-node C++ library, which `sh build.sh modules` in the SDK's cpp/
# directory builds.
#
#   sh build.sh         build/levels.wasm
#   sh build.sh test    builds the tests for this machine and runs them
#
#   FFRWD_NODE        the SDK's cpp/ directory
#   FFRWD_NODE_BUILD  where its library was built, $FFRWD_NODE/build unless set
#   WASI_SDK          wasi-sdk 34 or newer
#   CXX               a C++23 compiler for this machine, for the tests
set -eu
cd "$(dirname "$0")"
sdk=${FFRWD_NODE:-../../../../../cpp}
lib=${FFRWD_NODE_BUILD:-$sdk/build}
wasi=${WASI_SDK:?set WASI_SDK to your wasi-sdk}
cxx="$wasi/bin/clang++ --target=wasm32-wasip2 -std=c++23 -fno-exceptions -fno-rtti"
mkdir -p build

if [ "${1:-}" = test ]; then
    ${CXX:-clang++} -std=c++23 -O1 -I"$sdk/include" -I"$sdk/tests" -o build/levels_test \
        $(ls "$sdk"/src/*.cpp | grep -v glue.cpp) "$sdk/tests/main.cpp" src/levels_test.cpp
    exec build/levels_test
fi

$cxx -O2 -I"$sdk/include" -c src/levels.cpp -o build/levels.o
$cxx -mexec-model=reactor -Wl,--gc-sections -Wl,--strip-all -o build/levels.wasm build/levels.o \
    "$lib/wasm/libffrwd-node.a" "$lib/wasm/node_module.o" "$lib/gen/node_module_component_type.o"
```

**JavaScript**

The package has a `build.js` like the one in chapter 1, beside this
`package.json`:

```json
{
  "name": "levels",
  "version": "0.1.0",
  "private": true,
  "type": "module",
  "scripts": {
    "build": "node build.js",
    "test": "node --test"
  },
  "dependencies": {
    "@ffrwd/node": "^0.2.0"
  },
  "devDependencies": {
    "@bytecodealliance/componentize-js": "0.22.0",
    "esbuild": "^0.25.0"
  }
}
```

**Go**

The package has a `build.sh` that runs the `componentize-go` command from
chapter 1, beside this `go.mod`:

```
module levels

go 1.25

require github.com/imbcmdth/ffrwd-node/go v0.2.0

require go.bytecodealliance.org/pkg v0.2.2 // indirect
```

The export names the built module by the module's path from the root of the
package:

**Rust**

```sql
-- Stretches the picture's levels so `black` becomes 0 and `white` 255.
CREATE FUNCTION levels(v video_stream, black number DEFAULT 16, white number DEFAULT 235)
RETURNS video_stream
  AS 'target/wasm32-wasip2/release/levels.wasm', 'levels' LANGUAGE wasm;
```

**C++**

```sql
-- Stretches the picture's levels so `black` becomes 0 and `white` 255.
CREATE FUNCTION levels(v video_stream, black number DEFAULT 16, white number DEFAULT 235)
RETURNS video_stream
  AS 'build/levels.wasm', 'levels' LANGUAGE wasm;
```

**JavaScript**

```sql
-- Stretches the picture's levels so `black` becomes 0 and `white` 255.
CREATE FUNCTION levels(v video_stream, black number DEFAULT 16, white number DEFAULT 235)
RETURNS video_stream
  AS 'build/levels.wasm', 'levels' LANGUAGE wasm;
```

**Go**

```sql
-- Stretches the picture's levels so `black` becomes 0 and `white` 255.
CREATE FUNCTION levels(v video_stream, black number DEFAULT 16, white number DEFAULT 235)
RETURNS video_stream
  AS 'build/levels.wasm', 'levels' LANGUAGE wasm;
```

A recipe calls the export by its full name, which is the two halves of the
package's name followed by the export's name:

```sql
-- Stretch a file's picture to full range, its audio carried through untouched.
-- variables: source (input media path), dest (output path)
-- example: ffrwd run levels -v source=in.mp4 -v dest=out.mp4
COPY (
  SELECT acme.levels.levels(f.video[1]), f.audio[1]
  FROM input(:'source') f
) TO :'dest'
```

Run from inside the package, `ffrwd run levels -v source=in.mp4 -v
dest=out.mp4` runs the recipe.

The manifest names the export and the recipe, declares a dependency on the
interface that the module uses to talk to the host, and says how to test the
package:

**Rust**

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

**C++**

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
  "test": "sh build.sh test"
}
```

**JavaScript**

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
  "test": "npm test"
}
```

**Go**

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
  "test": "go test ./..."
}
```

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
