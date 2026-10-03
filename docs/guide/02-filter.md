# 2. A per-frame filter

A filter takes a picture and hands back another, frame for frame, shaped by
the params of the call. This chapter builds two: `zoom`, which crops a part
of the picture and scales it back up to fill the frame, and `blend`, which
mixes a second picture over the first. Both hand a frame on untouched when
there is nothing to do.

## Params

A node's params are one JSON object, and the node declares their JSON
Schema. The compiler checks each call against it, and the SDK reads the
call's params against it again before the node sees them: a param left out
takes the schema's default, a param set to null is not set, and a whole
number given as `30.0` to an `integer` param reads as `30`. A param the
schema does not name, or one outside its type, bounds or `enum`, is refused
with the param named.

The query passes params as the function's value arguments, by position or by
name. Ports and values may come in any order.

## zoom

`zoom` takes `amount`, how far in to go, and `x` and `y`, the point to go in
on as fractions of the picture. Its crop is `1 / amount` of each side,
centred on that point as far as the picture allows.

**Rust**

```toml
ffrwd-frame = { git = "https://github.com/imbcmdth/ffrwd-frame", tag = "v0.1.1" }
```

**C++**

```cpp
#include "ffrwd/frame.hpp"
```

**JavaScript**

```js
import { Filter, planes, Rect, Rgba } from '@ffrwd/node/frame';
```

**Go**

```go
import "github.com/imbcmdth/ffrwd-node/go/frame"
```

**Rust**

```rust
use ffrwd_frame::{planes, Filter, Norm, Rect, Rgba};
use ffrwd_node::{Bound, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::Deserialize;

/// What `planes` divides by to hand back eight-bit values unchanged.
const EIGHT_BITS: Norm = Norm {
    mean: [0.0; 3],
    std: [1.0 / 255.0; 3],
};

#[derive(Deserialize)]
struct Params {
    amount: f64,
    x: f64,
    y: f64,
}

struct Zoom {
    v: u32,
    width: usize,
    height: usize,
    params: Params,
}

impl Zoom {
    /// The part of the picture that fills the frame: `1 / amount` of each
    /// side, centred on `x`, `y` as far as the picture allows.
    fn crop(&self) -> Rect {
        let (width, height) = (self.width as f64, self.height as f64);
        let w = (width / self.params.amount).round().max(1.0);
        let h = (height / self.params.amount).round().max(1.0);
        let x0 = (self.params.x * width - w / 2.0).clamp(0.0, width - w) as usize;
        let y0 = (self.params.y * height - h / 2.0).clamp(0.0, height - h) as usize;
        Rect {
            x0,
            y0,
            x1: x0 + w as usize,
            y1: y0 + h as usize,
        }
    }
}

/// Planar red, green and blue back to opaque rgba.
fn interleave(planes: &[f32], pixels: usize) -> Vec<u8> {
    let (red, rest) = planes.split_at(pixels);
    let (green, blue) = rest.split_at(pixels);
    red.iter()
        .zip(green)
        .zip(blue)
        .flat_map(|((r, g), b)| [*r, *g, *b, 255.0].map(|c| c.round().clamp(0.0, 255.0) as u8))
        .collect()
}

impl Node for Zoom {
    const NAME: &'static str = "zoom";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"amount":{"type":"number","minimum":1,"maximum":16,"default":2},"x":{"type":"number","minimum":0,"maximum":1,"default":0.5},"y":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(_: &Params, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .output(Output::like("v"))
            .pure()
            .one_to_one())
    }

    fn init(params: Params, init: &Init) -> Result<Zoom> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is a video input")?;
        Ok(Zoom {
            v: v.id,
            width: video.width as usize,
            height: video.height as usize,
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
        if self.params.amount == 1.0 {
            return Ok(out.pass("v", self.v, &frame)?);
        }
        let pixels = tick.fetch(self.v, frame.index);
        let picture = Rgba::new(&pixels, self.width, self.height)?;
        let (width, height) = (self.width, self.height);
        let rgb = planes(
            &picture,
            self.crop(),
            width,
            height,
            Filter::Bilinear,
            EIGHT_BITS,
        );
        let zoomed = interleave(&rgb, width * height);
        Ok(out.frame("v", frame.pts, frame.duration, zoomed)?)
    }
}

ffrwd_node::export!(Zoom);
```

