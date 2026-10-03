# 4. A reader of rows

A reader is a node that takes rows another node wrote and acts on them. A
reader might draw boxes, blur faces or show captions. This chapter builds
two readers. `band` darkens a band across the bottom of the picture while a
cue is showing, and fades the band in before the cue starts. `boxmask` turns
boxes into a matte the size of the picture, without reading a single pixel
of the picture.

## Pairing rows with the clock

A node's clock is the input that decides when the node runs. The host, which
is the program that runs the node, calls the node once per tick of the
clock, and a tick is usually one frame of the clock input. Every input that
is not the clock declares how its contents are paired with the clock's
ticks. A data input, which carries rows, pairs with a clock input in one of
two ways:

- **Lockstep.** The tick hands the rows stamped with exactly the clock
  frame's pts, one frame at a time. The rows a detector writes about the
  same picture pair this way.
- **By interval.** The tick hands every message whose pts falls in the
  tick's interval. The interval runs from the tick's time to the next tick's
  time. Rows whose times do not follow the picture's frames pair this way:
  cues, a schedule, the words a speech recogniser heard.

A data input may also take its messages as they arrive, without pairing them
to any tick. A sink reads its inputs this way ([chapter 7](07-sink.md)).
Pictures and sound paired by time, called held inputs, are the subject of
[chapter 8](08-held.md).

An input paired by interval holds back the tick until the node that produces
the rows has said that it has nothing more to send stamped inside that
interval. The producer says so with its progress, which is a time that the
host sends from the producer to the reader after every tick of the producer.
The input can also put a limit on the wait. The input's latency is the
longest it waits, in seconds. The wait is measured on the clock input: once
frames of the clock input have arrived `latency` seconds past the end of the
interval, the host stops waiting. A message that arrives later than that
comes with the next tick, and the run reports it as late.

`ahead` extends the interval at its end. Messages stamped up to `ahead`
seconds past the end of the interval are handed with that interval. `ahead`
serves a node that has to act before a given time, such as a fade that
starts before its cue.

## Rows as state

A cue that starts at one tick goes on showing at the next tick. A node that
keeps rows across ticks declares the input as state. For a state input, the
SDK, which is the library the module is built with, hands each row to the
node before the tick that the row arrives with, oldest row first. The node
keeps whatever it needs from the rows.

State rows also let a node that keeps rows stay pure. A pure node is one
whose every tick depends only on what the host hands it for that tick. The
host may spread a pure node over several worker threads, running one
instance of the node on each. An instance is one running copy of the node,
and each instance sees only some of the ticks. Before an instance runs a
tick, the host hands the instance the rows of every tick that the instance
did not process, oldest first. The SDK hands those rows to the node ahead of
the tick's own rows. As a result, every instance holds the same cues at the
same tick, whichever ticks the instance ran.

## band

**Rust**

