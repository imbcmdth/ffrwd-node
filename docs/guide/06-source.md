# 6. A source

A source is a node that has no inputs and makes streams. A source might make
a test pattern, a page rendered from HTML, or a broadcast pulled from a
relay. A query reads a source in its `FROM` clause, in the same way the
query reads a file. This chapter builds `bars`, which draws colour bars at a
fixed rate, and `beat`, which makes a picture whenever it has one ready.

## Its own clock

A node's clock decides when the host, which is the program that runs the
node, calls the node. Each call is one tick. A node that has no input to
serve as its clock keeps time itself, in one of two ways.

- **A rate.** The node ticks a fixed number of times a second. The pts of a
  tick is the tick's number, counted from 0, in a time base of one over the
  rate. In a live run, the host paces the ticks to the wall clock. In any
  other run, the ticks come as fast as the node's outputs are drained.
- **Self-clocked.** The node emits whenever it has something to emit, and
  the pts on the node's outputs are the timeline. The pts of a tick is the
  number of microseconds since the node's first call. The node's call may
  wait until there is something to emit, and the host calls the node again
  as soon as the call returns. A source that reads from the network is
  self-clocked.

A source's output has no input to take its format from, so the output states
a format of its own: a size and a pixel format for video, or a sample rate,
a channel count and a sample format for sound.

## Bounded, and finished

A source declares whether it ends by itself. A source that ends by itself is
bounded. A source that does not end by itself is live. For a live source,
the compiler plans the whole query as live, and something other than the
source has to end the run. A bounded source signals its end with its last
emission. The node marks the result of that tick as finished, the host makes
the node's last call, and every output of the node ends.

## Relation rows

A source read in `FROM` is a table with one row per rendition, in the way
that a streaming manifest lists a ladder of renditions. These rows are
called relation rows. The node's shape, which lists its ports and its clock,
lists the relation rows as JSON objects. Each output names the relation row
it belongs to. A query picks a rendition by the fields of the rendition's
row, for example with `WHERE s.height = 720`.

## bars

`bars` ticks at the rate that its `fps` param gives. When the call sets
`seconds`, `bars` is bounded, and `bars` finishes on the first tick at or
past that many seconds. When the call does not set `seconds`, `bars` runs
until whatever reads its output stops.

**Rust**

```rust
use ffrwd_node::{Bound, Init, Node, Out, Output, Rational, Result, Shape, Tick};
use serde::Deserialize;

const COLOURS: [[u8; 4]; 7] = [
    [192, 192, 192, 255],
    [192, 192, 0, 255],
    [0, 192, 192, 255],
    [0, 192, 0, 255],
    [192, 0, 192, 255],
    [192, 0, 0, 255],
    [0, 0, 192, 255],
];

#[derive(Deserialize)]
struct Params {
    width: u32,
    height: u32,
    fps: f64,
    seconds: Option<f64>,
}

struct Bars {
    width: usize,
    height: usize,
    seconds: Option<f64>,
}

impl Bars {
    /// Seven bars, and a white line crossing them once a second.
    fn draw(&self, t: f64) -> Vec<u8> {
        let line = (t.fract() * self.width as f64) as usize;
        let mut canvas = Vec::with_capacity(self.width * self.height * 4);
        for _ in 0..self.height {
            for x in 0..self.width {
                let colour = if x == line {
                    [255; 4]
                } else {
                    COLOURS[x * COLOURS.len() / self.width]
                };
                canvas.extend_from_slice(&colour);
            }
        }
        canvas
    }
}

impl Node for Bars {
    const NAME: &'static str = "bars";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"width":{"type":"integer","minimum":16,"maximum":8192,"default":1280},"height":{"type":"integer","minimum":16,"maximum":8192,"default":720},"fps":{"type":"number","exclusiveMinimum":0,"maximum":240,"default":30},"seconds":{"type":["number","null"],"exclusiveMinimum":0}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, _: &Bound) -> Result<Shape> {
        let (width, height) = (params.width, params.height);
        Ok(Shape::new()
            .rate(Rational::approximate(params.fps, 1001))
            .output(
                Output::video("video")
                    .size(width, height)
                    .pixel_format("rgba")
                    .row(0),
            )
            .relation_row(&format!(r#"{{"width":{width},"height":{height}}}"#))
            .bounded(params.seconds.is_some())
            .pure())
    }

    fn init(params: Params, _: &Init) -> Result<Bars> {
        Ok(Bars {
            width: params.width as usize,
            height: params.height as usize,
            seconds: params.seconds,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        if self
            .seconds
            .is_some_and(|seconds| tick.seconds() >= seconds)
        {
            out.finish();
            return Ok(());
        }
        let canvas = self.draw(tick.seconds());
        Ok(out.frame("video", tick.pts(), Some(1), canvas)?)
    }
}

ffrwd_node::export!(Bars);
```