**C++**

```cpp
#include <algorithm>
#include <cmath>
#include <vector>

#include "ffrwd/frame.hpp"
#include "ffrwd/node.hpp"

using ffrwd::frame::Filter, ffrwd::frame::Norm, ffrwd::frame::planes, ffrwd::frame::Rect, ffrwd::frame::Rgba;

/// What `planes` divides by to hand back eight-bit values unchanged.
constexpr Norm EIGHT_BITS{{0.0f, 0.0f, 0.0f}, {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f}};

struct Params {
    double amount;
    double x;
    double y;
    FFRWD_FIELDS(amount, x, y)
};

/// Planar red, green and blue back to opaque rgba.
ffrwd::Bytes interleave(const std::vector<float>& planes, std::size_t pixels) {
    ffrwd::Bytes rgba(pixels * 4);
    for (std::size_t at = 0; at < pixels; ++at) {
        for (std::size_t channel = 0; channel < 3; ++channel)
            rgba[at * 4 + channel] = std::uint8_t(std::clamp(std::round(planes[channel * pixels + at]), 0.0f, 255.0f));
        rgba[at * 4 + 3] = 255;
    }
    return rgba;
}

struct Zoom : ffrwd::Node<Zoom, Params> {
    static constexpr std::string_view name = "zoom";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"amount":{"type":"number","minimum":1,"maximum":16,"default":2},"x":{"type":"number","minimum":0,"maximum":1,"default":0.5},"y":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::size_t width = 0;
    std::size_t height = 0;
    Params params;

    /// The part of the picture that fills the frame: `1 / amount` of each
    /// side, centred on `x`, `y` as far as the picture allows.
    Rect crop() const {
        double w = std::max(std::round(double(width) / params.amount), 1.0);
        double h = std::max(std::round(double(height) / params.amount), 1.0);
        auto x0 = std::size_t(std::clamp(params.x * double(width) - w / 2.0, 0.0, double(width) - w));
        auto y0 = std::size_t(std::clamp(params.y * double(height) - h / 2.0, 0.0, double(height) - h));
        return {x0, y0, x0 + std::size_t(w), y0 + std::size_t(h)};
    }

    static ffrwd::Result<ffrwd::Shape> shape(const Params&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .output(ffrwd::Output::like("v"))
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<Zoom> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        Zoom node;
        node.v = v.id;
        node.width = video->width;
        node.height = video->height;
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
        if (params.amount == 1.0) return out.pass("v", v, *frame);
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        FFRWD_LET(picture, Rgba::make(pixels, width, height));
        auto rgb = planes(picture, crop(), width, height, Filter::Bilinear, EIGHT_BITS);
        return out.frame("v", frame->pts, frame->duration, interleave(rgb, width * height));
    }
};

FFRWD_EXPORT(Zoom);
```

**JavaScript**

