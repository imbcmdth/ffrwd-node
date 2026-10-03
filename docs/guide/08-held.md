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

**C++**

```cpp
#include <algorithm>
#include <optional>
#include <string>

#include "ffrwd/node.hpp"

/// The tag a feeder puts on its stream to say its pts are programme time.
constexpr std::string_view TIMED = "smart_timed";

struct Params {
    double lead;
    double linger;
    double timeout;
    FFRWD_FIELDS(lead, linger, timeout)
};

/// One change in what the host says of the feed, or a note the feeder
/// wrote beside its picture.
struct Presence {
    std::string event;
    double t = 0.0;
    double at = 0.0;
    std::optional<std::string> text;
    FFRWD_FIELDS(event, t, at, text)
};

/// What a feeder writes beside its picture.
struct Note {
    std::string text;
    FFRWD_FIELDS(text)
};

struct Cutin : ffrwd::Node<Cutin, Params> {
    static constexpr std::string_view name = "cutin";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"port":{"type":"integer","minimum":1,"maximum":65535,"default":9000},"lead":{"type":"number","minimum":0,"maximum":60,"default":0.5},"linger":{"type":"number","minimum":0,"maximum":60,"default":0},"timeout":{"type":"number","minimum":0,"maximum":60,"default":1}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::size_t width = 0;
    std::optional<std::uint32_t> feed;
    std::optional<std::uint32_t> notes;
    /// One frame of the programme, in its time base.
    std::int64_t step = 1;

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound&) {
        auto feed = ffrwd::Input::video("feed")
                        .optional()
                        .hold()
                        .anchor(ffrwd::Anchor::tagged(std::string(TIMED)))
                        .lead(params.lead)
                        .group("cam")
                        .port_param("port")
                        .like("v")
                        .pixel_formats({"rgba"});
        if (params.linger > 0.0) feed.linger(params.linger);
        if (params.timeout > 0.0) feed.timeout(params.timeout);
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .input(std::move(feed))
            .input(ffrwd::Input::rows("notes").optional().interval().group("cam").schema<Note>())
            .output(ffrwd::Output::like("v"))
            .output(ffrwd::Output::rows("presence").schema<Presence>())
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<Cutin> init(Params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        Cutin node;
        node.v = v.id;
        node.width = video->width;
        if (const ffrwd::BoundStream* feed = init.optional("feed")) node.feed = feed->id;
        if (const ffrwd::BoundStream* notes = init.optional("notes")) node.notes = notes->id;
        if (v.hint.rate)
            node.step = std::max<std::int64_t>(v.info.time_base.pts(v.hint.rate->duration(1)), 1);
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        ffrwd::Rational clock = tick.time_base();
        std::int64_t pts = frame->pts, step = std::max<std::int64_t>(frame->duration.value_or(this->step), 1);
        auto say = [&](std::string_view event, std::int64_t at) {
            Presence row{std::string(event), clock.seconds(pts), clock.seconds(at), std::nullopt};
            return out.row("presence", pts, row);
        };
        std::optional<double> countdown;
        if (feed) {
            if (auto current = tick.feed(*feed)) {
                const ffrwd::FeedStart& start = current->start;
                if (start.known == pts && start.known < start.at) FFRWD_TRY(say("coming", start.at));
                if (pts <= start.at && start.at < pts + step) FFRWD_TRY(say("on", start.at));
                if (pts < start.at) countdown = double(start.at - pts) / double(start.at - start.known);
            }
            for (const ffrwd::Feed& ended : tick.ended_feeds(*feed)) {
                if (ended.ends && *ended.ends < pts && pts - step <= *ended.ends)
                    FFRWD_TRY(say("off", *ended.ends + step));
            }
        }
        if (notes) {
            ffrwd::Rational base = tick.info(*notes).time_base;
            for (const ffrwd::Message& message : tick.messages(*notes)) {
                std::int64_t at = std::max(base.rescale(message.pts, clock), pts);
                FFRWD_LET(note, message.row<Note>());
                Presence row{"note", clock.seconds(pts), clock.seconds(at), note.text};
                FFRWD_TRY(out.row("presence", at, row));
            }
        }
        if (feed) {
            if (auto shown = tick.frame(*feed))
                return out.same("v", pts, frame->duration, *feed, shown->index);
        }
        if (!countdown) return out.pass("v", v, *frame);
        double left = *countdown;
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        auto bar = std::size_t(double(width) * left);
        std::size_t rows = pixels.size() / (width * 4);
        for (std::size_t row = rows - std::min<std::size_t>(rows, 8); row < rows; ++row)
            for (std::size_t x = 0; x < bar; ++x) {
                std::uint8_t* pixel = pixels.data() + (row * width + x) * 4;
                pixel[0] = 220, pixel[1] = 40, pixel[2] = 40, pixel[3] = 255;
            }
        return out.frame("v", pts, frame->duration, std::move(pixels));
    }
};

FFRWD_EXPORT(Cutin);
```

