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

**C++**

```cpp
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "ffrwd/node.hpp"

struct Params {
    double window;
    std::optional<double> hop;
    FFRWD_FIELDS(window, hop)
};

/// How loud `samples` are, as a cue's text: their RMS in dB of full scale.
std::string loudness(const std::vector<float>& samples) {
    double power = 0.0;
    for (float sample : samples) power += double(sample) * double(sample);
    power /= double(std::max<std::size_t>(samples.size(), 1));
    double db = 10.0 * std::log10(power);
    if (db < -90.0) return "silence";
    char text[32];
    std::snprintf(text, sizeof text, "%.0f dB", db);
    return text;
}

struct Level : ffrwd::Node<Level, Params> {
    static constexpr std::string_view name = "level";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"window":{"type":"number","exclusiveMinimum":0,"maximum":60,"default":2},"hop":{"type":["number","null"],"exclusiveMinimum":0,"maximum":60}},"additionalProperties":false})";

    std::uint32_t a = 0;
    std::size_t channels = 0;
    double rate = 0.0;

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound& bound) {
        auto rate = bound.rate_of("a");
        if (!rate)
            return ffrwd::fail("level counts its window in samples, and the call gives `a` no sample rate");
        auto window = std::uint32_t(rate->count(params.window));
        auto stride = std::uint32_t(rate->count(params.hop.value_or(params.window)));
        return ffrwd::Shape()
            .input(ffrwd::Input::audio("a").clock().window(window, stride).sample_formats({"f32"}))
            .output(ffrwd::Output::rows("cues").schema<ffrwd::Cue>())
            .pure();
    }

    static ffrwd::Result<Level> init(Params, const ffrwd::Init& init) {
        FFRWD_LET(a, init.stream("a"));
        const ffrwd::AudioFormat* audio = a.audio_format();
        if (!audio) return ffrwd::fail("`a` is an audio input");
        Level node;
        node.a = a.id;
        node.channels = std::max<std::uint32_t>(audio->channels, 1);
        node.rate = audio->sample_rate;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto run = tick.frame(a);
        if (!run) return {};
        ffrwd::Bytes bytes = tick.fetch(a, run->index);
        std::vector<float> samples(bytes.size() / 4);
        std::memcpy(samples.data(), bytes.data(), samples.size() * 4);
        double start = tick.time_base().seconds(run->pts);
        double end = start + double(samples.size() / channels) / rate;
        return out.row("cues", run->pts, ffrwd::Cue{start, end, loudness(samples)});
    }
};

FFRWD_EXPORT(Level);
```

**JavaScript**

```js
import { CUE, Cue, defineNode, Input, Output, Shape } from '@ffrwd/node';

/** How loud `samples` are, as a cue's text: their RMS in dB of full scale. */
function loudness(samples) {
  const power = samples.reduce((sum, sample) => sum + sample * sample, 0) / Math.max(samples.length, 1);
  const db = 10 * Math.log10(power);
  return db < -90 ? 'silence' : `${db.toFixed(0)} dB`;
}

export const node = defineNode({
  name: 'level',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"window":{"type":"number","exclusiveMinimum":0,"maximum":60,"default":2},' +
    '"hop":{"type":["number","null"],"exclusiveMinimum":0,"maximum":60}},"additionalProperties":false}',

  shape(params, bound) {
    const rate = bound.rateOf('a');
    if (rate === undefined) {
      throw new Error('level counts its window in samples, and the call gives `a` no sample rate');
    }
    const window = rate.count(params.window);
    const stride = rate.count(params.hop ?? params.window);
    return new Shape()
      .input(Input.audio('a').clock().window(window, stride).sampleFormats(['f32']))
      .output(Output.rows('cues').schema(CUE))
      .pure();
  },

  init(_, init) {
    const a = init.stream('a');
    const audio = a.audioFormat();
    if (audio === undefined) throw new Error('`a` is an audio input');
    const channels = Math.max(audio.channels, 1);
    const rate = audio.sampleRate;
    return {
      process(tick, out) {
        const run = tick.frame(a.id);
        if (run === undefined) return;
        const bytes = tick.fetch(a.id, run.index);
        const samples = new Float32Array(bytes.slice(0, bytes.length - (bytes.length % 4)).buffer);
        const start = tick.timeBase().seconds(run.pts);
        const end = start + Math.floor(samples.length / channels) / rate;
        const cue = new Cue(start, end, loudness(samples));
        out.row('cues', run.pts, cue);
      },
    };
  },
});
```

