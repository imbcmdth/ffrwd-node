# 8. Held inputs

A live programme has pictures that come and go: a camera that connects
during the show, a replay that plays and ends, a wall of feeds where one
drops out. A node reads those as held inputs. The host keeps each one lined
up with the clock, and the node reads at every tick what to show and what
the host knows of the feed. This chapter builds `cutin`, a switch that cuts
to a feed while it is on, and `mosaic`, a compositor that lays any number of
pictures out in a grid.

## Holding a picture

A held input hands the newest frame at or before the tick. The host keeps
the source buffered ahead of the clock, repeats the last frame while the
source falls behind, skips forward when it catches up, and reports both. A
held input hands nothing before its feed's first frame shows and nothing
once the feed has ended. Held sound hands the tick's samples, or nothing
while the source is behind, and the node fills the silence.

## The feed

A feed is one source, from the tick its first frame shows on to the last
tick its last frame shows on. A source whose pts go backwards, or forwards
by more than a second, ends its feed and starts the next. A clock that jumps
ends every feed.

How a source's pts land on the clock is the input's anchor:

- **Shared clock.** The source's pts are on the clock's own timeline, as a
  timed feeder's are. Its first frame waits until the clock reaches it, and
  one the clock has already passed shows at once.
- **First frame.** The source counts from wherever it started. The host
  holds `lead` seconds of it, or all of it if it ends sooner, and then
  starts it `lead` seconds ahead of the clock, on the clock's grid.
- **Tagged.** Shared clock for a source whose tags carry the named tag set
  to `1`, first frame for any other. ffrwd's own switch reads `smart_timed`.

Three more settings shape a feed. `lead` is the seconds ahead of the clock a
first-frame feed starts. `linger` keeps the last frame showing that many
seconds after the source ends. `timeout` gives up on a live source that has
sent nothing for that many seconds of programme time, and ends its feed.

## Feeds by port

A held input may name a param that carries a port. Given a stream, the input
reads it, and the host writes the port it picked into the param. Given only
the port, it binds nothing: the host listens on that loopback port from the
moment the run starts, and whatever connects and writes a NUT of raw video
and PCM is the input's source for as long as it stays. Each connection is a
feed. Its picture is conformed on the way in, to the size of the input it
follows and the first pixel format the input accepts.

## Groups

Held inputs of one group arrive on one connection from one source: a
feeder's picture and its sound. The group's first picture fixes one offset
for all of them, and their feeds start and end on the same tick.

A data input on a group arrives on that connection too. The feeder writes a
JSON data stream beside its picture and sound, its pts counted on the same
origin as the picture's, and each row lands on the tick the picture at its
pts shows on. That is how a feeder says something about what it is sending.

## What the host knows of a feed

At every tick a held input has a feed record, from the tick its start was
fixed until its end:

- **at:** the clock time the feed's first frame shows at.
- **known:** the clock time of the tick its start was fixed on. For a
  first-frame feed, `lead` before `at`. For a timed feeder that arrives
  early, seconds before `at`. A countdown counts from here.
- **first pts:** the source's own pts of its first frame, which with `at`
  maps any of its pts onto the clock.
- **ends:** the last tick it shows on, once the host can tell. A feed read
  from a stream is told on the first tick its last frame's turn is within
  `lead` of. A feed by port is told on the first tick after its connection
  closes. A feed that stops sending is told once its `timeout` runs out, if
  `linger` keeps it.

And at every tick a held input lists the feeds that ended since the
instance's previous call, every one before on its first call, each with
`ends` set to the last tick it showed on. That list holds every end, told
ahead or not: a timeout, a connection closed with nothing queued, a clock
jump.

## Presence from the record alone

A node that says when a feed comes and goes could keep a flag from tick to
tick. Then only one instance could run it. `cutin` reads the record instead,
and writes each row on the one tick the record names:

- `coming` on the tick the start was fixed, when that is before the feed
  shows;
- `on` on the tick whose interval holds `at`;
- `off` on the tick after `ends`, from the list of ended feeds.

Each of those is one tick, run by one instance, and the record reads the
same there whichever instance runs it. So the rows come out the same on any
number of workers.

## cutin

`cutin` shows its programme `v` until a feed is on, and the feed's own frame
while it is, handing each on as it came. Between a feed's start being fixed
and its first frame, it draws a bar across the foot of the programme that
runs down to the cut. Notes the feeder writes beside its picture come out on
`presence` beside the feed's own rows.

