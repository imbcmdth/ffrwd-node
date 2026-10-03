# 4. A reader of rows

A reader takes rows another node wrote and acts on them: draws boxes, blurs
faces, shows captions. This chapter builds `band`, which darkens a band at
the foot of the picture while a cue is showing and fades it in ahead of the
cue, and `boxmask`, which turns boxes into a matte the size of the picture
without reading a pixel of it.

## Pairing rows with the clock

Every input that is not the clock says how it pairs with the clock. A data
input pairs in one of two ways with a clock input:

- **Lockstep.** The rows stamped at exactly the clock's pts, frame for
  frame. A detector's rows over the same picture pair this way.
- **By interval.** Every message whose pts falls in the tick's interval,
  from the tick's time to the next tick's. Rows on a clock of their own pair
  this way: cues, a schedule, the words a recogniser heard.

A data input may also take its messages as they arrive, unpaired, which is a
sink's way ([chapter 7](07-sink.md)). Pictures and sound held by time are
[chapter 8](08-held.md).

An interval input holds the tick until its producer has said it has nothing
more to send stamped in that interval. A producer says so with its progress,
which the host sends down the edge after every tick. The input can also
bound the wait: its latency is the most it waits, in seconds, counted on the
clock input's own arrival. A message later than that comes with the next
tick, and the run reports it.

`ahead` widens the interval at its end. Messages stamped up to that many
seconds past the interval come with it, for a node that has to act before a
time: a fade that starts before its cue.

## Rows as state

A cue that starts at one tick goes on showing at the next. A node that keeps
rows across ticks declares the input as state. The SDK then hands each row
to the node before the tick it arrives with, oldest first, and the node
keeps what it needs.

State rows are also what lets such a node stay pure. When the host spreads
the node over workers, each instance sees only some of the ticks. Before an
instance's tick, the host hands it the rows of every tick it did not
process, oldest first, and the SDK hands them to the node ahead of the
tick's own. Every instance holds the same cues at the same tick, whichever
ticks it ran.

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

The cue input carries `ahead` from the call's `fade`, so a cue arrives a
fade's length before it starts. Its shape:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/band.wasm --bound v,cues
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

Its rows' schema is the cue's own: `start_t`, `end_t` and `text`. A producer
matches it when every field the schema names is among the producer's, with a
type it takes. Fields beyond them pass by, so any rows carrying those three
will do.

`level`, from [chapter 5](05-window.md), writes a cue for every two seconds
of sound saying how loud it was:

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

`ffrwd explain --delays` says what each node waits for:

```
$ ffrwd explain --delays -f band.sql
level: tumbling 2 s
band: per-frame; cues by interval, at most 5 s
banded.mp4 stream 0 (video): 2.5 s behind the source
banded.mp4 stream 1 (audio): 0 s behind the source, waits 2.5 s
```

`level` hears two seconds before it writes a cue for them, and `band` waits
half a second past each tick for cues that start then. So the picture leaves
two and a half seconds behind the source, and the sound written beside it
waits as long at the muxer, which the plan sizes.

## A picture read for its timing alone

`boxmask` makes a gray matte, white inside each box and black elsewhere. It
needs the picture's size and the time of each frame, and never a pixel. An
input read for its timing alone says so. The host hands its frames' pts and
durations and the stream's info, and no bytes. Fetching one of its frames,
or handing it on, ends the run with the port named.

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

The rows are lockstep with the picture: `glow` stamps a row for a frame with
that frame's pts. The output is like `v` with one field changed, its pixel
format, so the matte is always the picture's size.

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/boxmask.wasm --bound v,boxes
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

The compiler hands a timing input the stream in whatever format its source
already has, scaled to 16x16 on the way out of ffmpeg, and tells the node
the picture's own size. Where another node in the same host reads the same
picture, as `glow` does here, the timing input binds that stream instead, so
the picture crosses once:

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

`boxmask` names four fields, and `glow` writes six. Every field `boxmask`
names is among `glow`'s with a type it takes, an `integer` being a `number`,
so the call compiles. A field missing from the producer, or of another type,
is refused at compile time, naming both ports.