```js
import { defineNode, Input, Output, Shape } from '@ffrwd/node';
import { Filter, planes, Rect, Rgba } from '@ffrwd/node/frame';

/** What `planes` divides by to hand back eight-bit values unchanged. */
const EIGHT_BITS = { mean: [0, 0, 0], std: [1 / 255, 1 / 255, 1 / 255] };

/** The part of the picture that fills the frame: `1 / amount` of each
 * side, centred on `x`, `y` as far as the picture allows. */
function crop(width, height, { amount, x, y }) {
  const w = Math.max(Math.round(width / amount), 1);
  const h = Math.max(Math.round(height / amount), 1);
  const x0 = Math.trunc(Math.min(Math.max(x * width - w / 2, 0), width - w));
  const y0 = Math.trunc(Math.min(Math.max(y * height - h / 2, 0), height - h));
  return new Rect(x0, y0, x0 + w, y0 + h);
}

/** Planar red, green and blue back to opaque rgba. */
function interleave(planes, pixels) {
  const rgba = new Uint8Array(pixels * 4).fill(255);
  for (let at = 0; at < pixels; at += 1) {
    for (let channel = 0; channel < 3; channel += 1) {
      rgba[at * 4 + channel] = Math.min(Math.max(Math.round(planes[channel * pixels + at]), 0), 255);
    }
  }
  return rgba;
}

export const node = defineNode({
  name: 'zoom',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"amount":{"type":"number","minimum":1,"maximum":16,"default":2},' +
    '"x":{"type":"number","minimum":0,"maximum":1,"default":0.5},' +
    '"y":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}',

  shape() {
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .output(Output.like('v'))
      .pure()
      .oneToOne();
  },

  init(params, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    const { width, height } = video;
    return {
      setParams(changed) {
        params = changed;
      },
      process(tick, out) {
        const frame = tick.frame(v.id);
        if (frame === undefined) return;
        if (params.amount === 1) return out.pass('v', v.id, frame);
        const picture = new Rgba(tick.fetch(v.id, frame.index), width, height);
        const rgb = planes(picture, crop(width, height, params), width, height, Filter.Bilinear, EIGHT_BITS);
        out.frame('v', frame.pts, frame.duration, interleave(rgb, width * height));
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
	"github.com/imbcmdth/ffrwd-node/go/frame"
)

// eightBits is what frame.Planes divides by to hand back eight-bit values
// unchanged.
var eightBits = frame.Norm{Std: [3]float32{1.0 / 255, 1.0 / 255, 1.0 / 255}}

type Params struct {
	Amount float64 `json:"amount"`
	X      float64 `json:"x"`
	Y      float64 `json:"y"`
}

type Zoom struct {
	v      uint32
	width  int
	height int
	params Params
}

// crop is the part of the picture that fills the frame: 1 / amount of each
// side, centred on x, y as far as the picture allows.
func (z *Zoom) crop() frame.Rect {
	width, height := float64(z.width), float64(z.height)
	w := math.Max(math.Round(width/z.params.Amount), 1)
	h := math.Max(math.Round(height/z.params.Amount), 1)
	x0 := int(min(max(z.params.X*width-w/2, 0), width-w))
	y0 := int(min(max(z.params.Y*height-h/2, 0), height-h))
	return frame.Rect{X0: x0, Y0: y0, X1: x0 + int(w), Y1: y0 + int(h)}
}

// interleave is planar red, green and blue back to opaque rgba.
func interleave(planes []float32, pixels int) []byte {
	rgba := make([]byte, 0, pixels*4)
	for at := range pixels {
		for _, c := range [4]float32{planes[at], planes[pixels+at], planes[2*pixels+at], 255} {
			rgba = append(rgba, byte(min(max(math.Round(float64(c)), 0), 255)))
		}
	}
	return rgba
}

var Definition = node.Definition[Params]{
	Name:         "zoom",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"amount":{"type":"number","minimum":1,"maximum":16,"default":2},"x":{"type":"number","minimum":0,"maximum":1,"default":0.5},"y":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}`,
	Shape: func(Params, *node.Bound) (node.Shape, error) {
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
		video := v.VideoFormat()
		if video == nil {
			return nil, errors.New("`v` is a video input")
		}
		return &Zoom{v: v.ID, width: int(video.Width), height: int(video.Height), params: params}, nil
	},
}

func (z *Zoom) SetParams(params Params) error {
	z.params = params
	return nil
}

func (z *Zoom) Process(tick *node.Tick, out *node.Out) error {
	in, ok := tick.Frame(z.v)
	if !ok {
		return nil
	}
	if z.params.Amount == 1 {
		return out.Pass("v", z.v, in)
	}
	pixels := tick.Fetch(z.v, in.Index)
	picture, err := frame.NewRgba(pixels, z.width, z.height)
	if err != nil {
		return err
	}
	width, height := z.width, z.height
	rgb := frame.Planes(picture, z.crop(), width, height, frame.Bilinear, eightBits)
	zoomed := interleave(rgb, width*height)
	return out.Frame("v", in.Pts, in.Duration, zoomed)
}

func init() { node.Export(Definition) }

