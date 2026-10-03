# 8. Held inputs

A live programme has pictures that come and go: a camera that connects
during the show, a replay that plays and then ends, a wall of feeds where
one feed drops out. A node reads pictures like these through held inputs. A
held input is a picture or sound input that the host, which is the program
that runs the node, keeps lined up with the node's clock. The clock decides
when the node runs, and each run of the node is one tick. At every tick, the
node reads from a held input what to show, and what the host knows about the
source currently connected to the input. This chapter builds `cutin`, a
switch that cuts to a feed while the feed is on, and `mosaic`, a compositor
that lays out any number of pictures in a grid.

## Holding a picture

At each tick, a held input hands the node the newest frame whose time is at
or before the tick's time. The host keeps the source buffered ahead of the
clock. While the source falls behind, the host repeats the last frame. When
the source catches up, the host skips forward. The host reports both the
repeats and the skips. A held input hands nothing before the first frame of
its feed is due to show, and nothing once the feed has ended. A held sound
input hands the tick's samples, or nothing while the source is behind. When
a held sound input hands nothing, the node fills the silence itself.

## The feed

A feed is one source as a held input sees it, from the tick on which the
source's first frame shows to the last tick on which the source's last frame
shows. If a source's pts go backwards, or jump forwards by more than a
second, the current feed ends and the next feed starts. If the clock jumps,
every feed ends.

The input's anchor is the rule that places a source's pts on the clock's
timeline. There are three anchors:

- **Shared clock.** The source's pts are already on the clock's own
  timeline, as the pts of a timed feeder are. A timed feeder is a program
  that sends a picture into the run stamped with the programme's own time.
  The source's first frame waits until the clock reaches that frame's time.
  A first frame whose time the clock has already passed shows at once.
- **First frame.** The source counts its pts from wherever the source
  started. The host holds `lead` seconds of the source, or all of the source
  if the source ends sooner. Then the host schedules the source's first
  frame to show `lead` seconds ahead of the clock, on one of the clock's
  ticks.
- **Tagged.** The input names a tag. A source whose stream tags set that tag
  to `1` uses the shared clock anchor, and any other source uses the first
  frame anchor. ffrwd's own switch uses the tag `smart_timed`.

Three more settings control a feed. `lead` is how many seconds ahead of the
clock a first frame feed is scheduled to start. `linger` keeps the last
frame showing for that many seconds after the source ends. `timeout` gives
up on a live source that has sent nothing for that many seconds of programme
time, and ends the source's feed.

## Feeds by port

A held input may name a param that carries a port number. A query can use
such an input in two ways. If the query binds a stream to the input, the
input reads that stream, and the host writes the port number it picked into
the param. If the query gives only the port number, the input binds no
stream. Instead, the host listens for a TCP connection on that port of the
loopback address, 127.0.0.1, from the moment the run starts. Whatever program connects to the port and writes
NUT with raw video and PCM sound becomes the input's source, for as long as
the program stays connected. Each connection is one feed. If the held input
is declared like another input, the host scales each connection's picture on
the way in to that input's size. The host also converts each connection's
picture to the first pixel format the held input accepts.

## Groups

A group is a set of held inputs that arrive on one connection from one
source, such as the picture and the sound of one feeder. A feeder is a
program that sends pictures and sound into the run. The group's first
picture fixes one offset between source time and clock time, and that offset
applies to every input in the group. The feeds of a group's inputs start and
end on the same tick.

A data input, which carries rows, can also belong to a group. A data input
in a group arrives on the group's connection. The feeder writes a JSON data
stream beside its picture and sound, with pts counted from the same origin
as the picture's pts. Each row lands on the tick on which the picture with
the row's pts shows. A feeder uses this data stream to say something about
what it is sending.

## What the host knows of a feed

A feed's start is fixed when the host settles the offset between the
source's time and the clock's time. At every tick from the tick on which a
feed's start was fixed until the feed's end, a held input has a feed record.
The record holds these fields:

- **at:** the clock time at which the feed's first frame shows.
- **known:** the clock time of the tick on which the feed's start was fixed.
  For a first frame feed, `known` is `lead` seconds before `at`. For a timed
  feeder that arrives early, `known` may be several seconds before `at`. A
  countdown to the feed counts down from `known`.
- **first pts:** the pts of the feed's first frame, in the source's own time
  base. Together with `at`, `first pts` maps any pts of the source onto the
  clock.