**JavaScript**

```js
import { Anchor, defineNode, Input, Output, parse, Shape } from '@ffrwd/node';

/** The tag a feeder puts on its stream to say its pts are programme time. */
const TIMED = 'smart_timed';

/** One change in what the host says of the feed, or a note the feeder
 * wrote beside its picture. */
const PRESENCE = { event: 'string', t: 'number', at: 'number', text: null };

/** What a feeder writes beside its picture. */
const NOTE = { text: 'string' };

export const node = defineNode({
  name: 'cutin',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"port":{"type":"integer","minimum":1,"maximum":65535,"default":9000},' +
    '"lead":{"type":"number","minimum":0,"maximum":60,"default":0.5},' +
    '"linger":{"type":"number","minimum":0,"maximum":60,"default":0},' +
    '"timeout":{"type":"number","minimum":0,"maximum":60,"default":1}},"additionalProperties":false}',

  shape({ lead, linger, timeout }) {
    const feed = Input.video('feed')
      .optional()
      .hold()
      .anchor(Anchor.tagged(TIMED))
      .lead(lead)
      .group('cam')
      .portParam('port')
      .like('v')
      .pixelFormats(['rgba']);
    if (linger > 0) feed.linger(linger);
    if (timeout > 0) feed.timeout(timeout);
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .input(feed)
      .input(Input.rows('notes').optional().interval().group('cam').schema(NOTE))
      .output(Output.like('v'))
      .output(Output.rows('presence').schema(PRESENCE))
      .pure()
      .oneToOne();
  },

  init(_, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    const width = video.width;
    const feed = init.optional('feed')?.id;
    const notes = init.optional('notes')?.id;
    // One frame of the programme, in its time base.
    const frameStep = v.hint.rate === undefined ? 1 : Math.max(v.info.timeBase.pts(v.hint.rate.duration(1)), 1);
    return {
      process(tick, out) {
        const frame = tick.frame(v.id);
        if (frame === undefined) return;
        const clock = tick.timeBase();
        const [pts, step] = [frame.pts, Math.max(frame.duration ?? frameStep, 1)];
        const say = (event, at) =>
          out.row('presence', pts, { event, t: clock.seconds(pts), at: clock.seconds(at), text: null });
        let countdown;
        if (feed !== undefined) {
          const current = tick.feed(feed);
          if (current !== undefined) {
            const start = current.start;
            if (start.known === pts && start.known < start.at) say('coming', start.at);
            if (pts <= start.at && start.at < pts + step) say('on', start.at);
            if (pts < start.at) countdown = (start.at - pts) / (start.at - start.known);
          }
          for (const ended of tick.endedFeeds(feed)) {
            if (ended.ends !== undefined && ended.ends < pts && pts - step <= ended.ends) {
              say('off', ended.ends + step);
            }
          }
        }
        if (notes !== undefined) {
          const base = tick.info(notes).timeBase;
          for (const message of tick.messages(notes)) {
            const at = Math.max(base.rescale(message.pts, clock), pts);
            const { text } = parse(new TextDecoder().decode(message.data));
            out.row('presence', at, { event: 'note', t: clock.seconds(pts), at: clock.seconds(at), text });
          }
        }
        const shown = feed === undefined ? undefined : tick.frame(feed);
        if (shown !== undefined) return out.same('v', pts, frame.duration, feed, shown.index);
        if (countdown === undefined) return out.pass('v', v.id, frame);
        const pixels = tick.fetch(v.id, frame.index);
        const bar = Math.trunc(width * countdown);
        const rows = Math.floor(pixels.length / (width * 4));
        for (let row = Math.max(rows - 8, 0); row < rows; row += 1) {
          for (let x = 0; x < bar; x += 1) pixels.set([220, 40, 40, 255], (row * width + x) * 4);
        }
        out.frame('v', pts, frame.duration, pixels);
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

// timed is the tag a feeder puts on its stream to say its pts are programme
// time.
const timed = "smart_timed"

type Params struct {
	Lead    float64 `json:"lead"`
	Linger  float64 `json:"linger"`
	Timeout float64 `json:"timeout"`
}

// Presence is one change in what the host says of the feed, or a note the
// feeder wrote beside its picture.
type Presence struct {
	Event string  `json:"event"`
	T     float64 `json:"t"`
	At    float64 `json:"at"`
	Text  *string `json:"text"`
}

// Note is what a feeder writes beside its picture.
type Note struct {
	Text string `json:"text"`
}

type Cutin struct {
	v     uint32
	width int
	feed  *uint32
	notes *uint32
	// One frame of the programme, in its time base.
	step int64
}

var Definition = node.Definition[Params]{
	Name:         "cutin",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"port":{"type":"integer","minimum":1,"maximum":65535,"default":9000},"lead":{"type":"number","minimum":0,"maximum":60,"default":0.5},"linger":{"type":"number","minimum":0,"maximum":60,"default":0},"timeout":{"type":"number","minimum":0,"maximum":60,"default":1}},"additionalProperties":false}`,
	Shape: func(params Params, _ *node.Bound) (node.Shape, error) {
		feed := node.VideoInput("feed").
			Optional().
			Hold().
			Anchor(node.Tagged(timed)).
			Lead(params.Lead).
			Group("cam").
			PortParam("port").
			Like("v").
			PixelFormats("rgba")
		if params.Linger > 0 {
			feed = feed.Linger(params.Linger)
		}
		if params.Timeout > 0 {
			feed = feed.Timeout(params.Timeout)
		}
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Input(feed).
			Input(node.RowsInput("notes").
				Optional().
				Interval().
				Group("cam").
				Schema(node.SchemaOf[Note]())).
			Output(node.LikeOutput("v")).
			Output(node.RowsOutput("presence").Schema(node.SchemaOf[Presence]())).
			Pure().
			OneToOne(), nil
	},
	Init: func(_ Params, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		video := v.VideoFormat()
		if video == nil {
			return nil, errors.New("`v` is a video input")
		}
		step := int64(1)
		if rate := v.Hint.Rate; rate != nil {
			step = max(v.Info.TimeBase.Pts(rate.Duration(1)), 1)
		}
		cutin := &Cutin{v: v.ID, width: int(video.Width), step: step}
		if feed := init.Optional("feed"); feed != nil {
			cutin.feed = &feed.ID
		}
		if notes := init.Optional("notes"); notes != nil {
			cutin.notes = &notes.ID
		}
		return cutin, nil
	},
}

