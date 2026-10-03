use ffrwd_node::{Bound, Init, Input, Node, Out, Output, Result, Shape, Span, Spans, Tick};
use serde::{Deserialize, Serialize};

#[derive(Deserialize)]
struct Params {
    threshold: u8,
    gap: u64,
}

#[derive(Default, Serialize)]
struct Glow {
    #[serde(flatten)]
    span: Span,
    x: u32,
    y: u32,
    w: u32,
    h: u32,
}

struct GlowNode {
    v: u32,
    width: usize,
    threshold: u8,
    spans: Spans<()>,
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
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"threshold":{"type":"integer","minimum":0,"maximum":255,"default":230},"gap":{"type":"integer","minimum":0,"default":2}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(_: &Params, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .output(Output::rows("glows").schema::<Glow>()))
    }

    fn init(params: Params, init: &Init) -> Result<GlowNode> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is a video input")?;
        Ok(GlowNode {
            v: v.id,
            width: video.width as usize,
            threshold: params.threshold,
            spans: Spans::new().gap(params.gap),
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        self.spans.tick(tick.time_base().seconds(frame.pts));
        let pixels = tick.fetch(self.v, frame.index);
        let Some([x, y, w, h]) = bright(&pixels, self.width, self.threshold) else {
            return Ok(());
        };
        let glow = Glow {
            span: self.spans.see(()),
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

    /// The `start_t` of every row of four lit ticks handed to `workers`
    /// instances in turn, in pts order.
    fn starts(workers: usize) -> Vec<String> {
        let open = || {
            let v = BoundStream::video("v", 0, 2, 2, "rgba", Rational::new(1, 10));
            Harness::<GlowNode>::new("", vec![v]).unwrap()
        };
        let mut instances: Vec<Harness<GlowNode>> = (0..workers).map(|_| open()).collect();
        let mut rows = Vec::new();
        for n in 0..4 {
            let worker = &mut instances[n % workers];
            let tick = worker
                .tick(n as i64)
                .ordinal(n as u64)
                .frame(0, n as i64, vec![255; 16]);
            rows.extend(worker.process(&tick).unwrap().messages("glows"));
        }
        rows.sort();
        rows.into_iter()
            .map(|(_, row)| row.split(',').next().unwrap().to_owned())
            .collect()
    }

    #[test]
    fn spans_kept_across_ticks_split_with_the_workers() {
        assert_eq!(starts(1), [r#"{"start_t":0.0"#; 4]);
        assert_eq!(
            starts(2),
            [
                r#"{"start_t":0.0"#,
                r#"{"start_t":0.1"#,
                r#"{"start_t":0.0"#,
                r#"{"start_t":0.1"#
            ]
        );
    }
}