**C++**

```cpp
#include <array>
#include <cmath>
#include <optional>
#include <string>

#include "ffrwd/node.hpp"

constexpr std::array<std::array<std::uint8_t, 4>, 7> COLOURS{{
    {192, 192, 192, 255},
    {192, 192, 0, 255},
    {0, 192, 192, 255},
    {0, 192, 0, 255},
    {192, 0, 192, 255},
    {192, 0, 0, 255},
    {0, 0, 192, 255},
}};

struct Params {
    std::uint32_t width;
    std::uint32_t height;
    double fps;
    std::optional<double> seconds;
    FFRWD_FIELDS(width, height, fps, seconds)
};

struct Bars : ffrwd::Node<Bars, Params> {
    static constexpr std::string_view name = "bars";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"width":{"type":"integer","minimum":16,"maximum":8192,"default":1280},"height":{"type":"integer","minimum":16,"maximum":8192,"default":720},"fps":{"type":"number","exclusiveMinimum":0,"maximum":240,"default":30},"seconds":{"type":["number","null"],"exclusiveMinimum":0}},"additionalProperties":false})";

    std::size_t width = 0;
    std::size_t height = 0;
    std::optional<double> seconds;

    /// Seven bars, and a white line crossing them once a second.
    ffrwd::Bytes draw(double t) const {
        auto line = std::size_t((t - std::trunc(t)) * double(width));
        ffrwd::Bytes canvas(width * height * 4);
        for (std::size_t y = 0; y < height; ++y)
            for (std::size_t x = 0; x < width; ++x) {
                std::array<std::uint8_t, 4> colour{255, 255, 255, 255};
                if (x != line) colour = COLOURS[x * COLOURS.size() / width];
                std::copy(colour.begin(), colour.end(), canvas.data() + (y * width + x) * 4);
            }
        return canvas;
    }

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound&) {
        auto [width, height] = std::pair(params.width, params.height);
        return ffrwd::Shape()
            .rate(ffrwd::Rational::approximate(params.fps, 1001))
            .output(ffrwd::Output::video("video").size(width, height).pixel_format("rgba").row(0))
            .relation_row(R"({"width":)" + std::to_string(width) + R"(,"height":)" + std::to_string(height) +
                          "}")
            .bounded(params.seconds.has_value())
            .pure();
    }

    static ffrwd::Result<Bars> init(Params params, const ffrwd::Init&) {
        Bars node;
        node.width = params.width;
        node.height = params.height;
        node.seconds = params.seconds;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        if (seconds && tick.seconds() >= *seconds) {
            out.finish();
            return {};
        }
        return out.frame("video", tick.pts(), 1, draw(tick.seconds()));
    }
};

FFRWD_EXPORT(Bars);
```

**JavaScript**