func main() {}
```

**The clock.** The input `v` is the clock, so the node ticks once per frame
of `v` and each tick hands that frame. Its pixels arrive in the format the
input accepts: the compiler converts the stream to rgba before it reaches
the host, and the host hands the bytes over tightly packed, row after row.

**The output follows the input.** An output declared like `v` is named `v`
and takes `v`'s format and time base: the same size, the same pixel format.
A filter that changes neither declares its output this way and never states
a size.

**Passing a frame on.** At an `amount` of 1 the crop is the whole picture.
The node then fetches nothing and hands the input frame back as it came: the
host sends the frame's own bytes on without copying them into the module or
out again. A frame passed on this way has to be in the output's format,
which an output like its input always is.

**New params while it runs.** A call's params may change between ticks. The
node takes the new ones and the next tick uses them. Params equal to the
ones in force never reach the node, and a node that cannot take new params
refuses them, which leaves the old ones in force. A change that would change
the node's shape is refused by the host before the node sees it.

The crop and the resize are Pillow's bilinear, which is what the vision
models ffrwd runs were trained on, and they hand back planar red, green and
blue. With a normalization that scales nothing, the planes hold plain
eight-bit values, which the node interleaves back into rgba.

The query:

```sql
CREATE FUNCTION zoom(v video_stream, amount number DEFAULT 2,
                     x number DEFAULT 0.5, y number DEFAULT 0.5)
RETURNS video_stream
  AS 'zoom.wasm', 'zoom' LANGUAGE wasm;

COPY (
  SELECT zoom(f.video[1], 3, x => 0.25), f.audio[1]
  FROM input('av.mp4') f
) TO 'zoomed.mp4' WITH (video_codec 'libx264', audio_codec 'aac')
```

```
$ ffrwd compile -f zoom.sql
ffmpeg -i av.mp4 -map 0:v:0 -c:0 rawvideo -pix_fmt:0 rgba -f nut pipe:1 | ffrwd-wasm \
  -f nut -i pipe:0 -pad \
  '{"color": {"range": "pc", "primaries": "unknown", "trc": "unknown", "space": "gbr"}}' \
  -m zoom=zoom.wasm -filter_complex '[v=0:v]zoom=amount=3:x=0.25:y=0.5[v=out0]' -bound \
  'zoom=[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]' -map '[out0]' -f nut \
  pipe:1 | ffmpeg -i av.mp4 -f nut -analyzeduration 0 -fpsprobesize 3 -i pipe:0 -map \
  1:v:0 -map 0:a:0 -c:0 libx264 -c:1 aac zoomed.mp4
```

The call's params land in the node's options: `zoom=amount=3:x=0.25:y=0.5`.
`y` was never written, and the compiler fills it in from the declaration's
`DEFAULT`.

## blend

`blend` mixes `over` into `v` by `mix`: 0 is `v` alone, 1 is `over` alone.

**Rust**

```rust
use ffrwd_node::{Bound, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::Deserialize;

#[derive(Deserialize)]
struct Params {
    mix: f64,
}

struct Blend {
    v: u32,
    over: u32,
    mix: f64,
}

impl Node for Blend {
    const NAME: &'static str = "blend";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"mix":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(_: &Params, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .input(
                Input::video("over")
                    .lockstep()
                    .like("v")
                    .pixel_formats(&["rgba"]),
            )
            .output(Output::like("v"))
            .pure()
            .one_to_one())
    }

    fn init(params: Params, init: &Init) -> Result<Blend> {
        Ok(Blend {
            v: init.stream("v")?.id,
            over: init.stream("over")?.id,
            mix: params.mix,
        })
    }

    fn set_params(&mut self, params: Params) -> Result<()> {
        self.mix = params.mix;
        Ok(())
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        let Some(top) = tick.frame(self.over) else {
            return Ok(out.pass("v", self.v, &frame)?);
        };
        if self.mix == 0.0 {
            return Ok(out.pass("v", self.v, &frame)?);
        }
        if self.mix == 1.0 {
            return Ok(out.same("v", frame.pts, frame.duration, self.over, top.index)?);
        }
        let mut pixels = tick.fetch(self.v, frame.index);
        let over = tick.fetch(self.over, top.index);
        for (under, over) in pixels.iter_mut().zip(&over) {
            let mixed = *under as f64 + (*over as f64 - *under as f64) * self.mix;
            *under = mixed.round() as u8;
        }
        Ok(out.frame("v", frame.pts, frame.duration, pixels)?)
    }
}

ffrwd_node::export!(Blend);
```

**C++**

```cpp
#include <cmath>

#include "ffrwd/node.hpp"

struct Params {
    double mix;
    FFRWD_FIELDS(mix)
};