```rust
use ffrwd_node::{Bound, Cue, Init, Input, Node, Out, Output, Result, Shape, StateRow, Tick};
use serde::Deserialize;

#[derive(Deserialize)]
struct Params {
    fade: f64,
}

struct Band {
    v: u32,
    width: usize,
    height: usize,
    fade: f64,
    cues: Vec<Cue>,
}

/// How much of the band `cue` shows at `t`: rising over `fade` seconds
/// before it starts, whole while it runs, falling over `fade` after it ends.
fn opacity(cue: &Cue, t: f64, fade: f64) -> f64 {
    if fade == 0.0 {
        return if cue.covers(t) { 1.0 } else { 0.0 };
    }
    let rising = (t - (cue.start_t - fade)) / fade;
    let falling = (cue.end_t + fade - t) / fade;
    rising.min(falling).clamp(0.0, 1.0)
}

impl Node for Band {
    const NAME: &'static str = "band";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"fade":{"type":"number","minimum":0,"maximum":5,"default":0.5}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .input(
                Input::rows("cues")
                    .interval()
                    .latency(5.0)
                    .ahead(params.fade)
                    .state()
                    .schema::<Cue>(),
            )
            .output(Output::like("v"))
            .pure()
            .one_to_one())
    }

    fn init(params: Params, init: &Init) -> Result<Band> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is a video input")?;
        Ok(Band {
            v: v.id,
            width: video.width as usize,
            height: video.height as usize,
            fade: params.fade,
            cues: Vec::new(),
        })
    }

    fn fold(&mut self, row: StateRow) -> Result<()> {
        self.cues.push(row.row::<Cue>()?);
        Ok(())
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        let t = tick.time_base().seconds(frame.pts);
        self.cues.retain(|cue| cue.end_t + self.fade > t);
        let shown = self
            .cues
            .iter()
            .map(|cue| opacity(cue, t, self.fade))
            .fold(0.0, f64::max);
        if shown == 0.0 {
            return Ok(out.pass("v", self.v, &frame)?);
        }
        let mut pixels = tick.fetch(self.v, frame.index);
        let keep = 1.0 - 0.6 * shown;
        let top = self.height * 4 / 5;
        for pixel in pixels[top * self.width * 4..].as_chunks_mut::<4>().0 {
            for channel in &mut pixel[..3] {
                *channel = (*channel as f64 * keep).round() as u8;
            }
        }
        Ok(out.frame("v", frame.pts, frame.duration, pixels)?)
    }
}

ffrwd_node::export!(Band);
```

**C++**

```cpp
#include <algorithm>
#include <cmath>
#include <vector>

#include "ffrwd/node.hpp"

struct Params {
    double fade;
    FFRWD_FIELDS(fade)
};

/// How much of the band `cue` shows at `t`: rising over `fade` seconds
/// before it starts, whole while it runs, falling over `fade` after it ends.
double opacity(const ffrwd::Cue& cue, double t, double fade) {
    if (fade == 0.0) return cue.covers(t) ? 1.0 : 0.0;
    double rising = (t - (cue.start_t - fade)) / fade;
    double falling = (cue.end_t + fade - t) / fade;
    return std::clamp(std::min(rising, falling), 0.0, 1.0);
}

struct Band : ffrwd::Node<Band, Params> {
    static constexpr std::string_view name = "band";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"fade":{"type":"number","minimum":0,"maximum":5,"default":0.5}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::size_t width = 0;
    std::size_t height = 0;
    double fade = 0.0;
    std::vector<ffrwd::Cue> cues;

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .input(ffrwd::Input::rows("cues")
                       .interval()
                       .latency(5.0)
                       .ahead(params.fade)
                       .state()
                       .schema<ffrwd::Cue>())
            .output(ffrwd::Output::like("v"))
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<Band> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        Band node;
        node.v = v.id;
        node.width = video->width;
        node.height = video->height;
        node.fade = params.fade;
        return node;
    }

    ffrwd::Status fold(const ffrwd::StateRow& row) {
        FFRWD_LET(cue, row.row<ffrwd::Cue>());
        cues.push_back(std::move(cue));
        return {};
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        double t = tick.time_base().seconds(frame->pts);
        std::erase_if(cues, [&](const ffrwd::Cue& cue) { return !(cue.end_t + fade > t); });
        double shown = 0.0;
        for (const ffrwd::Cue& cue : cues) shown = std::max(shown, opacity(cue, t, fade));
        if (shown == 0.0) return out.pass("v", v, *frame);
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        double keep = 1.0 - 0.6 * shown;
        std::size_t top = height * 4 / 5;
        for (std::size_t at = top * width * 4; at + 3 < pixels.size(); at += 4)
            for (std::size_t channel = at; channel < at + 3; ++channel)
                pixels[channel] = std::uint8_t(std::round(pixels[channel] * keep));
        return out.frame("v", frame->pts, frame->duration, std::move(pixels));
    }
};

FFRWD_EXPORT(Band);
```