```js
import { defineNode, Output, Rational, Shape } from '@ffrwd/node';

const COLOURS = [
  [192, 192, 192, 255],
  [192, 192, 0, 255],
  [0, 192, 192, 255],
  [0, 192, 0, 255],
  [192, 0, 192, 255],
  [192, 0, 0, 255],
  [0, 0, 192, 255],
];

/** Seven bars, and a white line crossing them once a second. */
function draw(width, height, t) {
  const line = Math.trunc((t % 1) * width);
  const row = new Uint8Array(width * 4);
  for (let x = 0; x < width; x += 1) {
    row.set(x === line ? [255, 255, 255, 255] : COLOURS[Math.floor((x * COLOURS.length) / width)], x * 4);
  }
  const canvas = new Uint8Array(row.length * height);
  for (let y = 0; y < height; y += 1) canvas.set(row, y * row.length);
  return canvas;
}

export const node = defineNode({
  name: 'bars',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"width":{"type":"integer","minimum":16,"maximum":8192,"default":1280},' +
    '"height":{"type":"integer","minimum":16,"maximum":8192,"default":720},' +
    '"fps":{"type":"number","exclusiveMinimum":0,"maximum":240,"default":30},' +
    '"seconds":{"type":["number","null"],"exclusiveMinimum":0}},"additionalProperties":false}',

  shape({ width, height, fps, seconds }) {
    return new Shape()
      .rate(Rational.approximate(fps, 1001))
      .output(Output.video('video').size(width, height).pixelFormat('rgba').row(0))
      .relationRow(JSON.stringify({ width, height }))
      .bounded(seconds !== undefined)
      .pure();
  },

  init({ width, height, seconds }) {
    return {
      process(tick, out) {
        if (seconds !== undefined && tick.seconds() >= seconds) {
          out.finish();
          return;
        }
        out.frame('video', tick.pts(), 1, draw(width, height, tick.seconds()));
      },
    };
  },
});
```

**Go**

```go
package main

import (
	"fmt"
	"math"

	node "github.com/imbcmdth/ffrwd-node/go"
)

var colours = [7][4]byte{
	{192, 192, 192, 255},
	{192, 192, 0, 255},
	{0, 192, 192, 255},
	{0, 192, 0, 255},
	{192, 0, 192, 255},
	{192, 0, 0, 255},
	{0, 0, 192, 255},
}

type Params struct {
	Width   uint32   `json:"width"`
	Height  uint32   `json:"height"`
	Fps     float64  `json:"fps"`
	Seconds *float64 `json:"seconds"`
}

type Bars struct {
	width   int
	height  int
	seconds *float64
}

// draw is seven bars, and a white line crossing them once a second.
func (b *Bars) draw(t float64) []byte {
	line := int((t - math.Trunc(t)) * float64(b.width))
	canvas := make([]byte, 0, b.width*b.height*4)
	for range b.height {
		for x := range b.width {
			colour := [4]byte{255, 255, 255, 255}
			if x != line {
				colour = colours[x*len(colours)/b.width]
			}
			canvas = append(canvas, colour[:]...)
		}
	}
	return canvas
}

var Definition = node.Definition[Params]{
	Name:         "bars",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"width":{"type":"integer","minimum":16,"maximum":8192,"default":1280},"height":{"type":"integer","minimum":16,"maximum":8192,"default":720},"fps":{"type":"number","exclusiveMinimum":0,"maximum":240,"default":30},"seconds":{"type":["number","null"],"exclusiveMinimum":0}},"additionalProperties":false}`,
	Shape: func(params Params, _ *node.Bound) (node.Shape, error) {
		width, height := params.Width, params.Height
		return node.NewShape().
			Rate(node.Approximate(params.Fps, 1001)).
			Output(node.VideoOutput("video").
				Size(width, height).
				PixelFormat("rgba").
				Row(0)).
			RelationRow(fmt.Sprintf(`{"width":%d,"height":%d}`, width, height)).
			Bounded(params.Seconds != nil).
			Pure(), nil
	},
	Init: func(params Params, _ *node.Init) (node.Instance, error) {
		return &Bars{width: int(params.Width), height: int(params.Height), seconds: params.Seconds}, nil
	},
}

