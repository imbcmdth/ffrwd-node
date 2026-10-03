use ffrwd_frame::{planes, Filter, Norm, Rect, Rgba};
use ffrwd_node::{Bound, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::Deserialize;

/// What `planes` divides by to hand back eight-bit values unchanged.
const EIGHT_BITS: Norm = Norm {
    mean: [0.0; 3],
    std: [1.0 / 255.0; 3],
};

#[derive(Deserialize)]
struct Params {
    amount: f64,
    x: f64,
    y: f64,
}

struct Zoom {
    v: u32,
    width: usize,
    height: usize,
    params: Params,
}

impl Zoom {
    /// The part of the picture that fills the frame: `1 / amount` of each
    /// side, centred on `x`, `y` as far as the picture allows.
    fn crop(&self) -> Rect {
        let (width, height) = (self.width as f64, self.height as f64);
        let w = (width / self.params.amount).round().max(1.0);
        let h = (height / self.params.amount).round().max(1.0);
        let x0 = (self.params.x * width - w / 2.0).clamp(0.0, width - w) as usize;
        let y0 = (self.params.y * height - h / 2.0).clamp(0.0, height - h) as usize;
        Rect {
            x0,
            y0,
            x1: x0 + w as usize,
            y1: y0 + h as usize,
        }
    }
}

/// Planar red, green and blue back to opaque rgba.
fn interleave(planes: &[f32], pixels: usize) -> Vec<u8> {
    let (red, rest) = planes.split_at(pixels);
    let (green, blue) = rest.split_at(pixels);
    red.iter()
        .zip(green)
        .zip(blue)
        .flat_map(|((r, g), b)| [*r, *g, *b, 255.0].map(|c| c.round().clamp(0.0, 255.0) as u8))
        .collect()
}

impl Node for Zoom {
    const NAME: &'static str = "zoom";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"amount":{"type":"number","minimum":1,"maximum":16,"default":2},"x":{"type":"number","minimum":0,"maximum":1,"default":0.5},"y":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(_: &Params, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .output(Output::like("v"))
            .pure()
            .one_to_one())
    }

    fn init(params: Params, init: &Init) -> Result<Zoom> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is a video input")?;
        Ok(Zoom {
            v: v.id,
            width: video.width as usize,
            height: video.height as usize,
            params,
        })
    }

    fn set_params(&mut self, params: Params) -> Result<()> {
        self.params = params;
        Ok(())
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        if self.params.amount == 1.0 {
            return Ok(out.pass("v", self.v, &frame)?);
        }
        let pixels = tick.fetch(self.v, frame.index);
        let picture = Rgba::new(&pixels, self.width, self.height)?;
        let (width, height) = (self.width, self.height);
        let rgb = planes(
            &picture,
            self.crop(),
            width,
            height,
            Filter::Bilinear,
            EIGHT_BITS,
        );
        let zoomed = interleave(&rgb, width * height);
        Ok(out.frame("v", frame.pts, frame.duration, zoomed)?)
    }
}

ffrwd_node::export!(Zoom);
