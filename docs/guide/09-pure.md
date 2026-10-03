# 9. Staying pure

A node that runs on one worker runs at that worker's speed, however many the
machine has. A pure node runs on all of them, and its results come out the
same. This chapter says what pure promises, shows how the `glow` of [chapter
3](03-detector.md) breaks it, and makes it pure.

## What the host promises

A node that says it is pure promises that every tick depends only on what
the tick hands it, counting a state input's earlier rows as handed. In
return the host opens several instances of it and hands each one some of the
ticks. The results are put back in tick order before they leave, so a reader
sees one stream.

What each instance can rely on:

- the tick's frames, messages and packets, as for any node;
- the tick's ordinal: its number in the run, from 0, counted over every
  instance, so the same tick has the same number on every worker;
- the rows of every state input, including the ones from ticks another
  instance ran, before its own tick ([chapter 4](04-reader.md));
- a held input's feed record and the feeds that ended since its own previous
  call ([chapter 8](08-held.md));
- the params, and what it read from its streams when it opened.

What it cannot rely on is having seen the tick before this one.

A node that is not pure runs as one instance, a tick at a time, in order.
That is always correct and sometimes slow.

## How glow breaks it

The first `glow` keeps open spans from one tick to the next. On one worker,
a glow seen at every tick is one span. Over two workers, each instance sees
every other tick, opens its own span on its first, and the rows of one glow
carry two different `start_t`s. Its test, with ticks handed out the way two
workers would get them:

**Rust**

```rust
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
```

The host never spreads that `glow` over workers, because its shape does not
say it is pure. Saying pure with that body would give exactly those rows.

## Counting by ordinal

A span has to be something every worker can work out from the tick alone.
The new `glow` cuts time into blocks of `every` frames by the tick's
ordinal, and names a sighting by its block: the block's number is its `id`,
and the time of the block's first frame its `start_t`. That time is the
frame's pts less one frame for each tick into the block, counted in the
stream's own time base, so every row of a block carries exactly the same
`start_t`.

**Rust**

```rust
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
```

A glow that lasts across blocks is several spans, one per block, and
`ffrwd.merge_spans` writes a row for each. A glow that comes and goes inside
a block is one span with gaps in it.

## The harness as several workers

The mock harness opens a node as the host does, and a test can open several
and hand the ticks around, each with its ordinal, as workers would be handed
them. A pure node writes the same rows whichever way the ticks fall:

**Rust**

```rust
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
```

The query runs the same at any number of workers:

```sql
CREATE FUNCTION glow(v video_stream, threshold number DEFAULT 230, every number DEFAULT 30)
RETURNS STRUCT(start_t number, id number, x number, y number, w number, h number)[]
  AS 'glow.wasm', 'glow' LANGUAGE wasm;

COPY (
  SELECT ffrwd.merge_spans(glow(f.video[1]), max_span => 10)
  FROM input('testsrc.mp4') f
) TO 'spans.ndjson'
```

```ndjson
{"end_t":2.0,"h":240,"id":0,"start_t":0.0,"w":160,"x":0,"y":0}
{"end_t":4.0,"h":240,"id":1,"start_t":2.0,"w":170,"x":108,"y":0}
```

That is the run at `--jobs 1`; at `--jobs 4` it writes the same bytes.

## Ways to end up impure

Each of these ties a tick to something other than what it was handed.

- **Keeping what earlier ticks saw.** Open spans, a previous frame, a
  running total, a flag that says something already happened. Read a window
  of frames instead ([chapter 5](05-window.md)), or rows as state.
- **Counting calls.** A tick counter of the node's own counts only the ticks
  its instance ran. Count with the ordinal.
- **Keeping rows of an input that is not state.** Rows read per frame are
  handed once, to the instance that runs their tick. Declare the input as
  state, and every instance gets them.
- **Saying something once.** A row written "the first time" is written once
  per instance. Write it on the tick the record names, as `cutin` does with
  presence.
- **The wall clock, randomness, the network.** Each answers differently on
  each worker, and on each run.

Printing to the log is fine: it changes nothing the node emits.