func (b *Bars) Process(tick *node.Tick, out *node.Out) error {
	if b.seconds != nil && tick.Seconds() >= *b.seconds {
		out.Finish()
		return nil
	}
	canvas := b.draw(tick.Seconds())
	one := int64(1)
	return out.Frame("video", tick.Pts(), &one, canvas)
}

func init() { node.Export(Definition) }

func main() {}
```

The whole shape of `bars` when the call makes it bounded:

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/bars.wasm --params '{"width":640,"height":360,"seconds":5}'
```

**C++**

```
$ ffrwd-wasm --shape build/bars.wasm --params '{"width":640,"height":360,"seconds":5}'
```

**JavaScript**

```
$ ffrwd-wasm --shape build/bars.wasm --params '{"width":640,"height":360,"seconds":5}'
```

**Go**

```
$ ffrwd-wasm --shape build/bars.wasm --params '{"width":640,"height":360,"seconds":5}'
```

```json
{
  "bounded": true,
  "clock": {"den": 1, "kind": "rate", "num": 30},
  "inputs": [],
  "one_to_one": false,
  "outputs": [
    {
      "format": {"color": null, "height": 360, "kind": "video", "pix_fmt": "rgba", "width": 640},
      "kind": "video",
      "latency": 0.0,
      "name": "video",
      "row": 0,
      "schema": null,
      "time_base": null
    }
  ],
  "pure": true,
  "relation": ["{\"width\":640,\"height\":360}"]
}
```

The declaration gives `bars` the return type `source`, so the query calls
`bars` in `FROM`. The alias in the `FROM` clause carries the stream columns
that `bars` makes:

```sql
CREATE FUNCTION bars(width number DEFAULT 1280, height number DEFAULT 720,
                     fps number DEFAULT 30, seconds number DEFAULT NULL)
RETURNS source
  AS 'bars.wasm', 'bars' LANGUAGE wasm;

COPY (
  SELECT s.video[1]
  FROM bars(640, 360, seconds => 5) s
) TO 'bars.mp4' WITH (video_codec 'libx264')
```

```
$ ffrwd compile -f bars.sql
ffrwd-wasm -m bars=bars.wasm -filter_complex \
  'bars=width=640:height=360:fps=30:seconds=5[video=out0]' -bound 'bars=[]' -map \
  '[out0]' -f nut pipe:1 | ffmpeg -copyts -f nut -analyzeduration 0 -fpsprobesize 3 -i \
  pipe:0 -map 0:v:0 -c:0 libx264 bars.mp4
```

A source binds no inputs, so its `-bound` list is empty.

## beat

`beat` makes a frame every `every` seconds by the wall clock. Each frame is
a shade of grey that depends on the wall-clock second in which the frame was
made. `beat` sleeps until the next frame is due. `beat` stamps the frame
with the time at which the frame was due, in microseconds since the node's
first call.

**Rust**

```rust
use std::thread::sleep;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use ffrwd_node::{Bound, Init, Node, Out, Output, Result, Shape, Tick};
use serde::Deserialize;

#[derive(Deserialize)]
struct Params {
    every: f64,
    width: u32,
    height: u32,
}

struct Beat {
    every: i64,
    next: i64,
    pixels: usize,
}

impl Node for Beat {
    const NAME: &'static str = "beat";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"every":{"type":"number","minimum":0.01,"maximum":3600,"default":1},"width":{"type":"integer","minimum":16,"maximum":8192,"default":320},"height":{"type":"integer","minimum":16,"maximum":8192,"default":240}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, _: &Bound) -> Result<Shape> {
        let (width, height) = (params.width, params.height);
        Ok(Shape::new()
            .self_clocked()
            .output(
                Output::video("video")
                    .size(width, height)
                    .pixel_format("rgba")
                    .row(0),
            )
            .relation_row(&format!(r#"{{"width":{width},"height":{height}}}"#))
            .bounded(false))
    }

    fn init(params: Params, _: &Init) -> Result<Beat> {
        Ok(Beat {
            every: (params.every * 1e6).round() as i64,
            next: 0,
            pixels: (params.width * params.height) as usize,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let now = tick.pts();
        if now < self.next {
            sleep(Duration::from_micros((self.next - now) as u64));
        }
        let wall = SystemTime::now().duration_since(UNIX_EPOCH)?.as_secs();
        let grey = (wall % 8 * 32) as u8;
        let frame = [grey, grey, grey, 255].repeat(self.pixels);
        out.frame("video", self.next, Some(self.every), frame)?;
        self.next += self.every;
        Ok(())
    }
}

ffrwd_node::export!(Beat);
```

