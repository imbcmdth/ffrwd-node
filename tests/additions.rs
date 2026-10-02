//! What `ffrwd:av` 0.19.1 added, driven through `mock::Harness` the way a
//! module's own tests would.

use ffrwd_node::mock::Harness;
use ffrwd_node::{
    Bound, BoundStream, Feed, FeedStart, Init, Input, Node, Out, Output, Rational, Result, Runner,
    Shape, Tick, Wants,
};
use serde::{Deserialize, Serialize};

#[derive(Deserialize)]
struct Every {
    every: u64,
}

const EVERY: &str = r#"{"type":"object","properties":{"every":{"type":"integer","minimum":1,"default":3}},"additionalProperties":false}"#;

#[derive(Default, Serialize)]
struct Numbered {
    id: u64,
}

/// A new id every `every` frames of the run, on any worker.
struct Counter {
    v: u32,
    every: u64,
}

impl Node for Counter {
    const NAME: &'static str = "counter";
    const VERSION: &'static str = "0.0.0";
    const PARAMS_SCHEMA: &'static str = EVERY;
    type Params = Every;

    fn shape(_: &Every, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock())
            .output(Output::rows("ids").schema::<Numbered>())
            .pure())
    }

    fn init(params: Every, init: &Init) -> Result<Counter> {
        Ok(Counter {
            v: init.stream("v")?.id,
            every: params.every,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        let id = tick.ordinal() / self.every;
        Ok(out.row("ids", frame.pts, &Numbered { id })?)
    }
}

fn picture() -> BoundStream {
    BoundStream::video("v", 0, 2, 2, "rgba", Rational::new(1, 15))
}

#[test]
fn ordinals_number_a_split_run_as_one() {
    let mut whole = Harness::<Counter>::new("", vec![picture()]).unwrap();
    let mut one: Vec<(i64, String)> = Vec::new();
    for pts in 0..9 {
        let tick = whole.tick(pts).frame(0, pts, vec![0; 16]);
        one.extend(whole.process(&tick).unwrap().messages("ids"));
    }

    let mut workers = [
        Harness::<Counter>::new("", vec![picture()]).unwrap(),
        Harness::<Counter>::new("", vec![picture()]).unwrap(),
    ];
    let mut split: Vec<(i64, String)> = Vec::new();
    for pts in 0..9 {
        let worker = &mut workers[pts as usize % 2];
        let tick = worker
            .tick(pts)
            .ordinal(pts as u64)
            .frame(0, pts, vec![0; 16]);
        split.extend(worker.process(&tick).unwrap().messages("ids"));
    }
    assert_eq!(split, one);
    assert_eq!(one[2].1, r#"{"id":0}"#);
    assert_eq!(one[3].1, r#"{"id":1}"#);
}

#[test]
fn a_harness_numbers_its_ticks_from_0() {
    let mut node = Harness::<Counter>::new(r#"{"every":1}"#, vec![picture()]).unwrap();
    for (pts, expected) in [(0, 0), (1, 1), (5, 2)] {
        let tick = node.tick(pts).frame(0, pts, vec![0; 16]);
        let emitted = node.process(&tick).unwrap();
        assert_eq!(
            emitted.messages("ids")[0].1,
            format!(r#"{{"id":{expected}}}"#)
        );
    }
    let skipped = node.tick(9).ordinal(40).frame(0, 9, vec![0; 16]);
    node.process(&skipped).unwrap();
    assert_eq!(
        node.process(&node.tick(10).frame(0, 10, vec![0; 16]))
            .unwrap()
            .messages("ids")[0]
            .1,
        r#"{"id":41}"#
    );
}

#[derive(Deserialize)]
struct Peek {
    #[serde(default)]
    fetch: bool,
    #[serde(default)]
    pass: bool,
}

const PEEK: &str = r#"{"type":"object","properties":{"fetch":{"type":"boolean"},"pass":{"type":"boolean"}},"additionalProperties":false}"#;

/// A gray mask the size of a picture it never reads.
struct Mask {
    v: u32,
    size: usize,
    params: Peek,
}

impl Node for Mask {
    const NAME: &'static str = "mask";
    const VERSION: &'static str = "0.0.0";
    const PARAMS_SCHEMA: &'static str = PEEK;
    type Params = Peek;

    fn shape(_: &Peek, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().timing())
            .output(Output::video("mask").pixel_format("gray"))
            .output(Output::like("v"))
            .pure())
    }

    fn init(params: Peek, init: &Init) -> Result<Mask> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is video")?;
        Ok(Mask {
            v: v.id,
            size: (video.width * video.height) as usize,
            params,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        if self.params.fetch {
            let _ = tick.fetch(self.v, frame.index);
        }
        if self.params.pass {
            out.pass("v", self.v, &frame)?;
        }
        Ok(out.frame("mask", frame.pts, frame.duration, vec![255; self.size])?)
    }
}

#[test]
fn a_timing_input_hands_times_and_size_and_no_bytes() {
    let mut mask = Harness::<Mask>::new("", vec![picture()]).unwrap();
    assert_eq!(mask.shape().inputs[0].accepts.wants, Wants::Timing);
    let emitted = mask.process(&mask.tick(4).frame(0, 4, Vec::new())).unwrap();
    assert_eq!(emitted.on("mask").len(), 1);

    let mut fetching = Harness::<Mask>::new(r#"{"fetch":true}"#, vec![picture()]).unwrap();
    let err = fetching
        .process(&fetching.tick(0).frame(0, 0, Vec::new()))
        .unwrap_err();
    assert!(err.contains("`v`") && err.contains("timing alone"), "{err}");

    let mut passing = Harness::<Mask>::new(r#"{"pass":true}"#, vec![picture()]).unwrap();
    let err = passing
        .process(&passing.tick(0).frame(0, 0, Vec::new()))
        .unwrap_err();
    assert!(err.contains("`v`") && err.contains("timing alone"), "{err}");
}

#[derive(Deserialize)]
struct Seconds {
    seconds: f64,
}

const SECONDS: &str = r#"{"type":"object","properties":{"seconds":{"type":"number","exclusiveMinimum":0,"default":2}},"additionalProperties":false}"#;

/// A window of `seconds` of sound, whatever its rate.
struct Hear;

impl Node for Hear {
    const NAME: &'static str = "hear";
    const VERSION: &'static str = "0.0.0";
    const PARAMS_SCHEMA: &'static str = SECONDS;
    type Params = Seconds;

    fn shape(params: &Seconds, bound: &Bound) -> Result<Shape> {
        let rate = bound
            .rate_of("a")
            .ok_or("`a` is bound at a rate the compiler cannot know")?;
        let window = rate.count(params.seconds) as u32;
        Ok(Shape::new()
            .input(Input::audio("a").clock().window(window, window))
            .output(Output::rows("heard")))
    }

    fn init(_: Seconds, _: &Init) -> Result<Hear> {
        Ok(Hear)
    }

    fn process(&mut self, _: &Tick, _: &mut Out) -> Result<()> {
        Ok(())
    }
}

/// A stretch closed on the first frame at or past `seconds`: its rows trail
/// their tick by that and a frame.
struct Clips;

impl Node for Clips {
    const NAME: &'static str = "clips";
    const VERSION: &'static str = "0.0.0";
    const PARAMS_SCHEMA: &'static str = SECONDS;
    type Params = Seconds;

    fn shape(params: &Seconds, bound: &Bound) -> Result<Shape> {
        let frame = bound.rate_of("v").map_or(0.5, |rate| rate.duration(1));
        Ok(Shape::new()
            .input(Input::video("v").clock())
            .output(Output::rows("clips").latency(params.seconds + frame)))
    }

    fn init(_: Seconds, _: &Init) -> Result<Clips> {
        Ok(Clips)
    }

    fn process(&mut self, _: &Tick, _: &mut Out) -> Result<()> {
        Ok(())
    }
}

#[test]
fn a_window_in_seconds_is_counted_at_the_bound_rate() {
    let at_48k =
        Runner::<Hear>::shape("", Bound::new(&["a"]).rate("a", Rational::new(48000, 1))).unwrap();
    assert_eq!(at_48k.inputs[0].window, 96000);
    let err = Runner::<Hear>::shape("", Bound::new(&["a"])).unwrap_err();
    assert!(err.contains("`a`"), "{err}");

    let a = BoundStream::audio("a", 0, 16000, 1, "f32");
    let hear = Harness::<Hear>::new(r#"{"seconds":30}"#, vec![a]).unwrap();
    assert_eq!(hear.shape().inputs[0].window, 480000);
}

#[test]
fn a_latency_a_frame_past_a_cap_is_exact_where_the_rate_is_known() {
    let latency = |bound: Bound| {
        Runner::<Clips>::shape(r#"{"seconds":10}"#, bound)
            .unwrap()
            .outputs[0]
            .latency
    };
    assert_eq!(
        latency(Bound::new(&["v"]).rate("v", Rational::new(2, 1))),
        10.5
    );
    assert_eq!(
        latency(Bound::new(&["v"]).rate("v", Rational::new(25, 1))),
        10.0 + 1.0 / 25.0
    );
    assert_eq!(latency(Bound::new(&["v"])), 10.5);
}

#[test]
fn init_shapes_the_node_as_the_plan_did() {
    let at_25 = vec![picture().rate(Rational::new(25, 1))];
    let planned = Runner::<Clips>::shape(r#"{"seconds":10}"#, Bound::of(&at_25)).unwrap();
    let clips = Harness::<Clips>::new(r#"{"seconds":10}"#, at_25).unwrap();
    assert_eq!(clips.shape(), &planned);
    assert_eq!(clips.shape().outputs[0].latency, 10.0 + 1.0 / 25.0);
}

/// One input per stream of a many port, counted at compile time.
struct Tile;

impl Node for Tile {
    const NAME: &'static str = "tile";
    const VERSION: &'static str = "0.0.0";
    type Params = ffrwd_node::NoParams;

    fn shape(_: &ffrwd_node::NoParams, bound: &Bound) -> Result<Shape> {
        let cells = bound.count("v").max(1) as u32;
        let rate = bound.rate_of("v").unwrap_or(Rational::new(30, 1));
        Ok(Shape::new()
            .input(Input::video("v").many().hold())
            .rate(rate)
            .output(
                Output::video("grid")
                    .size(64 * cells, 48)
                    .pixel_format("rgba"),
            ))
    }

    fn init(_: ffrwd_node::NoParams, _: &Init) -> Result<Tile> {
        Ok(Tile)
    }

    fn process(&mut self, _: &Tick, _: &mut Out) -> Result<()> {
        Ok(())
    }
}

#[test]
fn a_many_port_says_how_many_streams_it_takes() {
    let three = Bound::default().bind(
        "v",
        &[Some(Rational::new(25, 1)), None, Some(Rational::new(30, 1))],
    );
    let shape = Runner::<Tile>::shape("", &three).unwrap();
    let Some(ffrwd_node::Format::Video(grid)) = &shape.outputs[0].format else {
        panic!("a video format");
    };
    assert_eq!(grid.width, 192);
    assert_eq!(
        shape.clock,
        Some(ffrwd_node::Clock::Rate(Rational::new(25, 1)))
    );
}

#[derive(Default, Serialize)]
struct Presence {
    coming: i64,
    ended: i64,
}

/// A row per feed of `feed` that ended, saying when it became known and
/// when it went.
struct Present {
    feed: Option<u32>,
}

impl Node for Present {
    const NAME: &'static str = "present";
    const VERSION: &'static str = "0.0.0";
    type Params = ffrwd_node::NoParams;

    fn shape(_: &ffrwd_node::NoParams, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock())
            .input(Input::video("feed").optional().hold().lead(0.5))
            .output(Output::rows("presence").schema::<Presence>())
            .pure())
    }

    fn init(_: ffrwd_node::NoParams, init: &Init) -> Result<Present> {
        Ok(Present {
            feed: init.optional("feed").map(|stream| stream.id),
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(feed) = self.feed else {
            return Ok(());
        };
        for ended in tick.ended_feeds(feed) {
            let row = Presence {
                coming: ended.start.known,
                ended: ended.ends.ok_or("an ended feed says its end")?,
            };
            out.row("presence", tick.pts(), &row)?;
        }
        Ok(())
    }
}

#[test]
fn ended_feeds_say_when_a_start_was_known_and_when_it_went() {
    let feed = BoundStream::video("feed", 1, 2, 2, "rgba", Rational::new(1, 15));
    let mut node = Harness::<Present>::new("", vec![picture(), feed]).unwrap();
    let gone = Feed {
        start: FeedStart {
            tags: Vec::new(),
            first_pts: 0,
            at: 20,
            known: 12,
        },
        ends: Some(31),
    };
    let quiet = node.tick(30).frame(0, 30, Vec::new());
    assert!(node
        .process(&quiet)
        .unwrap()
        .messages("presence")
        .is_empty());
    let after = node.tick(33).frame(0, 33, Vec::new()).ended(1, gone);
    assert_eq!(
        node.process(&after).unwrap().messages("presence"),
        [(33, r#"{"coming":12,"ended":31}"#.to_owned())]
    );
}