**Go**

```go
package main

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math"

	node "github.com/imbcmdth/ffrwd-node/go"
)

type Params struct {
	Window float64  `json:"window"`
	Hop    *float64 `json:"hop"`
}

type Level struct {
	a        uint32
	channels int
	rate     float64
}

// loudness is how loud samples are, as a cue's text: their RMS in dB of
// full scale.
func loudness(samples []float32) string {
	power := 0.0
	for _, sample := range samples {
		power += float64(sample) * float64(sample)
	}
	power /= float64(max(len(samples), 1))
	db := 10 * math.Log10(power)
	if db < -90 {
		return "silence"
	}
	return fmt.Sprintf("%.0f dB", db)
}

var Definition = node.Definition[Params]{
	Name:         "level",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"window":{"type":"number","exclusiveMinimum":0,"maximum":60,"default":2},"hop":{"type":["number","null"],"exclusiveMinimum":0,"maximum":60}},"additionalProperties":false}`,
	Shape: func(params Params, bound *node.Bound) (node.Shape, error) {
		rate, ok := bound.RateOf("a")
		if !ok {
			return node.Shape{}, errors.New("level counts its window in samples, and the call gives `a` no sample rate")
		}
		hop := params.Window
		if params.Hop != nil {
			hop = *params.Hop
		}
		window := uint32(rate.Count(params.Window))
		stride := uint32(rate.Count(hop))
		return node.NewShape().
			Input(node.AudioInput("a").
				Clock().
				Window(window, stride).
				SampleFormats("f32")).
			Output(node.RowsOutput("cues").Schema(node.SchemaOf[node.Cue]())).
			Pure(), nil
	},
	Init: func(_ Params, init *node.Init) (node.Instance, error) {
		a, err := init.Stream("a")
		if err != nil {
			return nil, err
		}
		audio := a.AudioFormat()
		if audio == nil {
			return nil, errors.New("`a` is an audio input")
		}
		return &Level{a: a.ID, channels: int(max(audio.Channels, 1)), rate: float64(audio.SampleRate)}, nil
	},
}

func (l *Level) Process(tick *node.Tick, out *node.Out) error {
	run, ok := tick.Frame(l.a)
	if !ok {
		return nil
	}
	bytes := tick.Fetch(l.a, run.Index)
	samples := make([]float32, len(bytes)/4)
	for n := range samples {
		samples[n] = math.Float32frombits(binary.LittleEndian.Uint32(bytes[n*4:]))
	}
	start := tick.TimeBase().Seconds(run.Pts)
	end := start + float64(len(samples)/l.channels)/l.rate
	cue := node.Cue{StartT: start, EndT: end, Text: loudness(samples)}
	return out.Row("cues", run.Pts, cue)
}

func init() { node.Export(Definition) }

func main() {}
```

At 44.1 kHz, a window of one second hopping every half second is:

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/level.wasm --params '{"window":1,"hop":0.5}' --bound '[{"input":"a","streams":[{"rate":{"num":44100,"den":1}}]}]'
```

**C++**

```
$ ffrwd-wasm --shape build/level.wasm --params '{"window":1,"hop":0.5}' --bound '[{"input":"a","streams":[{"rate":{"num":44100,"den":1}}]}]'
```

**JavaScript**

```
$ ffrwd-wasm --shape build/level.wasm --params '{"window":1,"hop":0.5}' --bound '[{"input":"a","streams":[{"rate":{"num":44100,"den":1}}]}]'
```

**Go**

```
$ ffrwd-wasm --shape build/level.wasm --params '{"window":1,"hop":0.5}' --bound '[{"input":"a","streams":[{"rate":{"num":44100,"den":1}}]}]'
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

**C++**

```cpp
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <utility>

#include "ffrwd/node.hpp"

struct Params {
    double shortest;
    double longest;
    double tolerance;
    FFRWD_FIELDS(shortest, longest, tolerance)
};

struct Still {
    double start_t = 0.0;
    double end_t = 0.0;
    FFRWD_FIELDS(start_t, end_t)
};

/// How far apart two pictures' luma planes are: the mean difference of a
/// pixel.
double difference(const std::uint8_t* a, const std::uint8_t* b, std::size_t size) {
    std::uint64_t total = 0;
    for (std::size_t at = 0; at < size; ++at) total += std::uint64_t(std::abs(int(a[at]) - int(b[at])));
    return double(total) / double(std::max<std::size_t>(size, 1));
}