struct Blend : ffrwd::Node<Blend, Params> {
    static constexpr std::string_view name = "blend";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"mix":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::uint32_t over = 0;
    double mix = 0.0;

    static ffrwd::Result<ffrwd::Shape> shape(const Params&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .input(ffrwd::Input::video("over").lockstep().like("v").pixel_formats({"rgba"}))
            .output(ffrwd::Output::like("v"))
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<Blend> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        FFRWD_LET(over, init.stream("over"));
        Blend node;
        node.v = v.id;
        node.over = over.id;
        node.mix = params.mix;
        return node;
    }

    ffrwd::Status set_params(Params params) {
        mix = params.mix;
        return {};
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        auto top = tick.frame(over);
        if (!top) return out.pass("v", v, *frame);
        if (mix == 0.0) return out.pass("v", v, *frame);
        if (mix == 1.0) return out.same("v", frame->pts, frame->duration, over, top->index);
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        ffrwd::Bytes above = tick.fetch(over, top->index);
        for (std::size_t at = 0; at < pixels.size() && at < above.size(); ++at) {
            double mixed = double(pixels[at]) + (double(above[at]) - double(pixels[at])) * mix;
            pixels[at] = std::uint8_t(std::round(mixed));
        }
        return out.frame("v", frame->pts, frame->duration, std::move(pixels));
    }
};

FFRWD_EXPORT(Blend);
```

**JavaScript**

```js
import { defineNode, Input, Output, Shape } from '@ffrwd/node';