**JavaScript**

```js
import { CUE, Cue, defineNode, Input, Output, Shape } from '@ffrwd/node';

/** How much of the band `cue` shows at `t`: rising over `fade` seconds
 * before it starts, whole while it runs, falling over `fade` after it ends. */
function opacity(cue, t, fade) {
  if (fade === 0) return cue.covers(t) ? 1 : 0;
  const rising = (t - (cue.start_t - fade)) / fade;
  const falling = (cue.end_t + fade - t) / fade;
  return Math.min(Math.max(Math.min(rising, falling), 0), 1);
}

export const node = defineNode({
  name: 'band',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"fade":{"type":"number","minimum":0,"maximum":5,"default":0.5}},' +
    '"additionalProperties":false}',

  shape({ fade }) {
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .input(Input.rows('cues').interval().latency(5).ahead(fade).state().schema(CUE))
      .output(Output.like('v'))
      .pure()
      .oneToOne();
  },

  init({ fade }, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    const { width, height } = video;
    let cues = [];
    return {
      fold(row) {
        const { start_t, end_t, text } = row.row();
        cues.push(new Cue(start_t, end_t, text));
      },
      process(tick, out) {
        const frame = tick.frame(v.id);
        if (frame === undefined) return;
        const t = tick.timeBase().seconds(frame.pts);
        cues = cues.filter((cue) => cue.end_t + fade > t);
        const shown = Math.max(0, ...cues.map((cue) => opacity(cue, t, fade)));
        if (shown === 0) return out.pass('v', v.id, frame);
        const pixels = tick.fetch(v.id, frame.index);
        const keep = 1 - 0.6 * shown;
        const top = Math.floor((height * 4) / 5);
        for (let at = top * width * 4; at < pixels.length; at += 4) {
          for (let channel = at; channel < at + 3; channel += 1) pixels[channel] = Math.round(pixels[channel] * keep);
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
	"slices"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Fade float64 `json:"fade"`
}

type Band struct {
	v      uint32
	width  int
	height int
	fade   float64
	cues   []node.Cue
}

// opacity is how much of the band cue shows at t: rising over fade seconds
// before it starts, whole while it runs, falling over fade after it ends.
func opacity(cue node.Cue, t, fade float64) float64 {
	if fade == 0 {
		if cue.Covers(t) {
			return 1
		}
		return 0
	}
	rising := (t - (cue.StartT - fade)) / fade
	falling := (cue.EndT + fade - t) / fade
	return min(max(min(rising, falling), 0), 1)
}

var Definition = node.Definition[Params]{
	Name:         "band",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"fade":{"type":"number","minimum":0,"maximum":5,"default":0.5}},"additionalProperties":false}`,
	Shape: func(params Params, _ *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Input(node.RowsInput("cues").
				Interval().
				Latency(5).
				Ahead(params.Fade).
				State().
				Schema(node.SchemaOf[node.Cue]())).
			Output(node.LikeOutput("v")).
			Pure().
			OneToOne(), nil
	},
	Init: func(params Params, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		video := v.VideoFormat()
		if video == nil {
			return nil, errors.New("`v` is a video input")
		}
		return &Band{v: v.ID, width: int(video.Width), height: int(video.Height), fade: params.Fade}, nil
	},
}

func (b *Band) Fold(row node.StateRow) error {
	var cue node.Cue
	if err := row.Decode(&cue); err != nil {
		return err
	}
	b.cues = append(b.cues, cue)
	return nil
}

