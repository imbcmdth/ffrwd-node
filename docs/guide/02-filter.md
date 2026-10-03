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

The crop and the resize are the examples' own `common/resize.hpp`, which the
build puts on the include path:

```sh
$cxx -O2 -I"$sdk/include" -I../../common -c src/zoom.cpp -o build/zoom.o
```

**Go**

The crop and the resize are the example's own `resize.go`:

```go
package main

import (
	"fmt"
	"math"
)

// What the Rust example takes from ffrwd-frame, written out by hand: the
// crop and Pillow's bilinear resize, as ffrwd-frame asks fast_image_resize
// for it. The resize antialiases when it shrinks, goes across first and then
// down, and rounds back into eight bits after each pass, so its bytes are
// the Rust example's.

// Rect is a half-open rectangle of a picture, in pixels: exclusive on the
// right and bottom.
type Rect struct {
	X0, Y0, X1, Y1 int
}

// Whole is the whole picture.
func Whole(width, height int) Rect {
	return Rect{0, 0, width, height}
}

func (r Rect) Width() int  { return max(r.X1-r.X0, 0) }
func (r Rect) Height() int { return max(r.Y1-r.Y0, 0) }

// resize is rect of an rgba picture resized to width x height: interleaved
// red, green and blue, alpha dropped.
func resize(pixels []byte, pictureWidth, pictureHeight int, rect Rect, width, height int) ([]byte, error) {
	if want := pictureWidth * pictureHeight * 4; len(pixels) != want {
		return nil, fmt.Errorf("an rgba frame of %dx%d is %d bytes, not %d", pictureWidth, pictureHeight, want, len(pixels))
	}
	rect.X1, rect.Y1 = min(rect.X1, pictureWidth), min(rect.Y1, pictureHeight)
	rect.X0, rect.Y0 = min(rect.X0, rect.X1), min(rect.Y0, rect.Y1)
	cw, ch := rect.Width(), rect.Height()
	if cw == 0 || ch == 0 || width == 0 || height == 0 {
		return make([]byte, width*height*3), nil
	}
	rgb := make([]byte, 0, cw*ch*3)
	for y := rect.Y0; y < rect.Y1; y++ {
		for x := rect.X0; x < rect.X1; x++ {
			at := (y*pictureWidth + x) * 4
			rgb = append(rgb, pixels[at:at+3]...)
		}
	}
	if width != cw {
		rgb = horizontal(rgb, cw, ch, width)
	}
	if height != ch {
		rgb = vertical(rgb, width, ch, height)
	}
	return rgb, nil
}

// kernel is, for each of size pixels resampled from in, the first source
// pixel it reads and the weight of each it reads, in fixed point of
// precision bits.
type kernel struct {
	starts    []int
	weights   [][]int16
	precision uint
}

func bilinear(x float64) float64 {
	x = math.Abs(x)
	if x < 1 {
		return 1 - x
	}
	return 0
}

func newKernel(in, size int) kernel {
	scale := float64(in) / float64(size)
	filterScale := max(scale, 1)
	radius := filterScale
	window := int(math.Ceil(radius))*2 + 1
	recip := 1 / filterScale
	values := make([]float64, 0, window*size)
	type bound struct{ start, size int }
	bounds := make([]bound, 0, size)
	for out := range size {
		inCenter := (float64(out) + 0.5) * scale
		lo := int(math.Max(math.Floor(inCenter-radius), 0))
		hi := int(math.Min(math.Ceil(inCenter+radius), float64(in)))
		at := len(values)
		total := 0.0
		center := inCenter - 0.5
		start, end := lo, hi
		for x := lo; x < hi; x++ {
			w := bilinear((float64(x) - center) * recip)
			if x == start && w == 0 {
				start++
			} else {
				values = append(values, w)
				total += w
			}
		}
		for i := len(values) - 1; i >= 0; i-- {
			if end <= start || values[i] != 0 {
				break
			}
			end--
		}
		if total != 0 {
			for i := at; i < len(values); i++ {
				values[i] /= total
			}
		}
		for len(values) < at+window {
			values = append(values, 0)
		}
		values = values[:at+window]
		bounds = append(bounds, bound{start, end - start})
	}

	heaviest := 0.0
	for n, w := range values {
		if n == 0 || w > heaviest {
			heaviest = w
		}
	}
	var precision uint
	for bits := uint(0); bits < 32-8-2; bits++ {
		precision = bits
		if int32(math.Round(heaviest*float64(int32(1)<<(bits+1)))) >= 1<<15 {
			break
		}
	}
	k := kernel{precision: precision}
	scaleBy := float64(int32(1) << precision)
	for n, b := range bounds {
		weights := make([]int16, b.size)
		for i := range weights {
			weights[i] = int16(math.Round(values[n*window+i] * scaleBy))
		}
		k.starts = append(k.starts, b.start)
		k.weights = append(k.weights, weights)
	}
	return k
}

func (k kernel) clip(sum int32) byte {
	return byte(min(max(sum>>k.precision, 0), 255))
}

// horizontal resamples each row of an rgb picture to width pixels.
func horizontal(rgb []byte, inWidth, height, width int) []byte {
	k := newKernel(inWidth, width)
	initial := int32(1) << (k.precision - 1)
	out := make([]byte, width*height*3)
	for y := range height {
		row := rgb[y*inWidth*3:]
		for x := range width {
			sums := [3]int32{initial, initial, initial}
			for i, w := range k.weights[x] {
				at := (k.starts[x] + i) * 3
				for c := range 3 {
					sums[c] += int32(row[at+c]) * int32(w)
				}
			}
			for c := range 3 {
				out[(y*width+x)*3+c] = k.clip(sums[c])
			}
		}
	}
	return out
}

// vertical resamples each column of an rgb picture to height pixels.
func vertical(rgb []byte, width, inHeight, height int) []byte {
	k := newKernel(inHeight, height)
	initial := int32(1) << (k.precision - 1)
	stride := width * 3
	out := make([]byte, stride*height)
	for y := range height {
		for x := range stride {
			sum := initial
			for i, w := range k.weights[y] {
				sum += int32(rgb[(k.starts[y]+i)*stride+x]) * int32(w)
			}
			out[y*stride+x] = k.clip(sum)
		}
	}
	return out
}
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

#include "ffrwd/node.hpp"
#include "resize.hpp"

struct Params {
    double amount;
    double x;
    double y;
    FFRWD_FIELDS(amount, x, y)
};

/// Red, green and blue back to opaque rgba.
ffrwd::Bytes interleave(const std::vector<std::uint8_t>& rgb, std::size_t pixels) {
    ffrwd::Bytes rgba(pixels * 4);
    for (std::size_t at = 0; at < pixels; ++at) {
        for (std::size_t channel = 0; channel < 3; ++channel) rgba[at * 4 + channel] = rgb[at * 3 + channel];
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
    resize::Rect crop() const {
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
        if (auto wrong = resize::misfit(pixels.size(), width, height)) return ffrwd::fail(*wrong);
        auto rgb = resize::bilinear(pixels.data(), width, height, crop(), width, height);
        return out.frame("v", frame->pts, frame->duration, interleave(rgb, width * height));
    }
};

FFRWD_EXPORT(Zoom);
```

