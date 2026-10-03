# 6. A source

A source reads nothing and makes streams: a test pattern, a page rendered
from HTML, a broadcast pulled from a relay. A query reads it in `FROM`, as
it reads a file. This chapter builds `bars`, which draws colour bars at a
rate, and `beat`, which makes a picture whenever it has one.

## Its own clock

A node with no input clock keeps time itself, in one of two ways.

- **A rate.** The node ticks so many times a second. The tick's pts is its
  number, from 0, in a time base of one over the rate. In a live run the
  ticks are paced to the wall clock; otherwise they come as fast as the
  outputs drain.
- **Self-clocked.** The node emits when it has something, and its outputs'
  pts are the timeline. A tick's pts is microseconds since the node's first
  call. The call may wait until there is something to emit, and the host
  calls again as soon as it returns. A source on the network is this kind.

An output of a source has no input to take its format from, so it states
one: a size and a pixel format, or a sample rate, channels and a sample
format.

## Bounded, and finished

A source says whether it ends by itself. One that does not is live: the
compiler plans the query as live, and something else ends it. One that does
says so with its last emission: the node finishes, the host makes the last
call, and every output ends.

## Relation rows

A source read in `FROM` is a table: one row per rendition, the way a
manifest's ladder is. The shape lists the rows as JSON objects, and each
output names the row it belongs to. A query picks a rendition by the row's
fields, `WHERE s.height = 720`.

## bars

`bars` ticks at `fps`. With `seconds` it is bounded and finishes on the
first tick at or past that time; without, it runs until its reader stops.

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

Its whole shape, bounded:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/bars.wasm --params '{"width":640,"height":360,"seconds":5}'
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

Declared `RETURNS source`, it is called in `FROM`, and the alias carries the
stream columns it makes:

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

A source binds nothing, so its `-bound` list is empty.

## beat

`beat` makes a frame every `every` seconds of the wall clock, grey by the
second it was made in. It sleeps until the next frame is due, and stamps the
frame at the time it was due, in microseconds from its first call.

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

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/beat.wasm --params '{"every":0.5}'
```

```json
{"kind": "self_clocked"}
```

It never ends by itself, so a query that reads it says where to stop:

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

`WHERE s.t < 5` becomes the reader's `-to 5`. The reader takes five seconds
and closes, and a source whose reader has closed ends cleanly.

`beat` reads the wall clock and keeps time between calls, so it is not pure.
A source fed from outside the run seldom is.