func (b *Band) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(b.v)
	if !ok {
		return nil
	}
	t := tick.TimeBase().Seconds(frame.Pts)
	b.cues = slices.DeleteFunc(b.cues, func(cue node.Cue) bool { return cue.EndT+b.fade <= t })
	shown := 0.0
	for _, cue := range b.cues {
		shown = max(shown, opacity(cue, t, b.fade))
	}
	if shown == 0 {
		return out.Pass("v", b.v, frame)
	}
	pixels := tick.Fetch(b.v, frame.Index)
	keep := 1 - 0.6*shown
	top := b.height * 4 / 5
	for at := top * b.width * 4; at+3 < len(pixels); at += 4 {
		for channel := at; channel < at+3; channel++ {
			pixels[channel] = uint8(math.Round(float64(pixels[channel]) * keep))
		}
	}
	return out.Frame("v", frame.Pts, frame.Duration, pixels)
}

func init() { node.Export(Definition) }

func main() {}
```

The input `cues` sets its `ahead` to the call's `fade` param, so each cue
reaches the node one fade's length before the cue starts. The shape of the
input:

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/band.wasm --bound v,cues
```

**C++**

```
$ ffrwd-wasm --shape build/band.wasm --bound v,cues
```

**JavaScript**

```
$ ffrwd-wasm --shape build/band.wasm --bound v,cues
```

**Go**

```
$ ffrwd-wasm --shape build/band.wasm --bound v,cues
```

```json
{
  "accepts": {
    "channel_counts": [],
    "codecs": [],
    "like": null,
    "pixel_formats": [],
    "sample_formats": [],
    "sample_rates": [],
    "wants": "all"
  },
  "kind": "data",
  "many": false,
  "name": "cues",
  "pairing": {
    "ahead": 0.5,
    "anchor": {"kind": "shared_clock"},
    "group": null,
    "kind": "interval",
    "latency": 5.0
  },
  "required": true,
  "rows": "state",
  "schema": "{\"properties\":{\"end_t\":{\"type\":\"number\"},\"start_t\":{\"type\":\"number\"},\"text\":{\"type\":\"string\"}},\"required\":[\"end_t\",\"start_t\",\"text\"],\"type\":\"object\"}",
  "stride": 1,
  "window": 1
}
```

The schema of the input's rows is the schema of a cue: `start_t`, `end_t`
and `text`. A producer matches the schema when every field that the schema
names is also one of the producer's fields, with a type that the schema
accepts. Any other fields the producer writes are ignored. So any rows that
carry those three fields will do.

`level`, from [chapter 5](05-window.md), writes one cue for every two
seconds of sound, saying how loud those two seconds were:

```sql
CREATE FUNCTION level(a audio_stream, window number DEFAULT 2, hop number DEFAULT NULL)
RETURNS cue[]
  AS 'level.wasm', 'level' LANGUAGE wasm;

CREATE FUNCTION band(v video_stream, cues cue[], fade number DEFAULT 0.5)
RETURNS video_stream
  AS 'band.wasm', 'band' LANGUAGE wasm;

COPY (
  SELECT band(f.video[1], level(f.audio[1])), f.audio[1]
  FROM input('av.mp4') f
) TO 'banded.mp4' WITH (video_codec 'libx264', audio_codec 'aac')
```

```
$ ffrwd compile -f band.sql
ffmpeg -i av.mp4 -map 0:a:0 -map 0:v:0 -c:0 pcm_f32le -c:1 rawvideo -pix_fmt:1 rgba -f \
  nut pipe:1 | ffrwd-wasm -f nut -i pipe:0 -pad \
  '{"color": {"range": "pc", "primaries": "unknown", "trc": "unknown", "space": "gbr"}}' \
  -m level=level.wasm -m band=band.wasm -filter_complex \
  '[a=0:a]level=window=2[cues=n1];[v=0:v][cues=n1]band=fade=0.5[v=out0]' -bound \
  'level=[{"input":"a","streams":[{"rate":{"num":44100,"den":1}}]}]' -bound \
  'band=[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]},{"input":"cues","streams":[{"rate":null}]}]' \
  -map '[out0]' -f nut pipe:1 | ffmpeg -i av.mp4 -f nut -analyzeduration 0 \
  -fpsprobesize 3 -i pipe:0 -map 1:v:0 -map 0:a:0 -c:0 libx264 -c:1 aac banded.mp4
```