**Rust**

```rust
use ffrwd_node::{Anchor, Bound, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::{Deserialize, Serialize};

/// The tag a feeder puts on its stream to say its pts are programme time.
const TIMED: &str = "smart_timed";

#[derive(Deserialize)]
struct Params {
    lead: f64,
    linger: f64,
    timeout: f64,
}

/// One change in what the host says of the feed, or a note the feeder
/// wrote beside its picture.
#[derive(Default, Serialize)]
struct Presence {
    event: String,
    t: f64,
    at: f64,
    text: Option<String>,
}

/// What a feeder writes beside its picture.
#[derive(Default, Serialize, Deserialize)]
struct Note {
    text: String,
}

struct Cutin {
    v: u32,
    width: usize,
    feed: Option<u32>,
    notes: Option<u32>,
    /// One frame of the programme, in its time base.
    step: i64,
}

impl Node for Cutin {
    const NAME: &'static str = "cutin";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"port":{"type":"integer","minimum":1,"maximum":65535,"default":9000},"lead":{"type":"number","minimum":0,"maximum":60,"default":0.5},"linger":{"type":"number","minimum":0,"maximum":60,"default":0},"timeout":{"type":"number","minimum":0,"maximum":60,"default":1}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, _: &Bound) -> Result<Shape> {
        let mut feed = Input::video("feed")
            .optional()
            .hold()
            .anchor(Anchor::Tagged(TIMED.to_owned()))
            .lead(params.lead)
            .group("cam")
            .port_param("port")
            .like("v")
            .pixel_formats(&["rgba"]);
        if params.linger > 0.0 {
            feed = feed.linger(params.linger);
        }
        if params.timeout > 0.0 {
            feed = feed.timeout(params.timeout);
        }
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .input(feed)
            .input(
                Input::rows("notes")
                    .optional()
                    .interval()
                    .group("cam")
                    .schema::<Note>(),
            )
            .output(Output::like("v"))
            .output(Output::rows("presence").schema::<Presence>())
            .pure()
            .one_to_one())
    }

    fn init(_: Params, init: &Init) -> Result<Cutin> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is a video input")?;
        let step = v
            .hint
            .rate
            .map_or(1, |rate| v.info.time_base.pts(rate.duration(1)).max(1));
        Ok(Cutin {
            v: v.id,
            width: video.width as usize,
            feed: init.optional("feed").map(|feed| feed.id),
            notes: init.optional("notes").map(|notes| notes.id),
            step,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        let clock = tick.time_base();
        let (pts, step) = (frame.pts, frame.duration.unwrap_or(self.step).max(1));
        let mut say = |event: &str, at: i64| -> Result<()> {
            let row = Presence {
                event: event.to_owned(),
                t: clock.seconds(pts),
                at: clock.seconds(at),
                text: None,
            };
            Ok(out.row("presence", pts, &row)?)
        };
        let mut countdown = None;
        if let Some(feed) = self.feed {
            if let Some(current) = tick.feed(feed) {
                let start = &current.start;
                if start.known == pts && start.known < start.at {
                    say("coming", start.at)?;
                }
                if pts <= start.at && start.at < pts + step {
                    say("on", start.at)?;
                }
                if pts < start.at {
                    countdown = Some((start.at - pts) as f64 / (start.at - start.known) as f64);
                }
            }
            for ended in tick.ended_feeds(feed) {
                if let Some(ends) = ended.ends.filter(|ends| *ends < pts && pts - step <= *ends) {
                    say("off", ends + step)?;
                }
            }
        }
        if let Some(notes) = self.notes {
            let base = tick.info(notes).time_base;
            for message in tick.messages(notes) {
                let at = base.rescale(message.pts, clock).max(pts);
                let row = Presence {
                    event: "note".to_owned(),
                    t: clock.seconds(pts),
                    at: clock.seconds(at),
                    text: Some(message.row::<Note>()?.text),
                };
                out.row("presence", at, &row)?;
            }
        }
        if let Some((feed, shown)) = self.feed.and_then(|id| Some((id, tick.frame(id)?))) {
            return Ok(out.same("v", pts, frame.duration, feed, shown.index)?);
        }
        let Some(left) = countdown else {
            return Ok(out.pass("v", self.v, &frame)?);
        };
        let mut pixels = tick.fetch(self.v, frame.index);
        let bar = (self.width as f64 * left) as usize;
        for row in pixels.chunks_exact_mut(self.width * 4).rev().take(8) {
            for pixel in row[..bar * 4].as_chunks_mut::<4>().0 {
                *pixel = [220, 40, 40, 255];
            }
        }
        Ok(out.frame("v", pts, frame.duration, pixels)?)
    }
}

ffrwd_node::export!(Cutin);
```

