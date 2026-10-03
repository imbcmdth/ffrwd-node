use std::thread::sleep;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use ffrwd_node::{Bound, Init, Node, Out, Output, Result, Shape, Tick};
use serde::Deserialize;

#[derive(Deserialize)]
struct Params {
    every: f64,
    width: u32,
    height: u32,
}

struct Beat {
    every: i64,
    next: i64,
    pixels: usize,
}

impl Node for Beat {
    const NAME: &'static str = "beat";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"every":{"type":"number","minimum":0.01,"maximum":3600,"default":1},"width":{"type":"integer","minimum":16,"maximum":8192,"default":320},"height":{"type":"integer","minimum":16,"maximum":8192,"default":240}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, _: &Bound) -> Result<Shape> {
        let (width, height) = (params.width, params.height);
        Ok(Shape::new()
            .self_clocked()
            .output(
                Output::video("video")
                    .size(width, height)
                    .pixel_format("rgba")
                    .row(0),
            )
            .relation_row(&format!(r#"{{"width":{width},"height":{height}}}"#))
            .bounded(false))
    }

    fn init(params: Params, _: &Init) -> Result<Beat> {
        Ok(Beat {
            every: (params.every * 1e6).round() as i64,
            next: 0,
            pixels: (params.width * params.height) as usize,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let now = tick.pts();
        if now < self.next {
            sleep(Duration::from_micros((self.next - now) as u64));
        }
        let wall = SystemTime::now().duration_since(UNIX_EPOCH)?.as_secs();
        let grey = (wall % 8 * 32) as u8;
        let frame = [grey, grey, grey, 255].repeat(self.pixels);
        out.frame("video", self.next, Some(self.every), frame)?;
        self.next += self.every;
        Ok(())
    }
}

ffrwd_node::export!(Beat);