`ffrwd explain --delays` shows what each node waits for:

```
$ ffrwd explain --delays -f band.sql
level: tumbling 2 s
band: per-frame; cues by interval, at most 5 s
banded.mp4 stream 0 (video): 2.5 s behind the source
banded.mp4 stream 1 (audio): 0 s behind the source, waits 2.5 s
```

`level` has to hear two seconds of sound before it writes the cue for those
two seconds. `band` waits half a second past each tick for cues that start
in that half second. So the picture leaves the plan two and a half seconds
behind the source. The sound that is written beside the picture waits the
same two and a half seconds at the muxer, and the plan sizes the muxer's
wait to match.

## A picture read for its timing alone

`boxmask` makes a matte: a gray picture that is white inside each box and
black everywhere else. `boxmask` needs the picture's size and the time of
each frame, but never a pixel of the picture. An input that is read only for
its timing declares so in the node's shape, which lists the node's ports and
clock. For such an input, the host hands the node each frame's pts and
duration and the stream's info, but no pixel bytes. If the node fetches a
frame of that input, or hands a frame of that input on, the run ends with an
error that names the port.

**Rust**

```rust
use ffrwd_node::{Bound, Init, Input, NoParams, Node, Out, Output, Result, Shape, Tick};
use serde::{Deserialize, Serialize};

/// The fields `boxmask` reads. Any row carrying them will do.
#[derive(Default, Serialize, Deserialize)]
struct Box {
    x: f64,
    y: f64,
    w: f64,
    h: f64,
}

struct BoxMask {
    v: u32,
    boxes: u32,
    width: usize,
    height: usize,
}

impl Node for BoxMask {
    const NAME: &'static str = "boxmask";
    const VERSION: &'static str = "0.1.0";
    type Params = NoParams;

    fn shape(_: &NoParams, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().timing())
            .input(Input::rows("boxes").schema::<Box>())
            .output(Output::like("v").pixel_format("gray"))
            .pure()
            .one_to_one())
    }

    fn init(_: NoParams, init: &Init) -> Result<BoxMask> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is a video input")?;
        Ok(BoxMask {
            v: v.id,
            boxes: init.stream("boxes")?.id,
            width: video.width as usize,
            height: video.height as usize,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        let mut mask = vec![0u8; self.width * self.height];
        for found in tick.rows::<Box>(self.boxes)? {
            let x0 = (found.x.max(0.0) as usize).min(self.width);
            let y0 = (found.y.max(0.0) as usize).min(self.height);
            let x1 = ((found.x + found.w).max(0.0) as usize).min(self.width);
            let y1 = ((found.y + found.h).max(0.0) as usize).min(self.height);
            for y in y0..y1 {
                mask[y * self.width + x0..y * self.width + x1.max(x0)].fill(255);
            }
        }
        Ok(out.frame("v", frame.pts, frame.duration, mask)?)
    }
}

ffrwd_node::export!(BoxMask);
```

**C++**

