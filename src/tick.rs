use std::collections::BTreeMap;

use serde::de::DeserializeOwned;

use crate::types::{Feed, Frame, Message, Packet, StreamInfo, TimedRows};
use crate::Rational;

/// What a tick reads from: the host's resource in a module, a
/// [`mock::Tick`](crate::mock::Tick) in a test.
pub(crate) trait Source {
    fn pts(&self) -> i64;
    fn time_base(&self) -> Rational;
    fn last(&self) -> bool;
    fn streams(&self, port: &str) -> Vec<u32>;
    fn info(&self, id: u32) -> StreamInfo;
    fn feed(&self, id: u32) -> Option<Feed>;
    fn frames(&self, id: u32) -> Vec<Frame>;
    fn fetch(&self, id: u32, index: u32) -> Vec<u8>;
    fn messages(&self, id: u32) -> Vec<Message>;
    fn packets(&self, id: u32) -> Vec<Packet>;
    fn earlier_rows(&self, id: u32) -> Vec<TimedRows>;
}

/// One call's inputs, held by the host for exactly that call. Streams are
/// named by the ids `init` gave them; an id it did not give, or an index past
/// a stream's frames, is a fault that stops the run.
pub struct Tick<'a> {
    source: &'a dyn Source,
    ports: &'a BTreeMap<u32, String>,
}

impl<'a> Tick<'a> {
    pub(crate) fn new(source: &'a dyn Source, ports: &'a BTreeMap<u32, String>) -> Tick<'a> {
        Tick { source, ports }
    }

    /// The tick's time in [`Tick::time_base`]: an input clock's first frame
    /// this call, a rate clock's tick number, a self-clocked node's
    /// microseconds since its first call.
    pub fn pts(&self) -> i64 {
        self.source.pts()
    }

    /// The clock input's time base, the inverse of a rate clock's rate, or
    /// microseconds.
    pub fn time_base(&self) -> Rational {
        self.source.time_base()
    }

    /// The tick's time in seconds.
    pub fn seconds(&self) -> f64 {
        self.time_base().seconds(self.pts())
    }

    /// Whether this is the instance's final call, which happens exactly once.
    pub fn last(&self) -> bool {
        self.source.last()
    }

    /// The streams bound to input `port`, in `init`'s order.
    pub fn streams(&self, port: &str) -> Vec<u32> {
        self.source.streams(port)
    }

    /// The first stream bound to input `port`: the only one of a single
    /// port, none for an optional port the call left out.
    pub fn stream(&self, port: &str) -> Option<u32> {
        self.streams(port).first().copied()
    }

    /// The stream as the host knows it this tick, time base included.
    pub fn info(&self, id: u32) -> StreamInfo {
        self.source.info(id)
    }

    /// A hold input's feed as it stands this tick; none while nothing shows.
    pub fn feed(&self, id: u32) -> Option<Feed> {
        self.source.feed(id)
    }

    /// The frames this tick hands on stream `id`, oldest first.
    pub fn frames(&self, id: u32) -> Vec<Frame> {
        self.source.frames(id)
    }

    /// The newest frame this tick hands on stream `id`: the only one a
    /// window of one, a hold input or an audio input hands.
    pub fn frame(&self, id: u32) -> Option<Frame> {
        self.frames(id).pop()
    }

    /// Frame `index`'s bytes, copied on demand: pixels tightly packed, or
    /// interleaved samples.
    pub fn fetch(&self, id: u32, index: u32) -> Vec<u8> {
        self.source.fetch(id, index)
    }

    /// The messages this tick hands on data stream `id`, in pts order.
    pub fn messages(&self, id: u32) -> Vec<Message> {
        self.source.messages(id)
    }

    /// The packets this tick hands on packets stream `id`, in decode order.
    pub fn packets(&self, id: u32) -> Vec<Packet> {
        self.source.packets(id)
    }

    /// The rows of the ticks this instance did not process, on a state
    /// input. A node with a state input has them folded for it, so it rarely
    /// reads them itself.
    pub fn earlier_rows(&self, id: u32) -> Vec<TimedRows> {
        self.source.earlier_rows(id)
    }

    /// Every row this tick hands on stream `id`, read as `T`: a data
    /// stream's messages, or the rows riding a frame stream's frames.
    pub fn rows<T: DeserializeOwned>(&self, id: u32) -> Result<Vec<T>, String> {
        let port = self.port(id);
        let messages = self.messages(id);
        let read = if messages.is_empty() {
            self.frames(id)
                .iter()
                .map(Frame::rows)
                .collect::<Result<Vec<Vec<T>>, String>>()
                .map(|rows| rows.into_iter().flatten().collect())
        } else {
            messages.iter().map(Message::row).collect()
        };
        read.map_err(|err| format!("on `{port}`: {err}"))
    }

    /// The input port stream `id` is bound to.
    pub fn port(&self, id: u32) -> &str {
        self.ports.get(&id).map_or("?", String::as_str)
    }
}
