//! A node's calls on the host, for its unit tests: a [`Harness`] runs the
//! same call sequence a module does, on a [`Tick`] built by hand.

use std::collections::BTreeMap;

use serde::Serialize;

use crate::node::{Node, Runner};
use crate::out::Emitted;
use crate::shape::{Clock, Shape};
use crate::tick::Source;
use crate::types::{BoundStream, Feed, Frame, Message, Packet, StreamInfo, TimedRows};
use crate::Rational;

/// One tick's inputs, built by hand. An id it was not told of, or an index
/// past a stream's frames, panics, as the host stops the run.
#[derive(Clone, Debug)]
pub struct Tick {
    pts: i64,
    time_base: Rational,
    last: bool,
    streams: Vec<(String, u32)>,
    infos: BTreeMap<u32, StreamInfo>,
    frames: BTreeMap<u32, Vec<(Frame, Vec<u8>)>>,
    messages: BTreeMap<u32, Vec<Message>>,
    packets: BTreeMap<u32, Vec<Packet>>,
    feeds: BTreeMap<u32, Feed>,
    earlier: BTreeMap<u32, Vec<TimedRows>>,
}

impl Tick {
    /// A tick at `pts` of a clock counted in `time_base`, with no streams.
    pub fn new(pts: i64, time_base: Rational) -> Tick {
        Tick {
            pts,
            time_base,
            last: false,
            streams: Vec::new(),
            infos: BTreeMap::new(),
            frames: BTreeMap::new(),
            messages: BTreeMap::new(),
            packets: BTreeMap::new(),
            feeds: BTreeMap::new(),
            earlier: BTreeMap::new(),
        }
    }

    /// `stream` bound on its port.
    pub fn bind(mut self, stream: &BoundStream) -> Tick {
        self.streams.push((stream.port.clone(), stream.id));
        self.infos.insert(stream.id, stream.info.clone());
        self
    }

    /// The instance's final call.
    pub fn last(mut self) -> Tick {
        self.last = true;
        self
    }

    /// A frame of `data` at `pts` on stream `id`, after the ones already
    /// there.
    pub fn frame(self, id: u32, pts: i64, data: Vec<u8>) -> Tick {
        self.frame_with(id, pts, None, &[], data)
    }

    /// A frame with a duration and rows riding it.
    pub fn frame_with(
        mut self,
        id: u32,
        pts: i64,
        duration: Option<i64>,
        rows: &[&str],
        data: Vec<u8>,
    ) -> Tick {
        let frames = self.frames.entry(id).or_default();
        let frame = Frame {
            pts,
            index: frames.len() as u32,
            duration,
            rows: rows.iter().map(|row| (*row).to_owned()).collect(),
        };
        frames.push((frame, data));
        self
    }

    /// A message on data stream `id`.
    pub fn message(mut self, id: u32, pts: i64, data: &[u8]) -> Tick {
        self.messages.entry(id).or_default().push(Message {
            pts,
            data: data.to_vec(),
        });
        self
    }

    /// `row` as a JSON message on data stream `id`.
    pub fn row<T: Serialize>(self, id: u32, pts: i64, row: &T) -> Tick {
        let data = serde_json::to_vec(row).expect("a row that serializes");
        self.message(id, pts, &data)
    }

    pub fn packet(mut self, id: u32, packet: Packet) -> Tick {
        self.packets.entry(id).or_default().push(packet);
        self
    }

    /// Hold input `id`'s feed as it stands on this tick.
    pub fn feed(mut self, id: u32, feed: Feed) -> Tick {
        self.feeds.insert(id, feed);
        self
    }

    /// Rows a state input received on a tick this instance did not process.
    pub fn earlier(mut self, id: u32, pts: i64, rows: &[&str]) -> Tick {
        self.earlier.entry(id).or_default().push(TimedRows {
            pts,
            rows: rows.iter().map(|row| (*row).to_owned()).collect(),
        });
        self
    }

    fn known(&self, id: u32) {
        assert!(
            self.infos.contains_key(&id),
            "stream {id} is not bound on this tick"
        );
    }
}