**JavaScript**

```js
import { defineNode, Input, Output, Shape } from '@ffrwd/node';

/** The part of the picture that fills the frame: `1 / amount` of each
 * side, centred on `x`, `y` as far as the picture allows. */
function crop(width, height, { amount, x, y }) {
  const w = Math.max(Math.round(width / amount), 1);
  const h = Math.max(Math.round(height / amount), 1);
  const x0 = Math.trunc(Math.min(Math.max(x * width - w / 2, 0), width - w));
  const y0 = Math.trunc(Math.min(Math.max(y * height - h / 2, 0), height - h));
  return { x0, y0, x1: x0 + w, y1: y0 + h };
}

// Pillow's bilinear resize, written out here: it works in the same fixed
// point, pass for pass, as the ffrwd-frame crate the Rust example calls, so
// both modules make the same picture to the byte.

/** For each of `size` pixels made from `from`: the first pixel it reads, and
 * its weights in fixed point with `bits` fractional bits. */
function taps(from, size) {
  const scale = from / size;
  const stretch = Math.max(scale, 1);
  const kernels = [];
  for (let at = 0; at < size; at += 1) {
    const centre = (at + 0.5) * scale;
    let first = Math.max(Math.floor(centre - stretch), 0);
    const end = Math.min(Math.ceil(centre + stretch), from);
    const weights = [];
    for (let x = first; x < end; x += 1) {
      const weight = Math.max(0, 1 - Math.abs((x - (centre - 0.5)) * (1 / stretch)));
      if (weight === 0 && weights.length === 0) first += 1;
      else weights.push(weight);
    }
    const sum = weights.reduce((total, weight) => total + weight, 0);
    while (weights.at(-1) === 0) weights.pop();
    kernels.push({ first, weights: weights.map((weight) => (sum === 0 ? weight : weight / sum)) });
  }
  const most = Math.max(0, ...kernels.flatMap((kernel) => kernel.weights));
  let bits = 0;
  while (bits < 21 && Math.round(most * 2 ** (bits + 1)) < 2 ** 15) bits += 1;
  for (const kernel of kernels) kernel.weights = kernel.weights.map((weight) => Math.round(weight * 2 ** bits));
  return { kernels, bits };
}

/** `size` pixels across (or down) made from each row (or column) of an rgb
 * picture `width` x `height`, rounded back to eight bits. */
function pass(rgb, width, height, size, across) {
  const { kernels, bits } = taps(across ? width : height, size);
  const [w, h] = across ? [size, height] : [width, size];
  const out = new Uint8Array(w * h * 3);
  for (let y = 0; y < h; y += 1) {
    for (let x = 0; x < w; x += 1) {
      const { first, weights } = kernels[across ? x : y];
      for (let channel = 0; channel < 3; channel += 1) {
        let sum = 1 << (bits - 1);
        for (let n = 0; n < weights.length; n += 1) {
          const at = across ? (y * width + first + n) * 3 : ((first + n) * width + x) * 3;
          sum += rgb[at + channel] * weights[n];
        }
        out[(y * w + x) * 3 + channel] = Math.min(Math.max(sum >> bits, 0), 255);
      }
    }
  }
  return out;
}

/** `rect` of an rgba picture `stride` pixels wide, resized to `width` x
 * `height`: across first, then down, as Pillow does. Red, green and blue. */
function resize(pixels, stride, rect, width, height) {
  let [w, h] = [rect.x1 - rect.x0, rect.y1 - rect.y0];
  let rgb = new Uint8Array(w * h * 3);
  for (let y = 0; y < h; y += 1) {
    for (let x = 0; x < w; x += 1) {
      const [from, to] = [((rect.y0 + y) * stride + rect.x0 + x) * 4, (y * w + x) * 3];
      for (let channel = 0; channel < 3; channel += 1) rgb[to + channel] = pixels[from + channel];
    }
  }
  if (w !== width) [rgb, w] = [pass(rgb, w, h, width, true), width];
  if (h !== height) [rgb, h] = [pass(rgb, w, h, height, false), height];
  return rgb;
}

/** Red, green and blue back to opaque rgba. */
function interleave(rgb) {
  const pixels = new Uint8Array((rgb.length / 3) * 4).fill(255);
  for (let at = 0; at < rgb.length / 3; at += 1) {
    for (let channel = 0; channel < 3; channel += 1) pixels[at * 4 + channel] = rgb[at * 3 + channel];
  }
  return pixels;
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
        const pixels = tick.fetch(v.id, frame.index);
        const zoomed = interleave(resize(pixels, width, crop(width, height, params), width, height));
        out.frame('v', frame.pts, frame.duration, zoomed);
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
func (z *Zoom) crop() Rect {
	width, height := float64(z.width), float64(z.height)
	w := math.Max(math.Round(width/z.params.Amount), 1)
	h := math.Max(math.Round(height/z.params.Amount), 1)
	x0 := int(min(max(z.params.X*width-w/2, 0), width-w))
	y0 := int(min(max(z.params.Y*height-h/2, 0), height-h))
	return Rect{X0: x0, Y0: y0, X1: x0 + int(w), Y1: y0 + int(h)}
}

// opaque is interleaved red, green and blue back to opaque rgba.
func opaque(rgb []byte) []byte {
	pixels := make([]byte, 0, len(rgb)/3*4)
	for at := 0; at+2 < len(rgb); at += 3 {
		pixels = append(pixels, rgb[at], rgb[at+1], rgb[at+2], 255)
	}
	return pixels
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
	frame, ok := tick.Frame(z.v)
	if !ok {
		return nil
	}
	if z.params.Amount == 1 {
		return out.Pass("v", z.v, frame)
	}
	pixels := tick.Fetch(z.v, frame.Index)
	rgb, err := resize(pixels, z.width, z.height, z.crop(), z.width, z.height)
	if err != nil {
		return err
	}
	return out.Frame("v", frame.Pts, frame.Duration, opaque(rgb))
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
