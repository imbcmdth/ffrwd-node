//! `dim`: every picture darkened by `amount`, 0 leaving it as it was and 1
//! making it black. The same node as `js/examples/dim.js` and
//! `go/examples/dim`.

use ffrwd_node::{Bound, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::Deserialize;

#[derive(Deserialize)]
struct Params {
    amount: f64,
}

struct Dim {
    v: u32,
    amount: f64,
}

/// Every colour byte of an rgba picture scaled by `1 - amount`, alpha kept.
fn darken(pixels: &mut [u8], amount: f64) {
    let keep = ((1.0 - amount.clamp(0.0, 1.0)) * 256.0).round() as u32;
    for pixel in pixels.chunks_exact_mut(4) {
        for channel in &mut pixel[..3] {
            *channel = ((*channel as u32 * keep) >> 8) as u8;
        }
    }
}

impl Node for Dim {
    const NAME: &'static str = "dim";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"amount":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(_: &Params, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .output(Output::like("v"))
            .pure()
            .one_to_one())
    }

    fn init(params: Params, init: &Init) -> Result<Dim> {
        Ok(Dim {
            v: init.stream("v")?.id,
            amount: params.amount,
        })
    }

    fn set_params(&mut self, params: Params) -> Result<()> {
        self.amount = params.amount;
        Ok(())
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(frame) = tick.frame(self.v) else {
            return Ok(());
        };
        if self.amount == 0.0 {
            return Ok(out.pass("v", self.v, &frame)?);
        }
        let mut pixels = tick.fetch(self.v, frame.index);
        darken(&mut pixels, self.amount);
        Ok(out.frame("v", frame.pts, frame.duration, pixels)?)
    }
}

ffrwd_node::export!(Dim);

#[cfg(test)]
mod tests {
    use super::*;
    use ffrwd_node::mock::Harness;
    use ffrwd_node::{BoundStream, Payload, Rational};

    fn open(params: &str) -> Harness<Dim> {
        let v = BoundStream::video("v", 0, 2, 1, "rgba", Rational::new(1, 30));
        Harness::new(params, vec![v]).unwrap()
    }

    #[test]
    fn a_quarter_left_of_every_colour() {
        let mut dim = open(r#"{"amount":0.75}"#);
        let tick = dim.tick(0).frame(0, 0, vec![200, 100, 0, 255, 8, 4, 2, 9]);
        let emitted = dim.process(&tick).unwrap();
        let [Payload::Frame { data, .. }] = emitted.on("v")[..] else {
            panic!("no frame")
        };
        assert_eq!(data, &[50, 25, 0, 255, 2, 1, 0, 9]);
    }

    #[test]
    fn nothing_to_dim_passes_the_picture() {
        let mut dim = open(r#"{"amount":0}"#);
        let emitted = dim.process(&dim.tick(3).frame(0, 3, vec![0; 8])).unwrap();
        assert!(matches!(
            emitted.on("v")[..],
            [Payload::Same { pts: 3, index: 0, .. }]
        ));
    }

    #[test]
    fn the_amount_changes_while_it_runs() {
        let mut dim = open("");
        dim.set_params(r#"{"amount":1}"#).unwrap();
        let emitted = dim.process(&dim.tick(0).frame(0, 0, vec![255; 8])).unwrap();
        let [Payload::Frame { data, .. }] = emitted.on("v")[..] else {
            panic!("no frame")
        };
        assert_eq!(data, &[0, 0, 0, 255, 0, 0, 0, 255]);
        assert!(dim.set_params(r#"{"amount":2}"#).is_err());
    }
}
