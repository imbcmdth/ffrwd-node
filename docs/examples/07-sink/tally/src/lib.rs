use ffrwd_node::{Bound, Format, Init, Input, NoParams, Node, Out, Rational, Result, Shape, Tick};
use serde::Serialize;

/// One row per stream, written on the last call.
#[derive(Serialize)]
struct Count {
    port: String,
    codec: String,
    packets: u64,
    keyframes: u64,
    bytes: u64,
    seconds: f64,
}

struct Stream {
    id: u32,
    time_base: Rational,
    count: Count,
    first: Option<i64>,
    last: Option<i64>,
}

struct Tally {
    streams: Vec<Stream>,
}

impl Node for Tally {
    const NAME: &'static str = "tally";
    const VERSION: &'static str = "0.1.0";
    const ROWS_SCHEMA: &'static str = r#"{"type":"object","properties":{"port":{"type":"string"},"codec":{"type":"string"},"packets":{"type":"integer"},"keyframes":{"type":"integer"},"bytes":{"type":"integer"},"seconds":{"type":"number"}},"required":["port","codec","packets","keyframes","bytes","seconds"]}"#;
    type Params = NoParams;

    fn shape(_: &NoParams, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::packets("video").optional().many().arrival())
            .input(Input::packets("audio").optional().many().arrival())
            .rate(Rational::new(10, 1)))
    }

    fn init(_: NoParams, init: &Init) -> Result<Tally> {
        let mut streams = Vec::new();
        for stream in init.all() {
            let Some(Format::Packets(coded)) = &stream.format else {
                return Err(format!("`{}` carries no packets", stream.port).into());
            };
            streams.push(Stream {
                id: stream.id,
                time_base: coded.time_base,
                count: Count {
                    port: stream.port.clone(),
                    codec: coded.codec.clone(),
                    packets: 0,
                    keyframes: 0,
                    bytes: 0,
                    seconds: 0.0,
                },
                first: None,
                last: None,
            });
        }
        Ok(Tally { streams })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        for stream in &mut self.streams {
            for packet in tick.packets(stream.id) {
                let count = &mut stream.count;
                count.packets += 1;
                count.keyframes += u64::from(packet.keyframe);
                count.bytes += packet.data.len() as u64;
                let end = packet.pts + packet.duration.unwrap_or(0);
                stream.first = Some(
                    stream
                        .first
                        .map_or(packet.pts, |first| first.min(packet.pts)),
                );
                stream.last = Some(stream.last.map_or(end, |last| last.max(end)));
            }
        }
        if tick.last() {
            for stream in &mut self.streams {
                if let (Some(first), Some(last)) = (stream.first, stream.last) {
                    stream.count.seconds = stream.time_base.seconds(last - first);
                }
                out.report(&stream.count)?;
            }
        }
        Ok(())
    }
}

ffrwd_node::export!(Tally);
