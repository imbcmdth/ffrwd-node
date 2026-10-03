use ffrwd_node::{Bound, Cue, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::Deserialize;

#[derive(Deserialize)]
struct Params {
    window: f64,
    hop: Option<f64>,
}

struct Level {
    a: u32,
    channels: usize,
    rate: f64,
}

/// How loud `samples` are, as a cue's text: their RMS in dB of full scale.
fn loudness(samples: &[f32]) -> String {
    let power =
        samples.iter().map(|s| (*s as f64).powi(2)).sum::<f64>() / samples.len().max(1) as f64;
    let db = 10.0 * power.log10();
    if db < -90.0 {
        "silence".to_owned()
    } else {
        format!("{db:.0} dB")
    }
}

impl Node for Level {
    const NAME: &'static str = "level";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"window":{"type":"number","exclusiveMinimum":0,"maximum":60,"default":2},"hop":{"type":["number","null"],"exclusiveMinimum":0,"maximum":60}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, bound: &Bound) -> Result<Shape> {
        let rate = bound
            .rate_of("a")
            .ok_or("level counts its window in samples, and the call gives `a` no sample rate")?;
        let window = rate.count(params.window) as u32;
        let stride = rate.count(params.hop.unwrap_or(params.window)) as u32;
        Ok(Shape::new()
            .input(
                Input::audio("a")
                    .clock()
                    .window(window, stride)
                    .sample_formats(&["f32"]),
            )
            .output(Output::rows("cues").schema::<Cue>())
            .pure())
    }

    fn init(_: Params, init: &Init) -> Result<Level> {
        let a = init.stream("a")?;
        let audio = a.audio_format().ok_or("`a` is an audio input")?;
        Ok(Level {
            a: a.id,
            channels: audio.channels.max(1) as usize,
            rate: audio.sample_rate as f64,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let Some(run) = tick.frame(self.a) else {
            return Ok(());
        };
        let bytes = tick.fetch(self.a, run.index);
        let samples: Vec<f32> = bytes
            .as_chunks::<4>()
            .0
            .iter()
            .map(|b| f32::from_le_bytes(*b))
            .collect();
        let start = tick.time_base().seconds(run.pts);
        let end = start + (samples.len() / self.channels) as f64 / self.rate;
        let cue = Cue::new(start, end, loudness(&samples));
        Ok(out.row("cues", run.pts, &cue)?)
    }
}

ffrwd_node::export!(Level);