func (c *Cutin) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(c.v)
	if !ok {
		return nil
	}
	clock := tick.TimeBase()
	pts, step := frame.Pts, c.step
	if frame.Duration != nil {
		step = *frame.Duration
	}
	step = max(step, 1)
	say := func(event string, at int64) error {
		row := Presence{
			Event: event,
			T:     clock.Seconds(pts),
			At:    clock.Seconds(at),
		}
		return out.Row("presence", pts, row)
	}
	var countdown *float64
	if c.feed != nil {
		if current := tick.Feed(*c.feed); current != nil {
			start := current.Start
			if start.Known == pts && start.Known < start.At {
				if err := say("coming", start.At); err != nil {
					return err
				}
			}
			if pts <= start.At && start.At < pts+step {
				if err := say("on", start.At); err != nil {
					return err
				}
			}
			if pts < start.At {
				left := float64(start.At-pts) / float64(start.At-start.Known)
				countdown = &left
			}
		}
		for _, ended := range tick.EndedFeeds(*c.feed) {
			if ends := ended.Ends; ends != nil && *ends < pts && pts-step <= *ends {
				if err := say("off", *ends+step); err != nil {
					return err
				}
			}
		}
	}
	if c.notes != nil {
		base := tick.Info(*c.notes).TimeBase
		for _, message := range tick.Messages(*c.notes) {
			at := max(base.Rescale(message.Pts, clock), pts)
			var note Note
			if err := message.Decode(&note); err != nil {
				return err
			}
			row := Presence{
				Event: "note",
				T:     clock.Seconds(pts),
				At:    clock.Seconds(at),
				Text:  &note.Text,
			}
			if err := out.Row("presence", at, row); err != nil {
				return err
			}
		}
	}
	if c.feed != nil {
		if shown, ok := tick.Frame(*c.feed); ok {
			return out.Same("v", pts, frame.Duration, *c.feed, shown.Index)
		}
	}
	if countdown == nil {
		return out.Pass("v", c.v, frame)
	}
	pixels := tick.Fetch(c.v, frame.Index)
	bar := int(float64(c.width) * *countdown)
	rows := len(pixels) / (c.width * 4)
	for n := 0; n < 8 && n < rows; n++ {
		row := pixels[(rows-1-n)*c.width*4:]
		for at := 0; at < bar*4; at += 4 {
			copy(row[at:at+4], []byte{220, 40, 40, 255})
		}
	}
	return out.Frame("v", pts, frame.Duration, pixels)
}