```cpp
#include <algorithm>
#include <cmath>

#include "ffrwd/node.hpp"

/// The fields `boxmask` reads. Any row carrying them will do.
struct Box {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    FFRWD_FIELDS(x, y, w, h)
};

/// `value` as a whole pixel from 0 to `limit`.
std::size_t edge(double value, std::size_t limit) {
    return std::size_t(std::fmin(std::fmax(value, 0.0), double(limit)));
}

struct BoxMask : ffrwd::Node<BoxMask> {
    static constexpr std::string_view name = "boxmask";
    static constexpr std::string_view version = "0.1.0";

    std::uint32_t v = 0;
    std::uint32_t boxes = 0;
    std::size_t width = 0;
    std::size_t height = 0;

    static ffrwd::Result<ffrwd::Shape> shape(const ffrwd::NoParams&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().timing())
            .input(ffrwd::Input::rows("boxes").schema<Box>())
            .output(ffrwd::Output::like("v").pixel_format("gray"))
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<BoxMask> init(ffrwd::NoParams, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        FFRWD_LET(boxes, init.stream("boxes"));
        BoxMask node;
        node.v = v.id;
        node.boxes = boxes.id;
        node.width = video->width;
        node.height = video->height;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        ffrwd::Bytes mask(width * height);
        FFRWD_LET(found, tick.rows<Box>(boxes));
        for (const Box& box : found) {
            std::size_t x0 = edge(box.x, width), y0 = edge(box.y, height);
            std::size_t x1 = std::max(edge(box.x + box.w, width), x0), y1 = edge(box.y + box.h, height);
            for (std::size_t y = y0; y < y1; ++y)
                std::fill(mask.data() + y * width + x0, mask.data() + y * width + x1, 255);
        }
        return out.frame("v", frame->pts, frame->duration, std::move(mask));
    }
};

FFRWD_EXPORT(BoxMask);
```

**JavaScript**

```js
import { defineNode, Input, Output, Shape } from '@ffrwd/node';

/** The fields `boxmask` reads. Any row carrying them will do. */
const BOX = { x: 'number', y: 'number', w: 'number', h: 'number' };

export const node = defineNode({
  name: 'boxmask',
  version: '0.1.0',

  shape() {
    return new Shape()
      .input(Input.video('v').clock().timing())
      .input(Input.rows('boxes').schema(BOX))
      .output(Output.like('v').pixelFormat('gray'))
      .pure()
      .oneToOne();
  },

  init(_, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    const { width, height } = video;
    const boxes = init.stream('boxes').id;
    return {
      process(tick, out) {
        const frame = tick.frame(v.id);
        if (frame === undefined) return;
        const mask = new Uint8Array(width * height);
        for (const found of tick.rows(boxes)) {
          const x0 = Math.min(Math.trunc(Math.max(found.x, 0)), width);
          const y0 = Math.min(Math.trunc(Math.max(found.y, 0)), height);
          const x1 = Math.min(Math.trunc(Math.max(found.x + found.w, 0)), width);
          const y1 = Math.min(Math.trunc(Math.max(found.y + found.h, 0)), height);
          for (let y = y0; y < y1; y += 1) mask.fill(255, y * width + x0, y * width + Math.max(x1, x0));
        }
        out.frame('v', frame.pts, frame.duration, mask);
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

	node "github.com/imbcmdth/ffrwd-node/go"
)

// Box is the fields boxmask reads. Any row carrying them will do.
type Box struct {
	X float64 `json:"x"`
	Y float64 `json:"y"`
	W float64 `json:"w"`
	H float64 `json:"h"`
}

type BoxMask struct {
	v      uint32
	boxes  uint32
	width  int
	height int
}

var Definition = node.Definition[struct{}]{
	Name:    "boxmask",
	Version: "0.1.0",
	Shape: func(struct{}, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().Timing()).
			Input(node.RowsInput("boxes").Schema(node.SchemaOf[Box]())).
			Output(node.LikeOutput("v").PixelFormat("gray")).
			Pure().
			OneToOne(), nil
	},
	Init: func(_ struct{}, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		video := v.VideoFormat()
		if video == nil {
			return nil, errors.New("`v` is a video input")
		}
		boxes, err := init.Stream("boxes")
		if err != nil {
			return nil, err
		}
		return &BoxMask{v: v.ID, boxes: boxes.ID, width: int(video.Width), height: int(video.Height)}, nil
	},
}

func (b *BoxMask) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(b.v)
	if !ok {
		return nil
	}
	mask := make([]byte, b.width*b.height)
	boxes, err := node.ReadRows[Box](tick, b.boxes)
	if err != nil {
		return err
	}
	for _, found := range boxes {
		x0 := min(int(max(found.X, 0)), b.width)
		y0 := min(int(max(found.Y, 0)), b.height)
		x1 := min(int(max(found.X+found.W, 0)), b.width)
		y1 := min(int(max(found.Y+found.H, 0)), b.height)
		for y := y0; y < y1; y++ {
			row := mask[y*b.width+x0 : y*b.width+max(x1, x0)]
			for x := range row {
				row[x] = 255
			}
		}
	}
	return out.Frame("v", frame.Pts, frame.Duration, mask)
}

func init() { node.Export(Definition) }

func main() {}
```

