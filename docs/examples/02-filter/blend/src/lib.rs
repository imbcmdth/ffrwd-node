use ffrwd_node::{Bound, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::Deserialize;

#[derive(Deserialize)]
struct Params {
    mix: f64,
}

struct Blend {
    v: u32,
    over: u32,
    mix: f64,
}

impl Node for Blend {
    const NAME: &'static str = "blend";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"mix":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(_: &Params, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .input(
                Input::video("over")
                    .lockstep()
                    .like("v")
                    .pixel_formats(&["rgba"]),
            )
            .output(Output::like("v"))
            .pure()
            .one_to_one())
    }

    fn init(params: Params, init: &Init) -> Result<Blend> {
        Ok(Blend {
            v: init.stream("v")?.id,
            over: init.stream("over")?.id,
            mix: params.mix,
        })
    }

    fn set_params(&mut self, params: Params) -> Result<()> {
        self.mix = params.mix;
        Ok(())
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        let Some(top) = tick.frame(self.over) else {
            return Ok(out.pass("v", self.v, &frame)?);
        };
        if self.mix == 0.0 {
            return Ok(out.pass("v", self.v, &frame)?);
        }
        if self.mix == 1.0 {
            return Ok(out.same("v", frame.pts, frame.duration, self.over, top.index)?);
        }
        let mut pixels = tick.fetch(self.v, frame.index);
        let over = tick.fetch(self.over, top.index);
        for (under, over) in pixels.iter_mut().zip(&over) {
            let mixed = *under as f64 + (*over as f64 - *under as f64) * self.mix;
            *under = mixed.round() as u8;
        }
        Ok(out.frame("v", frame.pts, frame.duration, pixels)?)
    }
}

ffrwd_node::export!(Blend);