The feed and the notes, as the shape has them:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/cutin.wasm --params '{"port":9100}' --bound '[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]'
```

```json
{
  "anchor": {"kind": "tagged", "tag": "smart_timed"},
  "group": "cam",
  "kind": "hold",
  "lead": 0.5,
  "linger": null,
  "port_param": "port",
  "timeout": 1.0
}
```

```json
{
  "ahead": 0.0,
  "anchor": {"kind": "shared_clock"},
  "group": "cam",
  "kind": "interval",
  "latency": null
}
```

`feed` is conformed to `v`, so a frame of the feed can leave on `v` as it
came. `notes` arrives on the group's connection, by interval, with the
group's offset.

Given a port, the feed is whatever connects there. The compile listing names
the port and the node that listens on it:

```sql
CREATE FUNCTION cutin(v video_stream, feed video_stream DEFAULT NULL,
                      port number DEFAULT 9000, lead number DEFAULT 0.5,
                      linger number DEFAULT 0, timeout number DEFAULT 1)
RETURNS STRUCT(v video_stream,
               presence STRUCT(event text, t number, at number, text text)[])
  AS 'cutin.wasm', 'cutin' LANGUAGE wasm;

COPY (
  SELECT cutin(f.video[1], port => 9100).v, f.audio[1]
  FROM input('av.mp4') f
) TO 'cutin.mp4' WITH (video_codec 'libx264', audio_codec 'aac')
```

```
$ ffrwd compile -f cutin.sql
ffmpeg -i av.mp4 -map 0:v:0 -c:0 rawvideo -pix_fmt:0 rgba -f nut pipe:1 | ffrwd-wasm \
  -f nut -i pipe:0 -pad \
  '{"color": {"range": "pc", "primaries": "unknown", "trc": "unknown", "space": "gbr"}}' \
  -m cutin=cutin.wasm -filter_complex \
  '[v=0:v]cutin=port=9100:lead=0.5:linger=0:timeout=1[v=out0]' -bound \
  'cutin=[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]' -map '[out0]' -f nut \
  pipe:1 | ffmpeg -i av.mp4 -f nut -analyzeduration 0 -fpsprobesize 3 -i pipe:0 -map \
  1:v:0 -map 0:a:0 -c:0 libx264 -c:1 aac cutin.mp4
# listens: sidecar0 at tcp://127.0.0.1:9100 for cutin(feed)
```

Given a stream, the feed is that stream. Here it is the first second and a
half of another file, started a second after it arrives:

```sql
CREATE FUNCTION cutin(v video_stream, feed video_stream DEFAULT NULL,
                      port number DEFAULT 9000, lead number DEFAULT 0.5,
                      linger number DEFAULT 0, timeout number DEFAULT 1)
RETURNS STRUCT(v video_stream,
               presence STRUCT(event text, t number, at number, text text)[])
  AS 'cutin.wasm', 'cutin' LANGUAGE wasm;

COPY (
  SELECT cutin(p.video[1], c.video[1], lead => 1).presence
  FROM input('av.mp4') p, input('testsrc.mp4') c
  WHERE c.t <= 1.5
) TO 'presence.ndjson'
```

```
$ ffrwd compile -f presence.sql
# named pipes: sidecar0 reads ffmpeg0, ffmpeg1
1. ffmpeg: ffmpeg -i av.mp4 -map 0:v:0 -c:0 rawvideo -pix_fmt:0 rgba -f nut \
  '<named pipe ffmpeg0-sidecar0 src:p:v:0 write>'
2. ffmpeg: ffmpeg -to 1.5 -i testsrc.mp4 -map 0:v:0 -c:0 rawvideo -pix_fmt:0 rgba -f \
  nut '<named pipe ffmpeg1-sidecar0 src:c:v:0 write>'