The input `boxes` is lockstep with the picture, because `glow` stamps the
row about a frame with that frame's pts. The output is declared like `v`
with one field changed, the pixel format. So the matte always has the
picture's size.

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/boxmask.wasm --bound v,boxes
```

**C++**

```
$ ffrwd-wasm --shape build/boxmask.wasm --bound v,boxes
```

**JavaScript**

```
$ ffrwd-wasm --shape build/boxmask.wasm --bound v,boxes
```

**Go**

```
$ ffrwd-wasm --shape build/boxmask.wasm --bound v,boxes
```

```json
{
  "channel_counts": [],
  "codecs": [],
  "like": null,
  "pixel_formats": [],
  "sample_formats": [],
  "sample_rates": [],
  "wants": "timing"
}
```

```json
{"kind": "like", "pixel_format": "gray", "port": "v", "sample_format": null}
```

For an input read for its timing, the compiler leaves the stream in whatever
format its source already has. The compiler has ffmpeg scale that stream to
16x16 before ffmpeg sends it on, and tells the node the picture's real size.
When another node in the same host reads the same picture, as `glow` does
here, the timing input binds the stream that the other node reads instead.
That way, the picture crosses from ffmpeg to the host only once:

```sql
CREATE FUNCTION glow(v video_stream, threshold number DEFAULT 230, gap number DEFAULT 2)
RETURNS STRUCT(start_t number, id number, x number, y number, w number, h number)[]
  AS 'glow.wasm', 'glow' LANGUAGE wasm;

CREATE FUNCTION boxmask(v video_stream, boxes STRUCT(x number, y number, w number, h number)[])
RETURNS video_stream
  AS 'boxmask.wasm', 'boxmask' LANGUAGE wasm;

COPY (
  SELECT boxmask(f.video[1], glow(f.video[1]))
  FROM input('testsrc.mp4') f
) TO 'mask.mkv' WITH (video_codec 'ffv1')
```

```
$ ffrwd compile -f boxmask.sql
ffmpeg -i testsrc.mp4 -map 0:v:0 -c:0 rawvideo -pix_fmt:0 rgba -f nut pipe:1 | \
  ffrwd-wasm -f nut -i pipe:0 -pad \
  '{"color": {"range": "pc", "primaries": "unknown", "trc": "unknown", "space": "gbr"}}' \
  -m glow=glow.wasm -m boxmask=boxmask.wasm -filter_complex \
  '[v=0:v]glow=threshold=230:gap=2[glows=n1];[v=0:v][boxes=n1]boxmask[v=out0]' -bound \
  'glow=[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]' -bound \
  'boxmask=[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]},{"input":"boxes","streams":[{"rate":null}]}]' \
  -map '[out0]' -f nut pipe:1 | ffmpeg -copyts -f nut -analyzeduration 0 -fpsprobesize \
  3 -i pipe:0 -map 0:v:0 -c:0 ffv1 mask.mkv
```

`boxmask` names four fields in the schema of its input, and `glow` writes
six. Every field that `boxmask` names is one of `glow`'s fields, with a type
that `boxmask` accepts: an `integer` counts as a `number`. So the call
compiles. If the producer lacked a field that the reader names, or wrote
that field with another type, the compiler would refuse the call, and the
error would name both ports.