func init() { node.Export(Definition) }

func main() {}
```

The feed and the notes, as the shape has them:

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/cutin.wasm --params '{"port":9100}' --bound '[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]'
```

**C++**

```
$ ffrwd-wasm --shape build/cutin.wasm --params '{"port":9100}' --bound '[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]'
```

**JavaScript**

```
$ ffrwd-wasm --shape build/cutin.wasm --params '{"port":9100}' --bound '[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]'
```

**Go**

```
$ ffrwd-wasm --shape build/cutin.wasm --params '{"port":9100}' --bound '[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]'
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

**C++**

```cpp
#include <algorithm>
#include <array>
#include <vector>

#include "ffrwd/node.hpp"
#include "resize.hpp"

constexpr std::array<std::uint8_t, 4> DOWN{48, 48, 48, 255};

struct Params {
    std::size_t columns;
    std::uint32_t width;
    std::uint32_t height;
    FFRWD_FIELDS(columns, width, height)
};

struct Tile {
    std::uint32_t id = 0;
    std::size_t width = 0;
    std::size_t height = 0;
    resize::Rect cell;
};

struct Mosaic : ffrwd::Node<Mosaic, Params> {
    static constexpr std::string_view name = "mosaic";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"columns":{"type":"integer","minimum":1,"default":2},"width":{"type":"integer","minimum":16,"default":1280},"height":{"type":"integer","minimum":16,"default":720}},"additionalProperties":false})";

    std::vector<Tile> tiles;
    std::size_t width = 0;
    std::size_t height = 0;

    /// `pixels`, a `tile`'s picture, resized into its cell of `canvas`.
    ffrwd::Status put(ffrwd::Bytes& canvas, const Tile& tile, const ffrwd::Bytes& pixels) const {
        if (auto wrong = resize::misfit(pixels.size(), tile.width, tile.height)) return ffrwd::fail(*wrong);
        std::size_t w = tile.cell.width(), h = tile.cell.height();
        auto whole = resize::Rect::whole(tile.width, tile.height);
        auto rgb = resize::bilinear(pixels.data(), tile.width, tile.height, whole, w, h);
        for (std::size_t y = 0; y < h; ++y)
            for (std::size_t x = 0; x < w; ++x) {
                std::size_t at = ((tile.cell.y0 + y) * width + tile.cell.x0 + x) * 4;
                for (std::size_t channel = 0; channel < 3; ++channel)
                    canvas[at + channel] = rgb[(y * w + x) * 3 + channel];
            }
        return {};
    }

    void fill(ffrwd::Bytes& canvas, resize::Rect cell, std::array<std::uint8_t, 4> colour) const {
        for (std::size_t y = cell.y0; y < cell.y1; ++y)
            for (std::size_t x = cell.x0; x < cell.x1; ++x)
                std::copy(colour.begin(), colour.end(), canvas.data() + (y * width + x) * 4);
    }

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v")
                       .many()
                       .hold()
                       .anchor(ffrwd::Anchor::shared_clock())
                       .pixel_formats({"rgba"}))
            .output(ffrwd::Output::video("v").size(params.width, params.height).pixel_format("rgba"))
            .rate_of("v")
            .pure();
    }

    static ffrwd::Result<Mosaic> init(Params params, const ffrwd::Init& init) {
        std::size_t width = params.width, height = params.height;
        auto streams = init.streams("v");
        std::size_t columns = std::max<std::size_t>(std::min(params.columns, streams.size()), 1);
        std::size_t rows = std::max<std::size_t>((streams.size() + columns - 1) / columns, 1);
        Mosaic node;
        for (std::size_t n = 0; n < streams.size(); ++n) {
            const ffrwd::VideoFormat* video = streams[n]->video_format();
            if (!video) return ffrwd::fail("`v` takes pictures");
            std::size_t column = n % columns, row = n / columns;
            node.tiles.push_back(Tile{
                streams[n]->id,
                video->width,
                video->height,
                {column * width / columns, row * height / rows, (column + 1) * width / columns,
                 (row + 1) * height / rows},
            });
        }
        node.width = width;
        node.height = height;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        ffrwd::Bytes canvas(width * height * 4);
        for (std::size_t at = 3; at < canvas.size(); at += 4) canvas[at] = 255;
        bool shown = false;
        for (const Tile& tile : tiles) {
            if (auto frame = tick.frame(tile.id)) {
                FFRWD_TRY(put(canvas, tile, tick.fetch(tile.id, frame->index)));
                shown = true;
            } else if (!tick.feed(tile.id)) {
                fill(canvas, tile.cell, DOWN);
            }
        }
        if (!shown && tick.last()) return {};
        return out.frame("v", tick.pts(), 1, std::move(canvas));
    }
};

