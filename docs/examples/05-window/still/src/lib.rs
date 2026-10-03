use ffrwd_node::{Bound, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::{Deserialize, Serialize};

#[derive(Deserialize)]
struct Params {
    shortest: f64,
    longest: f64,
    tolerance: f64,
}

#[derive(Default, Serialize)]
struct Still {
    start_t: f64,
    end_t: f64,
}

struct StillNode {
    v: u32,
    /// The bytes of a picture's luma plane, which come first in yuv420p.
    luma: usize,
    params: Params,
    /// Where the stretch the picture is still in began: its pts and seconds.
    open: Option<(i64, f64)>,
}

/// How far apart two pictures' luma planes are: the mean difference of a
/// pixel.
fn difference(a: &[u8], b: &[u8]) -> f64 {
    let total: u64 = a.iter().zip(b).map(|(a, b)| a.abs_diff(*b) as u64).sum();
    total as f64 / a.len().max(1) as f64
}

impl StillNode {
    /// Writes the open stretch as ending at `end_t`, if it lasted long enough.
    fn close(&mut self, end_t: f64, out: &mut Out) -> Result<()> {
        if let Some((pts, start_t)) = self.open.take() {
            if end_t - start_t >= self.params.shortest {
                out.row("stills", pts, &Still { start_t, end_t })?;
            }
        }
        Ok(())
    }
}

impl Node for StillNode {
    const NAME: &'static str = "still";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"shortest":{"type":"number","minimum":0,"default":1},"longest":{"type":"number","exclusiveMinimum":0,"maximum":600,"default":10},"tolerance":{"type":"number","minimum":0,"maximum":255,"default":2}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, bound: &Bound) -> Result<Shape> {
        let frame = bound.rate_of("v").map_or(1.0, |rate| rate.duration(1));
        Ok(Shape::new()
            .input(
                Input::video("v")
                    .clock()
                    .window(2, 1)
                    .pixel_formats(&["yuv420p"]),
            )
            .output(
                Output::rows("stills")
                    .latency(params.longest + frame)
                    .schema::<Still>(),
            ))
    }

    fn init(params: Params, init: &Init) -> Result<StillNode> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is a video input")?;
        Ok(StillNode {
            v: v.id,
            luma: (video.width * video.height) as usize,
            params,
            open: None,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let seconds = |pts| tick.time_base().seconds(pts);
        let frames = tick.frames(self.v);
        let [before, after] = &frames[..] else {
            let end = tick
                .frame(self.v)
                .map_or(tick.seconds(), |last| seconds(last.pts));
            return self.close(end, out);
        };
        let moved = difference(
            &tick.fetch(self.v, before.index)[..self.luma],
            &tick.fetch(self.v, after.index)[..self.luma],
        );
        if moved > self.params.tolerance {
            return self.close(seconds(after.pts), out);
        }
        let (_, start_t) = *self.open.get_or_insert((before.pts, seconds(before.pts)));
        if seconds(after.pts) - start_t >= self.params.longest {
            self.close(seconds(after.pts), out)?;
            self.open = Some((after.pts, seconds(after.pts)));
        }
        Ok(())
    }
}

ffrwd_node::export!(StillNode);
