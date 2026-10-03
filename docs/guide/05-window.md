# 5. A window

Some work needs more than one frame at a time: a loudness over a second of
sound, a picture compared with the one before it. The clock input says how
much each tick sees and how far the next tick moves. This chapter builds
`level`, which writes a cue saying how loud each stretch of sound was, and
`still`, which finds the stretches where the picture does not move.

## Window and stride

The clock input's window is how many frames, or samples of sound, a tick
sees. Its stride is how many the tick consumes, which is how far the next
tick starts after this one. Every other input hands what falls in the tick's
interval, one stride long.

| window and stride | the tick sees |
|---|---|
| 1 and 1 | one frame: a per-frame node |
| equal | a block, then the next block: tumbling |
| stride under the window | blocks that overlap by the difference: hopping |
| stride of 1 frame | the newest frames, moving one at a time: sliding |

A window of video is that many frames in the tick, oldest first. A window of
sound is one frame: the tick's samples as one run, interleaved, re-cut from
whatever packets arrived. The last tick takes whatever is left, which may be
less than a window.

## The bound rate

A window is counted in frames or samples, and the call's params say it in
seconds. The shape is asked with the rate of every stream the call binds:
the frame rate of a picture, the sample rate of a sound. So the node turns
seconds into a count exactly, at whatever rate it is bound.

## level

**Rust**

```rust
use ffrwd_node::{Bound, Cue, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::Deserialize;

#[derive(Deserialize)]
struct Params {
    window: f64,
    hop: Option<f64>,
}

struct Level {
    a: u32,
    channels: usize,
    rate: f64,
}

/// How loud `samples` are, as a cue's text: their RMS in dB of full scale.
fn loudness(samples: &[f32]) -> String {
    let power =
        samples.iter().map(|s| (*s as f64).powi(2)).sum::<f64>() / samples.len().max(1) as f64;
    let db = 10.0 * power.log10();
    if db < -90.0 {
        "silence".to_owned()
    } else {
        format!("{db:.0} dB")
    }
}

impl Node for Level {
    const NAME: &'static str = "level";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"window":{"type":"number","exclusiveMinimum":0,"maximum":60,"default":2},"hop":{"type":["number","null"],"exclusiveMinimum":0,"maximum":60}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, bound: &Bound) -> Result<Shape> {
        let rate = bound
            .rate_of("a")
            .ok_or("level counts its window in samples, and the call gives `a` no sample rate")?;
        let window = rate.count(params.window) as u32;
        let stride = rate.count(params.hop.unwrap_or(params.window)) as u32;
        Ok(Shape::new()
            .input(
                Input::audio("a")
                    .clock()
                    .window(window, stride)
                    .sample_formats(&["f32"]),
            )
            .output(Output::rows("cues").schema::<Cue>())
            .pure())
    }

    fn init(_: Params, init: &Init) -> Result<Level> {
        let a = init.stream("a")?;
        let audio = a.audio_format().ok_or("`a` is an audio input")?;
        Ok(Level {
            a: a.id,
            channels: audio.channels.max(1) as usize,
            rate: audio.sample_rate as f64,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(run) = tick.frame(self.a) else {
            return Ok(());
        };
        let bytes = tick.fetch(self.a, run.index);
        let samples: Vec<f32> = bytes
            .as_chunks::<4>()
            .0
            .iter()
            .map(|b| f32::from_le_bytes(*b))
            .collect();
        let start = tick.time_base().seconds(run.pts);
        let end = start + (samples.len() / self.channels) as f64 / self.rate;
        let cue = Cue::new(start, end, loudness(&samples));
        Ok(out.row("cues", run.pts, &cue)?)
    }
}

ffrwd_node::export!(Level);
```

