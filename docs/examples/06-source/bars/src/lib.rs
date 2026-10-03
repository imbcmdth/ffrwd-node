use ffrwd_node::{Bound, Init, Node, Out, Output, Rational, Result, Shape, Tick};
use serde::Deserialize;

const COLOURS: [[u8; 4]; 7] = [
    [192, 192, 192, 255],
    [192, 192, 0, 255],
    [0, 192, 192, 255],
    [0, 192, 0, 255],
    [192, 0, 192, 255],
    [192, 0, 0, 255],
    [0, 0, 192, 255],
];

#[derive(Deserialize)]
struct Params {
    width: u32,
    height: u32,
    fps: f64,
    seconds: Option<f64>,
}

struct Bars {
    width: usize,
    height: usize,
    seconds: Option<f64>,
}

impl Bars {
    /// Seven bars, and a white line crossing them once a second.
    fn draw(&self, t: f64) -> Vec<u8> {
        let line = (t.fract() * self.width as f64) as usize;
        let mut canvas = Vec::with_capacity(self.width * self.height * 4);
        for _ in 0..self.height {
            for x in 0..self.width {
                let colour = if x == line {
                    [255; 4]
                } else {
                    COLOURS[x * COLOURS.len() / self.width]
                };
                canvas.extend_from_slice(&colour);
            }
        }
        canvas
    }
}

impl Node for Bars {
    const NAME: &'static str = "bars";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"width":{"type":"integer","minimum":16,"maximum":8192,"default":1280},"height":{"type":"integer","minimum":16,"maximum":8192,"default":720},"fps":{"type":"number","exclusiveMinimum":0,"maximum":240,"default":30},"seconds":{"type":["number","null"],"exclusiveMinimum":0}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, _: &Bound) -> Result<Shape> {
        let (width, height) = (params.width, params.height);
        Ok(Shape::new()
            .rate(Rational::approximate(params.fps, 1001))
            .output(
                Output::video("video")
                    .size(width, height)
                    .pixel_format("rgba")
                    .row(0),
            )
            .relation_row(&format!(r#"{{"width":{width},"height":{height}}}"#))
            .bounded(params.seconds.is_some())
            .pure())
    }

    fn init(params: Params, _: &Init) -> Result<Bars> {
        Ok(Bars {
            width: params.width as usize,
            height: params.height as usize,
            seconds: params.seconds,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        if self
            .seconds
            .is_some_and(|seconds| tick.seconds() >= seconds)
        {
            out.finish();
            return Ok(());
        }
        let canvas = self.draw(tick.seconds());
        Ok(out.frame("video", tick.pts(), Some(1), canvas)?)
    }
}

ffrwd_node::export!(Bars);
