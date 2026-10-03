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