3. sidecar: ffrwd-wasm -f nut -i '<named pipe ffmpeg0-sidecar0 src:p:v:0 read>' -pad \
  '{"color": {"range": "pc", "primaries": "unknown", "trc": "unknown", "space": "gbr"}}' \
  -f nut -i '<named pipe ffmpeg1-sidecar0 src:c:v:0 read>' -pad \
  '{"color": {"range": "pc", "primaries": "unknown", "trc": "unknown", "space": "gbr"}}' \
  -m cutin=cutin.wasm -filter_complex \
  '[v=0:v][feed=1:v]cutin=port=9000:lead=1:linger=0:timeout=1[presence=out0]' -bound \
  'cutin=[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]},{"input":"feed","streams":[{"rate":{"num":15,"den":1}}]}]' \
  -map '[out0]' -f ndjson presence.ndjson
# this listing is not a shell command -- run the plan with `ffrwd run`
```

A run of it writes three rows: `coming` when the feed's start is fixed, `on`
a second later, and `off` on the tick after the feed's last frame. When the
start is fixed depends on when the second file's first frames reach the
host, so the times move from run to run.

## mosaic

A compositor holds many pictures at once. `mosaic` takes any number of
pictures on one port, holds each on the shared clock, and ticks at the rate
of the first. A cell whose feed is down shows grey, and a cell whose feed is
up but has no frame yet stays black.

**Rust**

```rust
use ffrwd_frame::{planes, Filter, Norm, Rect, Rgba};
use ffrwd_node::{Anchor, Bound, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::Deserialize;

/// What `planes` divides by to hand back eight-bit values unchanged.
const EIGHT_BITS: Norm = Norm {
    mean: [0.0; 3],
    std: [1.0 / 255.0; 3],
};

const DOWN: [u8; 4] = [48, 48, 48, 255];

#[derive(Deserialize)]
struct Params {
    columns: usize,
    width: u32,
    height: u32,
}

struct Tile {
    id: u32,
    width: usize,
    height: usize,
    cell: Rect,
}

struct Mosaic {
    tiles: Vec<Tile>,
    width: usize,
    height: usize,
}

impl Mosaic {
    /// `pixels`, a `tile`'s picture, resized into its cell of `canvas`.
    fn put(&self, canvas: &mut [u8], tile: &Tile, pixels: &[u8]) -> Result<()> {
        let picture = Rgba::new(pixels, tile.width, tile.height)?;
        let (w, h) = (tile.cell.width(), tile.cell.height());
        let whole = Rect::whole(tile.width, tile.height);
        let rgb = planes(&picture, whole, w, h, Filter::Bilinear, EIGHT_BITS);
        for y in 0..h {
            for x in 0..w {
                let at = ((tile.cell.y0 + y) * self.width + tile.cell.x0 + x) * 4;
                for channel in 0..3 {
                    let value = rgb[channel * w * h + y * w + x];
                    canvas[at + channel] = value.round().clamp(0.0, 255.0) as u8;
                }
            }
        }
        Ok(())
    }

    fn fill(&self, canvas: &mut [u8], cell: Rect, colour: [u8; 4]) {
        for y in cell.y0..cell.y1 {
            let row = &mut canvas[(y * self.width + cell.x0) * 4..(y * self.width + cell.x1) * 4];
            for pixel in row.as_chunks_mut::<4>().0 {
                *pixel = colour;
            }
        }
    }
}

impl Node for Mosaic {
    const NAME: &'static str = "mosaic";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"columns":{"type":"integer","minimum":1,"default":2},"width":{"type":"integer","minimum":16,"default":1280},"height":{"type":"integer","minimum":16,"default":720}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(
                Input::video("v")
                    .many()
                    .hold()
                    .anchor(Anchor::SharedClock)
                    .pixel_formats(&["rgba"]),
            )
            .output(
                Output::video("v")
                    .size(params.width, params.height)
                    .pixel_format("rgba"),
            )
            .rate_of("v")
            .pure())
    }

    fn init(params: Params, init: &Init) -> Result<Mosaic> {
        let (width, height) = (params.width as usize, params.height as usize);
        let streams = init.streams("v");
        let columns = params.columns.min(streams.len()).max(1);
        let rows = streams.len().div_ceil(columns).max(1);
        let mut tiles = Vec::new();
        for (n, stream) in streams.iter().enumerate() {
            let video = stream.video_format().ok_or("`v` takes pictures")?;
            let (column, row) = (n % columns, n / columns);
            tiles.push(Tile {
                id: stream.id,
                width: video.width as usize,
                height: video.height as usize,
                cell: Rect {
                    x0: column * width / columns,
                    y0: row * height / rows,
                    x1: (column + 1) * width / columns,
                    y1: (row + 1) * height / rows,
                },
            });
        }
        Ok(Mosaic {
            tiles,
            width,
            height,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let mut canvas = [0, 0, 0, 255].repeat(self.width * self.height);
        let mut shown = false;
        for tile in &self.tiles {
            match tick.frame(tile.id) {
                Some(frame) => {
                    self.put(&mut canvas, tile, &tick.fetch(tile.id, frame.index))?;
                    shown = true;
                }
                None if tick.feed(tile.id).is_none() => self.fill(&mut canvas, tile.cell, DOWN),
                None => {}
            }
        }
        if !shown && tick.last() {
            return Ok(());
        }
        Ok(out.frame("v", tick.pts(), Some(1), canvas)?)
    }
}

ffrwd_node::export!(Mosaic);
```

A port that takes many streams cannot be the clock, so the node ticks at a
rate, here the rate of `v`'s first stream, which the compiler reads off it:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/mosaic.wasm --params '{"columns":3,"width":960,"height":240}' --bound v,v,v
```

```json
{"kind": "rate_of", "port": "v"}
```

```json
{
  "anchor": {"kind": "shared_clock"},
  "group": null,
  "kind": "hold",
  "lead": 0.0,
  "linger": null,
  "port_param": null,
  "timeout": null
}
```

An array written to the port binds every stream in it, in order:

```sql
CREATE FUNCTION mosaic(v video_stream[], columns number DEFAULT 2,
                       width number DEFAULT 1280, height number DEFAULT 720)
RETURNS video_stream
  AS 'mosaic.wasm', 'mosaic' LANGUAGE wasm;

COPY (
  SELECT mosaic(ARRAY[a.video[1], b.video[1], c.video[1]], 3, 960, 240)
  FROM input('av.mp4') a, input('av2.mp4') b, input('testsrc.mp4') c
) TO 'mosaic.mp4' WITH (video_codec 'libx264')
```

```
$ ffrwd compile -f mosaic.sql
# named pipes: sidecar0 reads ffmpeg1, ffmpeg2, ffmpeg3
1. ffmpeg: ffmpeg -copyts -f nut -analyzeduration 0 -fpsprobesize 3 -i pipe:0 -map \
  0:v:0 -c:0 libx264 mosaic.mp4
2. ffmpeg: ffmpeg -i av.mp4 -map 0:v:0 -c:0 rawvideo -pix_fmt:0 rgba -f nut \
  '<named pipe ffmpeg1-sidecar0 src:a:v:0 write>'
3. ffmpeg: ffmpeg -i av2.mp4 -map 0:v:0 -c:0 rawvideo -pix_fmt:0 rgba -f nut \
  '<named pipe ffmpeg2-sidecar0 src:b:v:0 write>'
4. ffmpeg: ffmpeg -i testsrc.mp4 -map 0:v:0 -c:0 rawvideo -pix_fmt:0 rgba -f nut \
  '<named pipe ffmpeg3-sidecar0 src:c:v:0 write>'
5. sidecar: ffrwd-wasm -f nut -i '<named pipe ffmpeg1-sidecar0 src:a:v:0 read>' -pad \
  '{"color": {"range": "pc", "primaries": "unknown", "trc": "unknown", "space": "gbr"}}' \
  -f nut -i '<named pipe ffmpeg2-sidecar0 src:b:v:0 read>' -pad \
  '{"color": {"range": "pc", "primaries": "unknown", "trc": "unknown", "space": "gbr"}}' \
  -f nut -i '<named pipe ffmpeg3-sidecar0 src:c:v:0 read>' -pad \
  '{"color": {"range": "pc", "primaries": "unknown", "trc": "unknown", "space": "gbr"}}' \
  -m mosaic=mosaic.wasm -filter_complex \
  '[v=0:v][v=1:v][v=2:v]mosaic=columns=3:width=960:height=240[v=out0]' -bound \
  'mosaic=[{"input":"v","streams":[{"rate":{"num":15,"den":1}},{"rate":{"num":15,"den":1}},{"rate":{"num":15,"den":1}}]}]' \
  -map '[out0]' -f nut pipe:1
# this listing is not a shell command -- run the plan with `ffrwd run`
```

The switch and compositor in ffrwd's own packages, `ffrwd.switch.switch` and
`ffrwd.blitz.compose`, are these two nodes grown up: sound mixed in and out
with the picture, presence that drives an HTML page, feeds by port for every
input.
