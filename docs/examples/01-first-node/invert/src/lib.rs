use ffrwd_node::{Bound, Init, Input, NoParams, Node, Out, Output, Result, Shape, Tick};

struct Invert {
    v: u32,
}

impl Node for Invert {
    const NAME: &'static str = "invert";
    const VERSION: &'static str = "0.1.0";
    type Params = NoParams;

    fn shape(_: &NoParams, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .output(Output::like("v"))
            .pure()
            .one_to_one())
    }

    fn init(_: NoParams, init: &Init) -> Result<Invert> {
        Ok(Invert {
            v: init.stream("v")?.id,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        let mut pixels = tick.fetch(self.v, frame.index);
        for pixel in pixels.as_chunks_mut::<4>().0 {
            for channel in &mut pixel[..3] {
                *channel = 255 - *channel;
            }
        }
        Ok(out.frame("v", frame.pts, frame.duration, pixels)?)
    }
}

ffrwd_node::export!(Invert);