At 44.1 kHz, a window of one second hopping every half second is:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/level.wasm --params '{"window":1,"hop":0.5}' --bound '[{"input":"a","streams":[{"rate":{"num":44100,"den":1}}]}]'
```

```json
{
  "accepts": {
    "channel_counts": [],
    "codecs": [],
    "like": null,
    "pixel_formats": [],
    "sample_formats": ["f32"],
    "sample_rates": [],
    "wants": "all"
  },
  "kind": "audio",
  "many": false,
  "name": "a",
  "pairing": {"kind": "lockstep"},
  "required": true,
  "rows": "ignore",
  "schema": null,
  "stride": 22050,
  "window": 44100
}
```

A call that gives no rate gets a refusal naming the input, which the
compiler reports where the call is written. The compiler always knows a
sound's rate: it is the rate the sound reaches the node at.

A cue for a window is stamped at the window's first sample, and leaves with
the tick that saw the window. That is on time: the window's own length is
not lateness, and the output declares none. The compiler adds the window to
what it says each path waits.

```sql
CREATE FUNCTION level(a audio_stream, window number DEFAULT 2, hop number DEFAULT NULL)
RETURNS cue[]
  AS 'level.wasm', 'level' LANGUAGE wasm;

COPY (
  SELECT f.video[1], f.audio[1], level(f.audio[1], 1, 0.5)
  FROM input('av.mp4') f
) TO 'levels.mkv' WITH (video_codec 'copy', audio_codec 'copy')
```

```
$ ffrwd compile -f level.sql
ffmpeg -i av.mp4 -map 0:a:0 -c:0 pcm_f32le -f nut pipe:1 | ffrwd-wasm -f nut -i pipe:0 \
  -m level=level.wasm -filter_complex '[a=0:a]level=window=1:hop=0.5[cues=out0]' \
  -bound 'level=[{"input":"a","streams":[{"rate":{"num":44100,"den":1}}]}]' -map \
  '[out0]' -f webvtt pipe:1 | ffmpeg -i av.mp4 -f webvtt -i pipe:0 -map 0:v:0 -map \
  0:a:0 -map 1:s:0 -c:2 copy -c:0 copy -c:1 copy levels.mkv
```

```
$ ffrwd explain --delays -f level.sql
level: hopping 1 s every 0.5 s
levels.mkv stream 0 (video): 0 s behind the source, waits 1 s
levels.mkv stream 1 (audio): 0 s behind the source, waits 1 s
levels.mkv stream 2 (subtitle): 1 s behind the source
```

Written beside a picture, rows of `start_t`, `end_t` and `text` are a
subtitle track:

```vtt
WEBVTT

00:00.000 --> 00:01.000
-21 dB

00:00.500 --> 00:01.500
-21 dB