**C++**

```cpp
#include <chrono>
#include <cmath>
#include <string>
#include <thread>

#include "ffrwd/node.hpp"

struct Params {
    double every;
    std::uint32_t width;
    std::uint32_t height;
    FFRWD_FIELDS(every, width, height)
};

struct Beat : ffrwd::Node<Beat, Params> {
    static constexpr std::string_view name = "beat";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"every":{"type":"number","minimum":0.01,"maximum":3600,"default":1},"width":{"type":"integer","minimum":16,"maximum":8192,"default":320},"height":{"type":"integer","minimum":16,"maximum":8192,"default":240}},"additionalProperties":false})";

    std::int64_t every = 0;
    std::int64_t next = 0;
    std::size_t pixels = 0;

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound&) {
        auto [width, height] = std::pair(params.width, params.height);
        return ffrwd::Shape()
            .self_clocked()
            .output(ffrwd::Output::video("video").size(width, height).pixel_format("rgba").row(0))
            .relation_row(R"({"width":)" + std::to_string(width) + R"(,"height":)" + std::to_string(height) +
                          "}")
            .bounded(false);
    }

    static ffrwd::Result<Beat> init(Params params, const ffrwd::Init&) {
        Beat node;
        node.every = std::int64_t(std::round(params.every * 1e6));
        node.next = 0;
        node.pixels = std::size_t(params.width) * params.height;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        std::int64_t now = tick.pts();
        if (now < next) std::this_thread::sleep_for(std::chrono::microseconds(next - now));
        auto wall = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch());
        auto grey = std::uint8_t(wall.count() % 8 * 32);
        ffrwd::Bytes frame(pixels * 4);
        for (std::size_t at = 0; at < pixels; ++at) {
            frame[at * 4] = frame[at * 4 + 1] = frame[at * 4 + 2] = grey;
            frame[at * 4 + 3] = 255;
        }
        FFRWD_TRY(out.frame("video", next, every, std::move(frame)));
        next += every;
        return {};
    }
};

FFRWD_EXPORT(Beat);
```

**JavaScript**

`buildNode`, the SDK's build function, turns off the clocks that WASI
offers. `beat` reads the wall clock and waits on it, so the `build.js` of
`beat` keeps the clocks on.

```js
import { defineNode, Output, Shape } from '@ffrwd/node';

/** Waits `micros` microseconds. A JavaScript call has nothing that blocks,
 * so it watches the clock until the time has passed. */
function sleep(micros) {
  const until = performance.now() + micros / 1000;
  while (performance.now() < until) continue;
}

export const node = defineNode({
  name: 'beat',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"every":{"type":"number","minimum":0.01,"maximum":3600,"default":1},' +
    '"width":{"type":"integer","minimum":16,"maximum":8192,"default":320},' +
    '"height":{"type":"integer","minimum":16,"maximum":8192,"default":240}},"additionalProperties":false}',

  shape({ width, height }) {
    return new Shape()
      .selfClocked()
      .output(Output.video('video').size(width, height).pixelFormat('rgba').row(0))
      .relationRow(JSON.stringify({ width, height }))
      .bounded(false);
  },

  init(params) {
    const every = Math.round(params.every * 1e6);
    const pixels = params.width * params.height;
    let next = 0;
    return {
      process(tick, out) {
        const now = tick.pts();
        if (now < next) sleep(next - now);
        const wall = Math.floor(Date.now() / 1000);
        const grey = (wall % 8) * 32;
        const frame = new Uint8Array(pixels * 4).fill(grey);
        for (let at = 3; at < frame.length; at += 4) frame[at] = 255;
        out.frame('video', next, every, frame);
        next += every;
      },
    };
  },
});
```

