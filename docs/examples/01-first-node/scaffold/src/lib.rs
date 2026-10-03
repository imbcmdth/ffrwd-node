use ffrwd_node::{Bound, Init, Input, NoParams, Node, Out, Output, Result, Shape, Tick};

struct Passthrough {
    v: u32,
}

impl Node for Passthrough {
    const NAME: &'static str = "passthrough";
    const VERSION: &'static str = "0.1.0";
    type Params = NoParams;

    fn shape(_: &NoParams, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .output(Output::like("v"))
            .pure()
            .one_to_one())
    }

    fn init(_: NoParams, init: &Init) -> Result<Passthrough> {
        Ok(Passthrough {
            v: init.stream("v")?.id,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        // Your work goes here: `tick.fetch` reads the picture, `out.frame` sends a new one.
        Ok(out.pass("v", self.v, &frame)?)
    }
}

ffrwd_node::export!(Passthrough);