FFRWD_EXPORT(Mosaic);
```

**JavaScript**

```js
import { Anchor, defineNode, Input, Output, Shape } from '@ffrwd/node';

const DOWN = [48, 48, 48, 255];

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

/** Every pixel of `cell` of a `width`-wide canvas set to `colour`. */
function fill(canvas, width, cell, colour) {
  const row = new Uint8Array((cell.x1 - cell.x0) * 4);
  for (let at = 0; at < row.length; at += 4) row.set(colour, at);
  for (let y = cell.y0; y < cell.y1; y += 1) canvas.set(row, (y * width + cell.x0) * 4);
}

/** `pixels`, a `tile`'s picture, resized into its cell of a `width`-wide
 * canvas. */
function put(canvas, width, tile, pixels) {
  const { cell } = tile;
  const [w, h] = [cell.x1 - cell.x0, cell.y1 - cell.y0];
  const whole = { x0: 0, y0: 0, x1: tile.width, y1: tile.height };
  const rgb = resize(pixels, tile.width, whole, w, h);
  for (let y = 0; y < h; y += 1) {
    for (let x = 0; x < w; x += 1) {
      const [from, to] = [(y * w + x) * 3, ((cell.y0 + y) * width + cell.x0 + x) * 4];
      for (let channel = 0; channel < 3; channel += 1) canvas[to + channel] = rgb[from + channel];
    }
  }
}