- **ends:** the last tick on which the feed shows, once the host can tell.
  For a feed read from a stream, the host fills in `ends` on the first tick
  that is within `lead` seconds of the time the last frame is due to show.
  For a feed by port, the host fills in `ends` on the first tick after the
  connection closes. For a feed that stops sending, the host fills in `ends`
  once the feed's `timeout` runs out, if `linger` keeps the feed showing
  after that.

At every tick, a held input also lists the feeds that ended since the
instance's previous call. An instance is one running copy of the node. On an
instance's first call, the list holds every feed that ended before that
call. Each feed in the list has `ends` set to the last tick on which the
feed showed. The list holds every end, whether or not the host gave notice
of the end ahead of time. A timeout, a connection that closed with nothing
queued, and a clock jump all appear in the list.

## Presence from the record alone

A node that reports when a feed comes and goes could keep a flag from one
tick to the next. But then the node would not be pure, and only one instance
could run the node. A pure node is one whose every tick depends only on what
the host hands it for that tick. `cutin` reads the feed record instead, and
writes each presence row, which reports a change in the feed, on the one
tick that the record names:

- `coming`, on the tick on which the feed's start was fixed, when that tick
  comes before the feed shows;
- `on`, on the tick whose interval holds `at`, where a tick's interval runs
  from the tick's time to the next tick's time;
- `off`, on the tick after `ends`, which `cutin` finds in the list of ended
  feeds.

Each of those rows belongs to one tick, and one instance runs that tick. The
record reads the same on that tick, whichever instance runs the tick. So the
rows come out the same on any number of worker threads.

## cutin

`cutin` shows its programme input `v` until a feed is on. While a feed is
on, `cutin` shows the feed's own frame instead. In both cases, `cutin` hands
the frame on as it came, without touching its pixels. Between the tick on
which a feed's start is fixed and the feed's first frame, `cutin` draws a
bar across the bottom of the programme. The bar shrinks to nothing at the
moment of the cut, as a countdown. `cutin` writes presence rows, which
report when a feed is coming, on or off, on its output `presence`. Notes
that the feeder writes beside its picture also come out on `presence`,
beside the rows about the feed itself.

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

The shape, which lists the node's ports and clock, declares the feed and the
notes like this:

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

The input `feed` is declared like `v`, so the host scales each frame of the
feed to the size of `v`. Both inputs accept only rgba. So a frame of the
feed can leave on the output `v` as it came. The input `notes` arrives on
the group's connection and uses the group's offset. `notes` is paired by
interval, so each tick hands the notes whose pts fall in the tick's
interval.

When the call gives a port, the feed is whatever program connects to that
port. The compile listing names the port, and the node that listens on that
port:

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

When the call gives a stream, the feed is that stream. Here the feed is the
first one and a half seconds of another file. The call sets `lead` to 1, so
the feed starts showing one second after its frames arrive:

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

A run of this query writes three rows: `coming` when the feed's start is
fixed, `on` one second later, and `off` on the tick after the feed's last
frame. The time at which the start is fixed depends on when the second
file's first frames reach the host. So the times in the rows change from run
to run.

## mosaic

A compositor is a node that shows many pictures at once. `mosaic` takes any
number of pictures on one port, and holds each picture with the shared clock
anchor. `mosaic` ticks at the frame rate of the first picture. A grid cell
whose feed is down shows grey. A cell whose feed is up but has no frame yet
stays black.

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
#include <cmath>
#include <vector>

#include "ffrwd/frame.hpp"
#include "ffrwd/node.hpp"

using ffrwd::frame::Filter, ffrwd::frame::Norm, ffrwd::frame::planes, ffrwd::frame::Rect, ffrwd::frame::Rgba;