struct StillNode : ffrwd::Node<StillNode, Params> {
    static constexpr std::string_view name = "still";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"shortest":{"type":"number","minimum":0,"default":1},"longest":{"type":"number","exclusiveMinimum":0,"maximum":600,"default":10},"tolerance":{"type":"number","minimum":0,"maximum":255,"default":2}},"additionalProperties":false})";

    std::uint32_t v = 0;
    /// The bytes of a picture's luma plane, which come first in yuv420p.
    std::size_t luma = 0;
    Params params;
    /// Where the stretch the picture is still in began: its pts and seconds.
    std::optional<std::pair<std::int64_t, double>> open;

    /// Writes the open stretch as ending at `end_t`, if it lasted long enough.
    ffrwd::Status close(double end_t, ffrwd::Out& out) {
        auto stretch = std::exchange(open, std::nullopt);
        if (stretch && end_t - stretch->second >= params.shortest)
            return out.row("stills", stretch->first, Still{stretch->second, end_t});
        return {};
    }

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound& bound) {
        auto rate = bound.rate_of("v");
        double frame = rate ? rate->duration(1) : 1.0;
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().window(2, 1).pixel_formats({"yuv420p"}))
            .output(ffrwd::Output::rows("stills").latency(params.longest + frame).schema<Still>());
    }

    static ffrwd::Result<StillNode> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        StillNode node;
        node.v = v.id;
        node.luma = std::size_t(video->width) * video->height;
        node.params = params;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto seconds = [&](std::int64_t pts) { return tick.time_base().seconds(pts); };
        auto frames = tick.frames(v);
        if (frames.size() != 2) {
            auto last = tick.frame(v);
            return close(last ? seconds(last->pts) : tick.seconds(), out);
        }
        const ffrwd::Frame& before = frames[0];
        const ffrwd::Frame& after = frames[1];
        ffrwd::Bytes a = tick.fetch(v, before.index);
        ffrwd::Bytes b = tick.fetch(v, after.index);
        if (a.size() < luma || b.size() < luma) return ffrwd::fail("`v` is not a whole yuv420p picture");
        double moved = difference(a.data(), b.data(), luma);
        if (moved > params.tolerance) return close(seconds(after.pts), out);
        if (!open) open = std::pair(before.pts, seconds(before.pts));
        double start_t = open->second;
        if (seconds(after.pts) - start_t >= params.longest) {
            FFRWD_TRY(close(seconds(after.pts), out));
            open = std::pair(after.pts, seconds(after.pts));
        }
        return {};
    }
};

FFRWD_EXPORT(StillNode);
```

**JavaScript**

```js
import { defineNode, Input, Output, Shape } from '@ffrwd/node';

const STILL = { start_t: 'number', end_t: 'number' };

/** How far apart two pictures' luma planes are: the mean difference of a
 * pixel. */
function difference(a, b) {
  let total = 0;
  for (let at = 0; at < a.length; at += 1) total += Math.abs(a[at] - b[at]);
  return total / Math.max(a.length, 1);
}