**Go**

```go
package main

import (
	"bytes"
	"fmt"
	"math"
	"time"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Every  float64 `json:"every"`
	Width  uint32  `json:"width"`
	Height uint32  `json:"height"`
}

type Beat struct {
	every  int64
	next   int64
	pixels int
}

var Definition = node.Definition[Params]{
	Name:         "beat",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"every":{"type":"number","minimum":0.01,"maximum":3600,"default":1},"width":{"type":"integer","minimum":16,"maximum":8192,"default":320},"height":{"type":"integer","minimum":16,"maximum":8192,"default":240}},"additionalProperties":false}`,
	Shape: func(params Params, _ *node.Bound) (node.Shape, error) {
		width, height := params.Width, params.Height
		return node.NewShape().
			SelfClocked().
			Output(node.VideoOutput("video").
				Size(width, height).
				PixelFormat("rgba").
				Row(0)).
			RelationRow(fmt.Sprintf(`{"width":%d,"height":%d}`, width, height)).
			Bounded(false), nil
	},
	Init: func(params Params, _ *node.Init) (node.Instance, error) {
		return &Beat{
			every:  int64(math.Round(params.Every * 1e6)),
			next:   0,
			pixels: int(params.Width * params.Height),
		}, nil
	},
}

func (b *Beat) Process(tick *node.Tick, out *node.Out) error {
	now := tick.Pts()
	if now < b.next {
		time.Sleep(time.Duration(b.next-now) * time.Microsecond)
	}
	wall := time.Now().Unix()
	grey := byte(wall % 8 * 32)
	frame := bytes.Repeat([]byte{grey, grey, grey, 255}, b.pixels)
	every := b.every
	if err := out.Frame("video", b.next, &every, frame); err != nil {
		return err
	}
	b.next += b.every
	return nil
}

func init() { node.Export(Definition) }

func main() {}
```

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/beat.wasm --params '{"every":0.5}'
```

**C++**

```
$ ffrwd-wasm --shape build/beat.wasm --params '{"every":0.5}'
```

**JavaScript**

```
$ ffrwd-wasm --shape build/beat.wasm --params '{"every":0.5}'
```

**Go**

```
$ ffrwd-wasm --shape build/beat.wasm --params '{"every":0.5}'
```

```json
{"kind": "self_clocked"}
```

`beat` never ends by itself, so a query that reads `beat` has to say where
to stop:

```sql
CREATE FUNCTION beat(every number DEFAULT 1, width number DEFAULT 320,
                     height number DEFAULT 240)
RETURNS source
  AS 'beat.wasm', 'beat' LANGUAGE wasm;

COPY (
  SELECT s.video[1]
  FROM beat(0.5) s
  WHERE s.t < 5
) TO 'beat.mp4' WITH (video_codec 'libx264')
```

```
$ ffrwd compile -f beat.sql
ffrwd-wasm -m beat=beat.wasm -filter_complex \
  'beat=every=0.5:width=320:height=240[video=out0]' -bound 'beat=[]' -map '[out0]' -f \
  nut pipe:1 | ffmpeg -copyts -f nut -analyzeduration 0 -fpsprobesize 3 -to 5 -i \
  pipe:0 -map 0:v:0 -c:0 libx264 beat.mp4
```

The compiler turns `WHERE s.t < 5` into the option `-to 5` on the ffmpeg
that reads the source. That ffmpeg takes five seconds of the stream and then
closes. A source whose reader has closed ends cleanly.

A pure node is one whose every tick depends only on what the host hands it
for that tick. `beat` reads the wall clock and keeps time between calls, so
`beat` is not pure. A source that takes its content from outside the run is
seldom pure.