export const node = defineNode({
  name: 'mosaic',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"columns":{"type":"integer","minimum":1,"default":2},' +
    '"width":{"type":"integer","minimum":16,"default":1280},"height":{"type":"integer","minimum":16,"default":720}},' +
    '"additionalProperties":false}',

  shape({ width, height }) {
    return new Shape()
      .input(Input.video('v').many().hold().anchor(Anchor.sharedClock).pixelFormats(['rgba']))
      .output(Output.video('v').size(width, height).pixelFormat('rgba'))
      .rateOf('v')
      .pure();
  },

  init({ columns, width, height }, init) {
    const streams = init.streams('v');
    columns = Math.max(Math.min(columns, streams.length), 1);
    const rows = Math.max(Math.ceil(streams.length / columns), 1);
    const tiles = streams.map((stream, n) => {
      const video = stream.videoFormat();
      if (video === undefined) throw new Error('`v` takes pictures');
      const [column, row] = [n % columns, Math.floor(n / columns)];
      return {
        id: stream.id,
        width: video.width,
        height: video.height,
        cell: {
          x0: Math.floor((column * width) / columns),
          y0: Math.floor((row * height) / rows),
          x1: Math.floor(((column + 1) * width) / columns),
          y1: Math.floor(((row + 1) * height) / rows),
        },
      };
    });
    const whole = { x0: 0, y0: 0, x1: width, y1: height };
    return {
      process(tick, out) {
        const canvas = new Uint8Array(width * height * 4);
        fill(canvas, width, whole, [0, 0, 0, 255]);
        let shown = false;
        for (const tile of tiles) {
          const frame = tick.frame(tile.id);
          if (frame !== undefined) {
            put(canvas, width, tile, tick.fetch(tile.id, frame.index));
            shown = true;
          } else if (tick.feed(tile.id) === undefined) {
            fill(canvas, width, tile.cell, DOWN);
          }
        }
        if (!shown && tick.last()) return;
        out.frame('v', tick.pts(), 1, canvas);
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
	"errors"

	node "github.com/imbcmdth/ffrwd-node/go"
)

var down = [4]byte{48, 48, 48, 255}

type Params struct {
	Columns int    `json:"columns"`
	Width   uint32 `json:"width"`
	Height  uint32 `json:"height"`
}

type Tile struct {
	id     uint32
	width  int
	height int
	cell   Rect
}

type Mosaic struct {
	tiles  []Tile
	width  int
	height int
}

// put is pixels, a tile's picture, resized into its cell of canvas.
func (m *Mosaic) put(canvas []byte, tile Tile, pixels []byte) error {
	w, h := tile.cell.Width(), tile.cell.Height()
	whole := Whole(tile.width, tile.height)
	rgb, err := resize(pixels, tile.width, tile.height, whole, w, h)
	if err != nil {
		return err
	}
	for y := range h {
		for x := range w {
			at := ((tile.cell.Y0+y)*m.width + tile.cell.X0 + x) * 4
			copy(canvas[at:at+3], rgb[(y*w+x)*3:])
		}
	}
	return nil
}

func (m *Mosaic) fill(canvas []byte, cell Rect, colour [4]byte) {
	for y := cell.Y0; y < cell.Y1; y++ {
		row := canvas[(y*m.width+cell.X0)*4 : (y*m.width+cell.X1)*4]
		for at := 0; at < len(row); at += 4 {
			copy(row[at:at+4], colour[:])
		}
	}
}

var Definition = node.Definition[Params]{
	Name:         "mosaic",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"columns":{"type":"integer","minimum":1,"default":2},"width":{"type":"integer","minimum":16,"default":1280},"height":{"type":"integer","minimum":16,"default":720}},"additionalProperties":false}`,
	Shape: func(params Params, _ *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").
				Many().
				Hold().
				Anchor(node.SharedClock).
				PixelFormats("rgba")).
			Output(node.VideoOutput("v").
				Size(params.Width, params.Height).
				PixelFormat("rgba")).
			RateOf("v").
			Pure(), nil
	},
	Init: func(params Params, init *node.Init) (node.Instance, error) {
		width, height := int(params.Width), int(params.Height)
		streams := init.Streams("v")
		columns := max(min(params.Columns, len(streams)), 1)
		rows := max((len(streams)+columns-1)/columns, 1)
		var tiles []Tile
		for n, stream := range streams {
			video := stream.VideoFormat()
			if video == nil {
				return nil, errors.New("`v` takes pictures")
			}
			column, row := n%columns, n/columns
			tiles = append(tiles, Tile{
				id:     stream.ID,
				width:  int(video.Width),
				height: int(video.Height),
				cell: Rect{
					X0: column * width / columns,
					Y0: row * height / rows,
					X1: (column + 1) * width / columns,
					Y1: (row + 1) * height / rows,
				},
			})
		}
		return &Mosaic{tiles: tiles, width: width, height: height}, nil
	},
}

func (m *Mosaic) Process(tick *node.Tick, out *node.Out) error {
	canvas := bytes.Repeat([]byte{0, 0, 0, 255}, m.width*m.height)
	shown := false
	for _, tile := range m.tiles {
		frame, ok := tick.Frame(tile.id)
		switch {
		case ok:
			if err := m.put(canvas, tile, tick.Fetch(tile.id, frame.Index)); err != nil {
				return err
			}
			shown = true
		case tick.Feed(tile.id) == nil:
			m.fill(canvas, tile.cell, down)
		}
	}
	if !shown && tick.Last() {
		return nil
	}
	one := int64(1)
	return out.Frame("v", tick.Pts(), &one, canvas)
}

func init() { node.Export(Definition) }

func main() {}
```

A port that takes many streams cannot be the clock, so the node ticks at a
rate, here the rate of `v`'s first stream, which the compiler reads off it:

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/mosaic.wasm --params '{"columns":3,"width":960,"height":240}' --bound v,v,v
```

**C++**

```
$ ffrwd-wasm --shape build/mosaic.wasm --params '{"columns":3,"width":960,"height":240}' --bound v,v,v
```

**JavaScript**

```
$ ffrwd-wasm --shape build/mosaic.wasm --params '{"columns":3,"width":960,"height":240}' --bound v,v,v
```

**Go**

```
$ ffrwd-wasm --shape build/mosaic.wasm --params '{"columns":3,"width":960,"height":240}' --bound v,v,v
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