export const node = defineNode({
  name: 'still',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"shortest":{"type":"number","minimum":0,"default":1},' +
    '"longest":{"type":"number","exclusiveMinimum":0,"maximum":600,"default":10},' +
    '"tolerance":{"type":"number","minimum":0,"maximum":255,"default":2}},"additionalProperties":false}',

  shape({ longest }, bound) {
    const frame = bound.rateOf('v')?.duration(1) ?? 1;
    return new Shape()
      .input(Input.video('v').clock().window(2, 1).pixelFormats(['yuv420p']))
      .output(Output.rows('stills').latency(longest + frame).schema(STILL));
  },

  init({ shortest, longest, tolerance }, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    // The bytes of a picture's luma plane, which come first in yuv420p.
    const luma = video.width * video.height;
    // Where the stretch the picture is still in began: its pts and seconds.
    let open;

    /** Writes the open stretch as ending at `end_t`, if it lasted long enough. */
    function close(end_t, out) {
      if (open === undefined) return;
      const [pts, start_t] = open;
      open = undefined;
      if (end_t - start_t >= shortest) out.row('stills', pts, { start_t, end_t });
    }

    return {
      process(tick, out) {
        const seconds = (pts) => tick.timeBase().seconds(pts);
        const frames = tick.frames(v.id);
        if (frames.length !== 2) {
          const last = tick.frame(v.id);
          return close(last === undefined ? tick.seconds() : seconds(last.pts), out);
        }
        const [before, after] = frames;
        const moved = difference(
          tick.fetch(v.id, before.index).subarray(0, luma),
          tick.fetch(v.id, after.index).subarray(0, luma),
        );
        if (moved > tolerance) return close(seconds(after.pts), out);
        open ??= [before.pts, seconds(before.pts)];
        const [, start_t] = open;
        if (seconds(after.pts) - start_t >= longest) {
          close(seconds(after.pts), out);
          open = [after.pts, seconds(after.pts)];
        }
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

type Params struct {
	Shortest  float64 `json:"shortest"`
	Longest   float64 `json:"longest"`
	Tolerance float64 `json:"tolerance"`
}

type Still struct {
	StartT float64 `json:"start_t"`
	EndT   float64 `json:"end_t"`
}

// opening is where the stretch the picture is still in began: its pts and
// seconds.
type opening struct {
	pts    int64
	startT float64
}

type StillNode struct {
	v uint32
	// The bytes of a picture's luma plane, which come first in yuv420p.
	luma   int
	params Params
	open   *opening
}

// difference is how far apart two pictures' luma planes are: the mean
// difference of a pixel.
func difference(a, b []byte) float64 {
	var total uint64
	for n := range min(len(a), len(b)) {
		if a[n] > b[n] {
			total += uint64(a[n] - b[n])
		} else {
			total += uint64(b[n] - a[n])
		}
	}
	return float64(total) / float64(max(len(a), 1))
}

// close writes the open stretch as ending at endT, if it lasted long
// enough.
func (s *StillNode) close(endT float64, out *node.Out) error {
	open := s.open
	s.open = nil
	if open != nil && endT-open.startT >= s.params.Shortest {
		return out.Row("stills", open.pts, Still{StartT: open.startT, EndT: endT})
	}
	return nil
}

var Definition = node.Definition[Params]{
	Name:         "still",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"shortest":{"type":"number","minimum":0,"default":1},"longest":{"type":"number","exclusiveMinimum":0,"maximum":600,"default":10},"tolerance":{"type":"number","minimum":0,"maximum":255,"default":2}},"additionalProperties":false}`,
	Shape: func(params Params, bound *node.Bound) (node.Shape, error) {
		frame := 1.0
		if rate, ok := bound.RateOf("v"); ok {
			frame = rate.Duration(1)
		}
		return node.NewShape().
			Input(node.VideoInput("v").
				Clock().
				Window(2, 1).
				PixelFormats("yuv420p")).
			Output(node.RowsOutput("stills").
				Latency(params.Longest + frame).
				Schema(node.SchemaOf[Still]())), nil
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
		return &StillNode{v: v.ID, luma: int(video.Width * video.Height), params: params}, nil
	},
}

func (s *StillNode) Process(tick *node.Tick, out *node.Out) error {
	seconds := tick.TimeBase().Seconds
	frames := tick.Frames(s.v)
	if len(frames) != 2 {
		end := tick.Seconds()
		if last, ok := tick.Frame(s.v); ok {
			end = seconds(last.Pts)
		}
		return s.close(end, out)
	}
	before, after := frames[0], frames[1]
	moved := difference(
		tick.Fetch(s.v, before.Index)[:s.luma],
		tick.Fetch(s.v, after.Index)[:s.luma],
	)
	if moved > s.params.Tolerance {
		return s.close(seconds(after.Pts), out)
	}
	if s.open == nil {
		s.open = &opening{before.Pts, seconds(before.Pts)}
	}
	if seconds(after.Pts)-s.open.startT >= s.params.Longest {
		if err := s.close(seconds(after.Pts), out); err != nil {
			return err
		}
		s.open = &opening{after.Pts, seconds(after.Pts)}
	}
	return nil
}

func init() { node.Export(Definition) }

func main() {}
```

The window is two frames sliding one at a time: each tick sees a frame and
the one before it. The node asks for yuv420p, whose first plane is the luma,
a byte a pixel, and compares only that.

At 15 frames a second, with `longest` of 2:

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/still.wasm --params '{"longest":2}' --bound '[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]'
```

**C++**

```
$ ffrwd-wasm --shape build/still.wasm --params '{"longest":2}' --bound '[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]'
```

**JavaScript**

```
$ ffrwd-wasm --shape build/still.wasm --params '{"longest":2}' --bound '[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]'
```

**Go**

```
$ ffrwd-wasm --shape build/still.wasm --params '{"longest":2}' --bound '[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]'
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