/// What `planes` divides by to hand back eight-bit values unchanged.
constexpr Norm EIGHT_BITS{{0.0f, 0.0f, 0.0f}, {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f}};

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
    Rect cell;
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
        FFRWD_LET(picture, Rgba::make(pixels, tile.width, tile.height));
        std::size_t w = tile.cell.width(), h = tile.cell.height();
        auto whole = Rect::whole(tile.width, tile.height);
        auto rgb = planes(picture, whole, w, h, Filter::Bilinear, EIGHT_BITS);
        for (std::size_t y = 0; y < h; ++y)
            for (std::size_t x = 0; x < w; ++x) {
                std::size_t at = ((tile.cell.y0 + y) * width + tile.cell.x0 + x) * 4;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    float value = rgb[channel * w * h + y * w + x];
                    canvas[at + channel] = std::uint8_t(std::clamp(std::round(value), 0.0f, 255.0f));
                }
            }
        return {};
    }

    void fill(ffrwd::Bytes& canvas, Rect cell, std::array<std::uint8_t, 4> colour) const {
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
import { Filter, planes, Rect, Rgba } from '@ffrwd/node/frame';

/** What `planes` divides by to hand back eight-bit values unchanged. */
const EIGHT_BITS = { mean: [0, 0, 0], std: [1 / 255, 1 / 255, 1 / 255] };

const DOWN = [48, 48, 48, 255];

/** Every pixel of `cell` of a `width`-wide canvas set to `colour`. */
function fill(canvas, width, cell, colour) {
  const row = new Uint8Array((cell.x1 - cell.x0) * 4);
  for (let at = 0; at < row.length; at += 4) row.set(colour, at);
  for (let y = cell.y0; y < cell.y1; y += 1) canvas.set(row, (y * width + cell.x0) * 4);
}

/** `pixels`, a `tile`'s picture, resized into its cell of a `width`-wide
 * canvas. */
function put(canvas, width, tile, pixels) {
  const picture = new Rgba(pixels, tile.width, tile.height);
  const { cell } = tile;
  const [w, h] = [cell.width(), cell.height()];
  const whole = Rect.whole(tile.width, tile.height);
  const rgb = planes(picture, whole, w, h, Filter.Bilinear, EIGHT_BITS);
  for (let y = 0; y < h; y += 1) {
    for (let x = 0; x < w; x += 1) {
      const at = ((cell.y0 + y) * width + cell.x0 + x) * 4;
      for (let channel = 0; channel < 3; channel += 1) {
        const value = rgb[channel * w * h + y * w + x];
        canvas[at + channel] = Math.min(Math.max(Math.round(value), 0), 255);
      }
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
        cell: new Rect(
          Math.floor((column * width) / columns),
          Math.floor((row * height) / rows),
          Math.floor(((column + 1) * width) / columns),
          Math.floor(((row + 1) * height) / rows),
        ),
      };
    });
    const whole = Rect.whole(width, height);
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
	"math"

	node "github.com/imbcmdth/ffrwd-node/go"
	"github.com/imbcmdth/ffrwd-node/go/frame"
)

// eightBits is what frame.Planes divides by to hand back eight-bit values
// unchanged.
var eightBits = frame.Norm{Std: [3]float32{1.0 / 255, 1.0 / 255, 1.0 / 255}}

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
	cell   frame.Rect
}

type Mosaic struct {
	tiles  []Tile
	width  int
	height int
}

// put is pixels, a tile's picture, resized into its cell of canvas.
func (m *Mosaic) put(canvas []byte, tile Tile, pixels []byte) error {
	picture, err := frame.NewRgba(pixels, tile.width, tile.height)
	if err != nil {
		return err
	}
	w, h := tile.cell.Width(), tile.cell.Height()
	whole := frame.Whole(tile.width, tile.height)
	rgb := frame.Planes(picture, whole, w, h, frame.Bilinear, eightBits)
	for y := range h {
		for x := range w {
			at := ((tile.cell.Y0+y)*m.width + tile.cell.X0 + x) * 4
			for channel := range 3 {
				value := rgb[channel*w*h+y*w+x]
				canvas[at+channel] = byte(min(max(math.Round(float64(value)), 0), 255))
			}
		}
	}
	return nil
}

func (m *Mosaic) fill(canvas []byte, cell frame.Rect, colour [4]byte) {
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
				cell: frame.Rect{
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
		in, ok := tick.Frame(tile.id)
		switch {
		case ok:
			if err := m.put(canvas, tile, tick.Fetch(tile.id, in.Index)); err != nil {
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

A port that takes many streams cannot be the clock. So `mosaic` ticks at a
rate instead: the rate of the first stream bound to `v`, which the compiler
reads from that stream:

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

When the query passes an array to the port, the port binds every stream in
the array, in order:

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

ffrwd's own packages include a switch, `ffrwd.switch.switch`, and a
compositor, `ffrwd.blitz.compose`. These two nodes are fuller versions of
`cutin` and `mosaic`. The packaged nodes mix sound in and out together with
the picture, write presence rows that drive an HTML page, and accept feeds
by port on every input.
