use ffrwd_node::{Bound, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::{Deserialize, Serialize};

#[derive(Deserialize)]
struct Params {
    threshold: u8,
    every: u64,
}

#[derive(Default, Serialize)]
struct Glow {
    start_t: f64,
    id: u64,
    x: u32,
    y: u32,
    w: u32,
    h: u32,
}

struct GlowNode {
    v: u32,
    width: usize,
    threshold: u8,
    every: u64,
    /// One frame of the picture, in its time base.
    step: i64,
}

/// The box around every pixel of an rgba picture at least `threshold`
/// bright.
fn bright(pixels: &[u8], width: usize, threshold: u8) -> Option<[u32; 4]> {
    let mut found: Option<[usize; 4]> = None;
    for (at, pixel) in pixels.as_chunks::<4>().0.iter().enumerate() {
        let luma = (54 * pixel[0] as u32 + 183 * pixel[1] as u32 + 19 * pixel[2] as u32) >> 8;
        if luma >= threshold as u32 {
            let (x, y) = (at % width, at / width);
            let [x0, y0, x1, y1] = found.get_or_insert([x, y, x, y]);
            (*x0, *y0, *x1, *y1) = ((*x0).min(x), (*y0).min(y), (*x1).max(x), (*y1).max(y));
        }
    }
    found.map(|[x0, y0, x1, y1]| [x0, y0, x1 - x0 + 1, y1 - y0 + 1].map(|n| n as u32))
}

impl Node for GlowNode {
    const NAME: &'static str = "glow";
    const VERSION: &'static str = "0.2.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"threshold":{"type":"integer","minimum":0,"maximum":255,"default":230},"every":{"type":"integer","minimum":1,"default":30}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(_: &Params, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .output(Output::rows("glows").schema::<Glow>())
            .pure())
    }

    fn init(params: Params, init: &Init) -> Result<GlowNode> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is a video input")?;
        let step = v
            .hint
            .rate
            .map_or(1, |rate| v.info.time_base.pts(rate.duration(1)).max(1));
        Ok(GlowNode {
            v: v.id,
            width: video.width as usize,
            threshold: params.threshold,
            every: params.every,
            step,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        let pixels = tick.fetch(self.v, frame.index);
        let Some([x, y, w, h]) = bright(&pixels, self.width, self.threshold) else {
            return Ok(());
        };
        let into = (tick.ordinal() % self.every) as i64;
        let glow = Glow {
            start_t: tick.time_base().seconds(frame.pts - into * self.step),
            id: tick.ordinal() / self.every,
            x,
            y,
            w,
            h,
        };
        Ok(out.row("glows", frame.pts, &glow)?)
    }
}

ffrwd_node::export!(GlowNode);

#[cfg(test)]
mod tests {
    use super::*;
    use ffrwd_node::mock::Harness;
    use ffrwd_node::{BoundStream, Rational};

    fn open() -> Harness<GlowNode> {
        let v = BoundStream::video("v", 0, 4, 4, "rgba", Rational::new(1, 15))
            .rate(Rational::new(15, 1));
        Harness::new(r#"{"every":3}"#, vec![v]).unwrap()
    }

    /// A picture lit at one pixel, which moves along the top row.
    fn lit(n: usize) -> Vec<u8> {
        let mut pixels = vec![0; 4 * 4 * 4];
        pixels[(n % 4) * 4..(n % 4) * 4 + 4].fill(255);
        pixels
    }

    /// Every row of `ticks` ticks handed to `workers` instances in turn, in
    /// pts order.
    fn rows(ticks: usize, workers: usize) -> Vec<(i64, String)> {
        let mut instances: Vec<Harness<GlowNode>> = (0..workers).map(|_| open()).collect();
        let mut rows = Vec::new();
        for n in 0..ticks {
            let worker = &mut instances[n % workers];
            let tick = worker
                .tick(n as i64)
                .ordinal(n as u64)
                .frame(0, n as i64, lit(n));
            rows.extend(worker.process(&tick).unwrap().messages("glows"));
        }
        rows.sort();
        rows
    }

    #[test]
    fn any_number_of_workers_write_the_same_rows() {
        let alone = rows(10, 1);
        assert_eq!(alone.len(), 10);
        assert_eq!(rows(10, 2), alone);
        assert_eq!(rows(10, 3), alone);
    }

    #[test]
    fn a_sighting_starts_every_so_many_frames() {
        let ids: Vec<String> = rows(7, 1).into_iter().map(|(_, row)| row).collect();
        assert!(
            ids[2].starts_with(r#"{"start_t":0.0,"id":0,"#),
            "{}",
            ids[2]
        );
        assert!(
            ids[3].starts_with(r#"{"start_t":0.2,"id":1,"#),
            "{}",
            ids[3]
        );
    }
}
