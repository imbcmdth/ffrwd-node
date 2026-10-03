use ffrwd_node::{Bound, Init, Input, NoParams, Node, Out, Output, Result, Shape, Tick};
use serde::{Deserialize, Serialize};

/// The fields `boxmask` reads. Any row carrying them will do.
#[derive(Default, Serialize, Deserialize)]
struct Box {
    x: f64,
    y: f64,
    w: f64,
    h: f64,
}

struct BoxMask {
    v: u32,
    boxes: u32,
    width: usize,
    height: usize,
}

impl Node for BoxMask {
    const NAME: &'static str = "boxmask";
    const VERSION: &'static str = "0.1.0";
    type Params = NoParams;

    fn shape(_: &NoParams, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().timing())
            .input(Input::rows("boxes").schema::<Box>())
            .output(Output::like("v").pixel_format("gray"))
            .pure()
            .one_to_one())
    }

    fn init(_: NoParams, init: &Init) -> Result<BoxMask> {
        let v = init.stream("v")?;
        let video = v.video_format().ok_or("`v` is a video input")?;
        Ok(BoxMask {
            v: v.id,
            boxes: init.stream("boxes")?.id,
            width: video.width as usize,
            height: video.height as usize,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        let mut mask = vec![0u8; self.width * self.height];
        for found in tick.rows::<Box>(self.boxes)? {
            let x0 = (found.x.max(0.0) as usize).min(self.width);
            let y0 = (found.y.max(0.0) as usize).min(self.height);
            let x1 = ((found.x + found.w).max(0.0) as usize).min(self.width);
            let y1 = ((found.y + found.h).max(0.0) as usize).min(self.height);
            for y in y0..y1 {
                mask[y * self.width + x0..y * self.width + x1.max(x0)].fill(255);
            }
        }
        Ok(out.frame("v", frame.pts, frame.duration, mask)?)
    }
}

ffrwd_node::export!(BoxMask);
