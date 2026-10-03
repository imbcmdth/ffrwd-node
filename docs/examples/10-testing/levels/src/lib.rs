use ffrwd_node::{Bound, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::Deserialize;

#[derive(Clone, Copy, Deserialize)]
struct Params {
    black: u8,
    white: u8,
}

struct Levels {
    v: u32,
    params: Params,
}

/// `value` with `black` moved to 0 and `white` to 255.
fn stretch(value: u8, params: Params) -> u8 {
    let (black, white) = (params.black as f64, params.white as f64);
    ((value as f64 - black) * 255.0 / (white - black))
        .round()
        .clamp(0.0, 255.0) as u8
}

impl Node for Levels {
    const NAME: &'static str = "levels";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"black":{"type":"integer","minimum":0,"maximum":254,"default":16},"white":{"type":"integer","minimum":1,"maximum":255,"default":235}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, _: &Bound) -> Result<Shape> {
        if params.black >= params.white {
            return Err("levels needs `black` under `white`".into());
        }
        Ok(Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .output(Output::like("v"))
            .pure()
            .one_to_one())
    }

    fn init(params: Params, init: &Init) -> Result<Levels> {
        Ok(Levels {
            v: init.stream("v")?.id,
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
        if (self.params.black, self.params.white) == (0, 255) {
            return Ok(out.pass("v", self.v, &frame)?);
        }
        let mut pixels = tick.fetch(self.v, frame.index);
        for pixel in pixels.as_chunks_mut::<4>().0 {
            for channel in &mut pixel[..3] {
                *channel = stretch(*channel, self.params);
            }
        }
        Ok(out.frame("v", frame.pts, frame.duration, pixels)?)
    }
}

ffrwd_node::export!(Levels);

#[cfg(test)]
mod tests {
    use super::*;
    use ffrwd_node::mock::Harness;
    use ffrwd_node::{BoundStream, Payload, Rational};

    fn open(params: &str) -> Result<Harness<Levels>, String> {
        let v = BoundStream::video("v", 0, 2, 1, "rgba", Rational::new(1, 25));
        Harness::new(params, vec![v])
    }

    #[test]
    fn black_and_white_reach_the_ends() {
        let mut levels = open("").unwrap();
        let tick = levels
            .tick(0)
            .frame(0, 0, vec![16, 16, 16, 255, 235, 126, 235, 255]);
        let emitted = levels.process(&tick).unwrap();
        let [Payload::Frame { data, .. }] = emitted.on("v")[..] else {
            panic!("no frame: {emitted:?}")
        };
        assert_eq!(data, &[0, 0, 0, 255, 255, 128, 255, 255]);
    }

    #[test]
    fn the_full_range_passes_the_frame_on() {
        let mut levels = open(r#"{"black":0,"white":255}"#).unwrap();
        let emitted = levels
            .process(&levels.tick(0).frame(0, 0, vec![0; 8]))
            .unwrap();
        assert!(matches!(emitted.on("v")[..], [Payload::Same { .. }]));
    }

    #[test]
    fn params_change_between_ticks() {
        let mut levels = open("").unwrap();
        levels.set_params(r#"{"black":0,"white":255}"#).unwrap();
        let emitted = levels
            .process(&levels.tick(0).frame(0, 0, vec![0; 8]))
            .unwrap();
        assert!(matches!(emitted.on("v")[..], [Payload::Same { .. }]));
    }

    #[test]
    fn black_over_white_is_refused() {
        let err = open(r#"{"black":200,"white":100}"#).err().unwrap();
        assert!(err.contains("`black` under `white`"), "{err}");
        assert!(open(r#"{"black":-1}"#).is_err());
    }
}