export const node = defineNode({
  name: 'blend',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"mix":{"type":"number","minimum":0,"maximum":1,"default":0.5}},' +
    '"additionalProperties":false}',

  shape() {
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .input(Input.video('over').lockstep().like('v').pixelFormats(['rgba']))
      .output(Output.like('v'))
      .pure()
      .oneToOne();
  },

  init({ mix }, init) {
    const v = init.stream('v').id;
    const over = init.stream('over').id;
    return {
      setParams(params) {
        mix = params.mix;
      },
      process(tick, out) {
        const frame = tick.frame(v);
        if (frame === undefined) return;
        const top = tick.frame(over);
        if (top === undefined) return out.pass('v', v, frame);
        if (mix === 0) return out.pass('v', v, frame);
        if (mix === 1) return out.same('v', frame.pts, frame.duration, over, top.index);
        const pixels = tick.fetch(v, frame.index);
        const above = tick.fetch(over, top.index);
        for (let at = 0; at < pixels.length; at += 1) {
          pixels[at] = Math.round(pixels[at] + (above[at] - pixels[at]) * mix);
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
	"math"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Mix float64 `json:"mix"`
}

type Blend struct {
	v    uint32
	over uint32
	mix  float64
}

var Definition = node.Definition[Params]{
	Name:         "blend",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"mix":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}`,
	Shape: func(Params, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Input(node.VideoInput("over").
				Lockstep().
				Like("v").
				PixelFormats("rgba")).
			Output(node.LikeOutput("v")).
			Pure().
			OneToOne(), nil
	},
	Init: func(params Params, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		over, err := init.Stream("over")
		if err != nil {
			return nil, err
		}
		return &Blend{v: v.ID, over: over.ID, mix: params.Mix}, nil
	},
}

func (b *Blend) SetParams(params Params) error {
	b.mix = params.Mix
	return nil
}

func (b *Blend) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(b.v)
	if !ok {
		return nil
	}
	top, ok := tick.Frame(b.over)
	if !ok {
		return out.Pass("v", b.v, frame)
	}
	if b.mix == 0 {
		return out.Pass("v", b.v, frame)
	}
	if b.mix == 1 {
		return out.Same("v", frame.Pts, frame.Duration, b.over, top.Index)
	}
	pixels := tick.Fetch(b.v, frame.Index)
	over := tick.Fetch(b.over, top.Index)
	for at := range min(len(pixels), len(over)) {
		under := float64(pixels[at])
		mixed := under + (float64(over[at])-under)*b.mix
		pixels[at] = uint8(math.Round(mixed))
	}
	return out.Frame("v", frame.Pts, frame.Duration, pixels)
}

func init() { node.Export(Definition) }

func main() {}
```

**Lockstep.** `over` is paired lockstep with the clock: each tick hands the
frame of `over` at exactly the clock's pts, one frame per tick. That holds
only when both pictures come from one source, through nodes that hand one
frame out for every frame in, so the compiler checks it. A second picture
that reaches the node by another path is refused:

```sql
CREATE FUNCTION blend(v video_stream, over video_stream, mix number DEFAULT 0.5)
RETURNS video_stream
  AS 'blend.wasm', 'blend' LANGUAGE wasm;

COPY (
  SELECT blend(f.video[1], hflip(f.video[1]))
  FROM input('testsrc.mp4') f
) TO 'mirrored.mp4' WITH (video_codec 'libx264')
```

```
$ ffrwd compile -f hflip.sql
error: line 1:17: UNSUPPORTED_SQL: function 'blend': the module 'blend.wasm' reads several streams, and the output of 'src_f_v_0_split' and the output of 'n1' do not run in lockstep: they reach it from different points (hint: feed every stream of a multi-stream module from one stream, through modules that declare one frame out per frame in)
```

ffmpeg's `hflip` makes no promise about frames in and out, so its picture
cannot be lockstep with the source's. `zoom` does promise it, and the query
that blends the source with a zoom of itself compiles:

```sql
CREATE FUNCTION zoom(v video_stream, amount number DEFAULT 2,
                     x number DEFAULT 0.5, y number DEFAULT 0.5)
RETURNS video_stream
  AS 'zoom.wasm', 'zoom' LANGUAGE wasm;

CREATE FUNCTION blend(v video_stream, over video_stream, mix number DEFAULT 0.5)
RETURNS video_stream
  AS 'blend.wasm', 'blend' LANGUAGE wasm;

COPY (
  SELECT blend(f.video[1], zoom(f.video[1], 4), mix => 0.3)
  FROM input('testsrc.mp4') f
) TO 'blended.mp4' WITH (video_codec 'libx264')
```

```
$ ffrwd compile -f blend.sql
ffmpeg -i testsrc.mp4 -map 0:v:0 -c:0 rawvideo -pix_fmt:0 rgba -f nut pipe:1 | \
  ffrwd-wasm -f nut -i pipe:0 -pad \
  '{"color": {"range": "pc", "primaries": "unknown", "trc": "unknown", "space": "gbr"}}' \
  -m zoom=zoom.wasm -m blend=blend.wasm -filter_complex \
  '[v=0:v]zoom=amount=4:x=0.5:y=0.5[v=n1];[v=0:v][over=n1]blend=mix=0.3[v=out0]' \
  -bound 'zoom=[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]' -bound \
  'blend=[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]},{"input":"over","streams":[{"rate":{"num":15,"den":1}}]}]' \
  -map '[out0]' -f nut pipe:1 | ffmpeg -copyts -f nut -analyzeduration 0 -fpsprobesize \
  3 -i pipe:0 -map 0:v:0 -c:0 libx264 blended.mp4
```

Both nodes run in one host, and the zoomed picture never leaves it: `zoom`
writes `n1`, and `blend` reads `n1` on its port `over`.

**Like.** `over` also declares that it is conformed to `v`. The host scales
`over` to `v`'s size before the node sees it, so the two pictures always
line up byte for byte. A node that reads two pictures of different sizes
leaves that out and reads each stream's size when it opens. The shape says
it on the input:

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/blend.wasm --bound v,over
```

**C++**

```
$ ffrwd-wasm --shape build/blend.wasm --bound v,over
```

**JavaScript**

```
$ ffrwd-wasm --shape build/blend.wasm --bound v,over
```

**Go**

```
$ ffrwd-wasm --shape build/blend.wasm --bound v,over
```

```json
{
  "accepts": {
    "channel_counts": [],
    "codecs": [],
    "like": "v",
    "pixel_formats": ["rgba"],
    "sample_formats": [],
    "sample_rates": [],
    "wants": "all"
  },
  "kind": "video",
  "many": false,
  "name": "over",
  "pairing": {"kind": "lockstep"},
  "required": true,
  "rows": "ignore",
  "schema": null,
  "stride": 1,
  "window": 1
}
```

**Handing on a frame of another input.** At a `mix` of 1 the output is
`over`'s frame as it came. The node hands it on by its stream and its place
in the tick, stamped with the clock frame's pts and duration. This is how a
switch shows a feed: it never touches the pixels of either picture.
