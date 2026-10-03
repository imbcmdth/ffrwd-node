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

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/blend.wasm --bound v,over
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
