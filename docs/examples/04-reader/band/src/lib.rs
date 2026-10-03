use ffrwd_node::{Bound, Cue, Init, Input, Node, Out, Output, Result, Shape, StateRow, Tick};
use serde::Deserialize;

#[derive(Deserialize)]
struct Params {
    fade: f64,
}

struct Band {
    v: u32,
    width: usize,
    height: usize,
    fade: f64,
    cues: Vec<Cue>,
}

/// How much of the band `cue` shows at `t`: rising over `fade` seconds
/// before it starts, whole while it runs, falling over `fade` after it ends.
fn opacity(cue: &Cue, t: f64, fade: f64) -> f64 {
    if fade == 0.0 {
        return if cue.covers(t) { 1.0 } else { 0.0 };
    }
    let rising = (t - (cue.start_t - fade)) / fade;
    let falling = (cue.end_t + fade - t) / fade;
    rising.min(falling).clamp(0.0, 1.0)
}

impl Node for Band {
    const NAME: &'static str = "band";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"fade":{"type":"number","minimum":0,"maximum":5,"default":0.5}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .input(
                Input::rows("cues")
                    .interval()
                    .latency(5.0)
                    .ahead(params.fade)
                    .state()
                    .schema::<Cue>(),
            )
            .output(Output::like("v"))
            .pure()
            .one_to_one())
    }

    fn init(params: Params, init: &Init) -> Result<Band> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is a video input")?;
        Ok(Band {
            v: v.id,
            width: video.width as usize,
            height: video.height as usize,
            fade: params.fade,
            cues: Vec::new(),
        })
    }

    fn fold(&mut self, row: StateRow) -> Result<()> {
        self.cues.push(row.row::<Cue>()?);
        Ok(())
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        let t = tick.time_base().seconds(frame.pts);
        self.cues.retain(|cue| cue.end_t + self.fade > t);
        let shown = self
            .cues
            .iter()
            .map(|cue| opacity(cue, t, self.fade))
            .fold(0.0, f64::max);
        if shown == 0.0 {
            return Ok(out.pass("v", self.v, &frame)?);
        }
        let mut pixels = tick.fetch(self.v, frame.index);
        let keep = 1.0 - 0.6 * shown;
        let top = self.height * 4 / 5;
        for pixel in pixels[top * self.width * 4..].as_chunks_mut::<4>().0 {
            for channel in &mut pixel[..3] {
                *channel = (*channel as f64 * keep).round() as u8;
            }
        }
        Ok(out.frame("v", frame.pts, frame.duration, pixels)?)
    }
}

ffrwd_node::export!(Band);