00:01.000 --> 00:02.000
```

## Latency in seconds

A node may write a row about a time well before the tick it writes it on.
`still` writes a row for each stretch where the picture holds, stamped at
the stretch's start, once the stretch ends. Its output declares how far
behind the end of its tick's interval a row may be stamped: its latency, in
seconds. Each tick, the host tells the output's readers that nothing more
will come stamped before the interval's end less that latency. A row that
breaks the promise reaches its readers late, and the run reports it.

`still` cuts a stretch at `longest` seconds and carries on with a new one,
and the row for a stretch leaves on the tick after its last frame. So its
latency is `longest` and one frame, which the bound rate turns into seconds.

**Rust**

```rust
use ffrwd_node::{Bound, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::{Deserialize, Serialize};

#[derive(Deserialize)]
struct Params {
    shortest: f64,
    longest: f64,
    tolerance: f64,
}

#[derive(Default, Serialize)]
struct Still {
    start_t: f64,
    end_t: f64,
}

struct StillNode {
    v: u32,
    /// The bytes of a picture's luma plane, which come first in yuv420p.
    luma: usize,
    params: Params,
    /// Where the stretch the picture is still in began: its pts and seconds.
    open: Option<(i64, f64)>,
}

/// How far apart two pictures' luma planes are: the mean difference of a
/// pixel.
fn difference(a: &[u8], b: &[u8]) -> f64 {
    let total: u64 = a.iter().zip(b).map(|(a, b)| a.abs_diff(*b) as u64).sum();
    total as f64 / a.len().max(1) as f64
}

impl StillNode {
    /// Writes the open stretch as ending at `end_t`, if it lasted long enough.
    fn close(&mut self, end_t: f64, out: &mut Out) -> Result<()> {
        if let Some((pts, start_t)) = self.open.take() {
            if end_t - start_t >= self.params.shortest {
                out.row("stills", pts, &Still { start_t, end_t })?;
            }
        }
        Ok(())
    }
}

impl Node for StillNode {
    const NAME: &'static str = "still";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"shortest":{"type":"number","minimum":0,"default":1},"longest":{"type":"number","exclusiveMinimum":0,"maximum":600,"default":10},"tolerance":{"type":"number","minimum":0,"maximum":255,"default":2}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, bound: &Bound) -> Result<Shape> {
        let frame = bound.rate_of("v").map_or(1.0, |rate| rate.duration(1));
        Ok(Shape::new()
            .input(
                Input::video("v")
                    .clock()
                    .window(2, 1)
                    .pixel_formats(&["yuv420p"]),
            )
            .output(
                Output::rows("stills")
                    .latency(params.longest + frame)
                    .schema::<Still>(),
            ))
    }

    fn init(params: Params, init: &Init) -> Result<StillNode> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is a video input")?;
        Ok(StillNode {
            v: v.id,
            luma: (video.width * video.height) as usize,
            params,
            open: None,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let seconds = |pts| tick.time_base().seconds(pts);
        let frames = tick.frames(self.v);
        let [before, after] = &frames[..] else {
            let end = tick
                .frame(self.v)
                .map_or(tick.seconds(), |last| seconds(last.pts));
            return self.close(end, out);
        };
        let moved = difference(
            &tick.fetch(self.v, before.index)[..self.luma],
            &tick.fetch(self.v, after.index)[..self.luma],
        );
        if moved > self.params.tolerance {
            return self.close(seconds(after.pts), out);
        }
        let (_, start_t) = *self.open.get_or_insert((before.pts, seconds(before.pts)));
        if seconds(after.pts) - start_t >= self.params.longest {
            self.close(seconds(after.pts), out)?;
            self.open = Some((after.pts, seconds(after.pts)));
        }
        Ok(())
    }
}

ffrwd_node::export!(StillNode);
```

The window is two frames sliding one at a time: each tick sees a frame and
the one before it. The node asks for yuv420p, whose first plane is the luma,
a byte a pixel, and compares only that.

At 15 frames a second, with `longest` of 2:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/still.wasm --params '{"longest":2}' --bound '[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]'
```

```json
{
  "format": {"codec": "json", "kind": "data"},
  "kind": "data",
  "latency": 2.066666666666667,
  "name": "stills",
  "row": null,
  "schema": "{\"properties\":{\"end_t\":{\"type\":\"number\"},\"start_t\":{\"type\":\"number\"}},\"required\":[\"end_t\",\"start_t\"],\"type\":\"object\"}",
  "time_base": null
}
```

```sql
CREATE FUNCTION still(v video_stream, shortest number DEFAULT 1, longest number DEFAULT 10,
                      tolerance number DEFAULT 2)
RETURNS STRUCT(start_t number, end_t number)[]
  AS 'still.wasm', 'still' LANGUAGE wasm;

COPY (
  SELECT still(f.video[1], longest => 2)
  FROM input('smptebars.mp4') f
) TO 'stills.ndjson'
```

```
$ ffrwd compile -f still.sql
ffmpeg -i smptebars.mp4 -map 0:v:0 -c:0 rawvideo -pix_fmt:0 yuv420p -f nut pipe:1 | \
  ffrwd-wasm -f nut -i pipe:0 -pad \
  '{"color": {"range": "tv", "primaries": "unknown", "trc": "unknown", "space": "bt470bg"}}' \
  -m still=still.wasm -filter_complex \
  '[v=0:v]still=shortest=1:longest=2:tolerance=2[stills=out0]' -bound \
  'still=[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]' -map '[out0]' -f \
  ndjson stills.ndjson
```

```
$ ffrwd explain --delays -f still.sql
still: sliding 0.133 s
stills.ndjson (rows): 2.2 s behind the source
```

The bars of `smptebars.mp4` never move, so the stretches are cut at two
seconds:

```ndjson
{"start_t":0.0,"end_t":2.0,"pts":0,"time":0.0}
{"start_t":2.0,"end_t":3.933333333333333,"pts":122880,"time":2.0}
```

`still` keeps the open stretch from tick to tick, so it is not pure, like
the first `glow`. A node that has to remember what it saw runs as one
instance.