impl Source for Tick {
    fn pts(&self) -> i64 {
        self.pts
    }

    fn time_base(&self) -> Rational {
        self.time_base
    }

    fn last(&self) -> bool {
        self.last
    }

    fn streams(&self, port: &str) -> Vec<u32> {
        self.streams
            .iter()
            .filter(|(name, _)| name == port)
            .map(|(_, id)| *id)
            .collect()
    }

    fn info(&self, id: u32) -> StreamInfo {
        self.known(id);
        self.infos[&id].clone()
    }

    fn feed(&self, id: u32) -> Option<Feed> {
        self.known(id);
        self.feeds.get(&id).cloned()
    }

    fn frames(&self, id: u32) -> Vec<Frame> {
        self.known(id);
        self.frames
            .get(&id)
            .map(|frames| frames.iter().map(|(frame, _)| frame.clone()).collect())
            .unwrap_or_default()
    }

    fn fetch(&self, id: u32, index: u32) -> Vec<u8> {
        self.known(id);
        let frames = self.frames.get(&id).map(Vec::as_slice).unwrap_or_default();
        match frames.get(index as usize) {
            Some((_, data)) => data.clone(),
            None => panic!("stream {id} has no frame {index} on this tick"),
        }
    }

    fn messages(&self, id: u32) -> Vec<Message> {
        self.known(id);
        self.messages.get(&id).cloned().unwrap_or_default()
    }

    fn packets(&self, id: u32) -> Vec<Packet> {
        self.known(id);
        self.packets.get(&id).cloned().unwrap_or_default()
    }

    fn earlier_rows(&self, id: u32) -> Vec<TimedRows> {
        self.known(id);
        self.earlier.get(&id).cloned().unwrap_or_default()
    }
}

/// A node opened on the host: `shape` and `init` as the host calls them,
/// every output latched, and ticks that come with the bound streams in
/// place.
pub struct Harness<N: Node> {
    runner: Runner<N>,
    bound: Vec<BoundStream>,
    clock: Option<Rational>,
}

impl<N: Node> Harness<N> {
    /// Opens `N` with `params` on `bound`.
    pub fn new(params: &str, bound: Vec<BoundStream>) -> Result<Harness<N>, String> {
        let names: Vec<String> = bound.iter().map(|stream| stream.port.clone()).collect();
        let shape = Runner::<N>::shape(params, &names)?;
        let latched = shape
            .outputs
            .iter()
            .map(|output| output.name.clone())
            .collect();
        let runner = Runner::<N>::init(bound.clone(), latched, params)?;
        let clock = match &runner.resolved().clock {
            Some(Clock::Input(port)) => bound
                .iter()
                .find(|stream| &stream.port == port)
                .map(|stream| stream.info.time_base),
            Some(Clock::Rate(rate)) => Some(rate.inverse()),
            Some(Clock::SelfClocked) => Some(Rational::MICROS),
            _ => None,
        };
        Ok(Harness {
            runner,
            bound,
            clock,
        })
    }

    /// The clock's time base, for a node whose clock is another input's
    /// rate.
    pub fn clock(mut self, time_base: Rational) -> Harness<N> {
        self.clock = Some(time_base);
        self
    }

    /// A tick at `pts` on the clock, every bound stream in place.
    pub fn tick(&self, pts: i64) -> Tick {
        let time_base = self
            .clock
            .expect("the clock's time base is the rate of an input; give it with `clock`");
        self.bound
            .iter()
            .fold(Tick::new(pts, time_base), |tick, stream| tick.bind(stream))
    }

    /// One `process` call.
    pub fn process(&mut self, tick: &Tick) -> Result<Emitted, String> {
        self.runner.process_source(tick)
    }

    pub fn set_params(&mut self, params: &str) -> Result<(), String> {
        self.runner.set_params(params)
    }

    pub fn node(&self) -> &N {
        self.runner.node()
    }

    pub fn shape(&self) -> &Shape {
        self.runner.resolved()
    }
}
