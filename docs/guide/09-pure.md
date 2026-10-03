# 9. Staying pure

The host can run a node on one worker thread or on several. A node that is
not pure runs on one worker, one tick at a time, no matter how many cores
the machine has. A pure node can be spread across all of them, and the host
guarantees that the output is the same as if the node had run on one. This
chapter explains what "pure" promises, shows how the `glow` detector from
[chapter 3](03-detector.md) breaks that promise, and fixes it.

## What the host promises

A node declares itself pure in its shape. By doing so it promises that the
result of every tick depends only on what the host handed it for that tick.
For an input whose rows are kept as state, the rows of earlier ticks count
as handed, since the host delivers them before the tick runs.

In return, the host opens several instances of the node and gives each
instance a share of the ticks. The results are put back into tick order
before they leave the node, so whatever reads the node's output sees one
stream, as if one instance had produced it.

An instance can rely on these things being the same no matter which worker
it runs on:

- the frames, messages and packets of its tick, as for any node;
- the tick's ordinal: the tick's number in the run, counted from 0 across
  every instance, so a given tick has the same number on every worker;
- the rows of every state input, including rows from ticks that another
  instance ran, delivered before the instance's own tick ([chapter
  4](04-reader.md));
- the feed record of each held input, and the list of feeds that ended since
  this instance's previous call ([chapter 8](08-held.md));
- the parameters, and what the instance read from its streams when it was
  opened.

The one thing an instance cannot rely on is having seen the previous tick.
Another instance may have run it.

A node that does not declare itself pure runs as a single instance, one tick
at a time, in order. That is always correct. It is slow when the node's work
per tick is heavy and the machine has cores to spare.

## How glow breaks the promise

The first version of `glow` keeps its open spans from one tick to the next:
when a glow is still visible on the next tick, the node extends the span it
already started. On one worker this works, and a glow that lasts ten ticks
is one span. On two workers, each instance sees every other tick, and each
instance starts its own span the first time it sees the glow. The rows for
one glow then carry two different `start_t` values, one per instance. The
test below shows this. It hands the ticks to two instances the way two
workers would receive them:

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

**C++**

```cpp
#include "glow.cpp"

#include <algorithm>
#include <string>
#include <vector>

#include "check.hpp"
#include "ffrwd/mock.hpp"

/// The `start_t` of every row of four lit ticks handed to `workers`
/// instances in turn, in pts order.
std::vector<std::string> starts(std::size_t workers) {
    auto open = [] {
        auto v = ffrwd::BoundStream::video("v", 0, 2, 2, "rgba", ffrwd::Rational(1, 10));
        return CHECK_OK(ffrwd::mock::Harness<GlowNode>::open("", {v}));
    };
    std::vector<ffrwd::mock::Harness<GlowNode>> instances;
    for (std::size_t n = 0; n < workers; ++n) instances.push_back(open());
    std::vector<std::pair<std::int64_t, std::string>> rows;
    for (std::int64_t n = 0; n < 4; ++n) {
        auto& worker = instances[std::size_t(n) % workers];
        auto tick = worker.tick(n).ordinal(std::uint64_t(n)).frame(0, n, ffrwd::Bytes::filled(16, 255));
        for (auto& row : CHECK_OK(worker.process(tick)).messages("glows")) rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end());
    std::vector<std::string> starts;
    for (const auto& [pts, row] : rows) starts.push_back(row.substr(0, row.find(',')));
    return starts;
}

TEST(spans_kept_across_ticks_split_with_the_workers) {
    CHECK(starts(1) == std::vector<std::string>(4, R"({"start_t":0.0)"));
    CHECK(starts(2) == (std::vector<std::string>{R"({"start_t":0.0)", R"({"start_t":0.1)",
                                                 R"({"start_t":0.0)", R"({"start_t":0.1)"}));
}
```

**JavaScript**

```js
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { BoundStream, Rational } from '@ffrwd/node';
import { Harness } from '@ffrwd/node/mock';

import { node } from '../src/glow.js';

/** The `start_t` of every row of four lit ticks handed to `workers`
 * instances in turn, in pts order. */
function starts(workers) {
  const open = () => new Harness(node, '', [BoundStream.video('v', 0, 2, 2, 'rgba', new Rational(1, 10))]);
  const instances = Array.from({ length: workers }, open);
  const rows = [];
  for (let n = 0; n < 4; n += 1) {
    const worker = instances[n % workers];
    const tick = worker.tick(n).ordinal(n).frame(0, n, new Uint8Array(16).fill(255));
    rows.push(...worker.process(tick).messages('glows'));
  }
  rows.sort(([a], [b]) => a - b);
  return rows.map(([, row]) => row.split(',')[0]);
}

test('spans kept across ticks split with the workers', () => {
  assert.deepEqual(starts(1), Array(4).fill('{"start_t":0'));
  assert.deepEqual(starts(2), ['{"start_t":0', '{"start_t":0.1', '{"start_t":0', '{"start_t":0.1']);
});
```

**Go**

```go
package main

import (
	"bytes"
	"reflect"
	"sort"
	"strings"
	"testing"

	node "github.com/imbcmdth/ffrwd-node/go"
	"github.com/imbcmdth/ffrwd-node/go/mock"
)

// starts is the start_t of every row of four lit ticks handed to workers
// instances in turn, in pts order.
func starts(t *testing.T, workers int) []string {
	t.Helper()
	open := func() *mock.Harness[Params] {
		v := node.VideoStream("v", 0, 2, 2, "rgba", node.R(1, 10))
		h, err := mock.Open(Definition, "", v)
		if err != nil {
			t.Fatal(err)
		}
		return h
	}
	instances := make([]*mock.Harness[Params], workers)
	for n := range instances {
		instances[n] = open()
	}
	var rows []node.TimedText
	for n := range 4 {
		worker := instances[n%workers]
		tick := worker.Tick(int64(n)).
			WithOrdinal(uint64(n)).
			WithFrame(0, int64(n), bytes.Repeat([]byte{255}, 16))
		emitted, err := worker.Process(tick)
		if err != nil {
			t.Fatal(err)
		}
		rows = append(rows, emitted.Messages("glows")...)
	}
	sort.Slice(rows, func(a, b int) bool {
		if rows[a].Pts != rows[b].Pts {
			return rows[a].Pts < rows[b].Pts
		}
		return rows[a].Text < rows[b].Text
	})
	var firsts []string
	for _, row := range rows {
		first, _, _ := strings.Cut(row.Text, ",")
		firsts = append(firsts, first)
	}
	return firsts
}

func TestSpansKeptAcrossTicksSplitWithTheWorkers(t *testing.T) {
	alone := []string{`{"start_t":0`, `{"start_t":0`, `{"start_t":0`, `{"start_t":0`}
	if got := starts(t, 1); !reflect.DeepEqual(got, alone) {
		t.Fatalf("%q", got)
	}
	split := []string{`{"start_t":0`, `{"start_t":0.1`, `{"start_t":0`, `{"start_t":0.1`}
	if got := starts(t, 2); !reflect.DeepEqual(got, split) {
		t.Fatalf("%q", got)
	}
}
```

The host never spreads this version of `glow` across workers, because its
shape does not say it is pure. If the shape did say so, with this body, the
rows would come out exactly as the test shows.

## Counting by ordinal

For a span to be the same on every worker, every worker must be able to
work out which span a tick belongs to from the tick alone. The new `glow`
does this with the tick's ordinal. It divides time into blocks of `every`
frames, and a sighting is named by its block: the block number is the
row's `id`, and the time of the block's first frame is the row's `start_t`.
That time is the current frame's pts minus one frame for each tick since
the block began, counted in the stream's own time base. Every row in a
block therefore carries exactly the same `start_t`, whichever instance
wrote it.

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

**C++**

```cpp
struct GlowNode : ffrwd::Node<GlowNode, Params> {
    static constexpr std::string_view name = "glow";
    static constexpr std::string_view version = "0.2.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"threshold":{"type":"integer","minimum":0,"maximum":255,"default":230},"every":{"type":"integer","minimum":1,"default":30}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::size_t width = 0;
    std::uint8_t threshold = 0;
    std::uint64_t every = 1;
    /// One frame of the picture, in its time base.
    std::int64_t step = 1;

    static ffrwd::Result<ffrwd::Shape> shape(const Params&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .output(ffrwd::Output::rows("glows").schema<Glow>())
            .pure();
    }

    static ffrwd::Result<GlowNode> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        GlowNode node;
        node.v = v.id;
        node.width = video->width;
        node.threshold = params.threshold;
        node.every = params.every;
        if (v.hint.rate)
            node.step = std::max<std::int64_t>(v.info.time_base.pts(v.hint.rate->duration(1)), 1);
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        auto box = bright(pixels, width, threshold);
        if (!box) return {};
        auto [x, y, w, h] = *box;
        auto into = std::int64_t(tick.ordinal() % every);
        Glow glow{tick.time_base().seconds(frame->pts - into * step), tick.ordinal() / every, x, y, w, h};
        return out.row("glows", frame->pts, glow);
    }
};

FFRWD_EXPORT(GlowNode);
```

**JavaScript**

```js
export const node = defineNode({
  name: 'glow',
  version: '0.2.0',
  paramsSchema:
    '{"type":"object","properties":{"threshold":{"type":"integer","minimum":0,"maximum":255,"default":230},' +
    '"every":{"type":"integer","minimum":1,"default":30}},"additionalProperties":false}',

  shape() {
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .output(Output.rows('glows').schema(GLOW))
      .pure();
  },

  init({ threshold, every }, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    const width = video.width;
    // One frame of the picture, in its time base.
    const step = v.hint.rate === undefined ? 1 : Math.max(v.info.timeBase.pts(v.hint.rate.duration(1)), 1);
    return {
      process(tick, out) {
        const frame = tick.frame(v.id);
        if (frame === undefined) return;
        const pixels = tick.fetch(v.id, frame.index);
        const box = bright(pixels, width, threshold);
        if (box === undefined) return;
        const [x, y, w, h] = box;
        const into = tick.ordinal() % every;
        const glow = {
          start_t: tick.timeBase().seconds(frame.pts - into * step),
          id: Math.floor(tick.ordinal() / every),
          x,
          y,
          w,
          h,
        };
        out.row('glows', frame.pts, glow);
      },
    };
  },
});
```

**Go**

```go
var Definition = node.Definition[Params]{
	Name:         "glow",
	Version:      "0.2.0",
	ParamsSchema: `{"type":"object","properties":{"threshold":{"type":"integer","minimum":0,"maximum":255,"default":230},"every":{"type":"integer","minimum":1,"default":30}},"additionalProperties":false}`,
	Shape: func(Params, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Output(node.RowsOutput("glows").Schema(node.SchemaOf[Glow]())).
			Pure(), nil
	},
	Init: func(params Params, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		video := v.VideoFormat()
		if video == nil {
			return nil, errors.New("`v` is a video input")
		}
		step := int64(1)
		if rate := v.Hint.Rate; rate != nil {
			step = max(v.Info.TimeBase.Pts(rate.Duration(1)), 1)
		}
		return &GlowNode{
			v:         v.ID,
			width:     int(video.Width),
			threshold: params.Threshold,
			every:     params.Every,
			step:      step,
		}, nil
	},
}

func (g *GlowNode) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(g.v)
	if !ok {
		return nil
	}
	pixels := tick.Fetch(g.v, frame.Index)
	box, ok := bright(pixels, g.width, g.threshold)
	if !ok {
		return nil
	}
	into := int64(tick.Ordinal() % g.every)
	glow := Glow{
		StartT: tick.TimeBase().Seconds(frame.Pts - into*g.step),
		ID:     tick.Ordinal() / g.every,
		X:      box[0],
		Y:      box[1],
		W:      box[2],
		H:      box[3],
	}
	return out.Row("glows", frame.Pts, glow)
}

func init() { node.Export(Definition) }

func main() {}
```

A glow that lasts across several blocks becomes several spans, one per
block, and `ffrwd.merge_spans` writes one row for each. A glow that
flickers on and off within one block is one span with gaps.

## Running the harness as several workers

The mock harness opens a node the way the host does. A test can open
several harnesses and distribute the ticks among them, each tick with its
ordinal, exactly as the host would distribute them among workers. A pure
node writes the same rows no matter how the ticks are distributed:

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

**C++**

```cpp
#include "glow.cpp"

#include <algorithm>
#include <string>
#include <vector>

#include "check.hpp"
#include "ffrwd/mock.hpp"

ffrwd::mock::Harness<GlowNode> open() {
    auto v =
        ffrwd::BoundStream::video("v", 0, 4, 4, "rgba", ffrwd::Rational(1, 15)).rate(ffrwd::Rational(15, 1));
    return CHECK_OK(ffrwd::mock::Harness<GlowNode>::open(R"({"every":3})", {v}));
}

/// A picture lit at one pixel, which moves along the top row.
ffrwd::Bytes lit(std::size_t n) {
    ffrwd::Bytes pixels(4 * 4 * 4);
    std::fill(pixels.data() + (n % 4) * 4, pixels.data() + (n % 4) * 4 + 4, 255);
    return pixels;
}

/// Every row of `ticks` ticks handed to `workers` instances in turn, in
/// pts order.
std::vector<std::pair<std::int64_t, std::string>> rows(std::size_t ticks, std::size_t workers) {
    std::vector<ffrwd::mock::Harness<GlowNode>> instances;
    for (std::size_t n = 0; n < workers; ++n) instances.push_back(open());
    std::vector<std::pair<std::int64_t, std::string>> rows;
    for (std::size_t n = 0; n < ticks; ++n) {
        auto& worker = instances[n % workers];
        auto tick = worker.tick(std::int64_t(n)).ordinal(n).frame(0, std::int64_t(n), lit(n));
        for (auto& row : CHECK_OK(worker.process(tick)).messages("glows")) rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

TEST(any_number_of_workers_write_the_same_rows) {
    auto alone = rows(10, 1);
    CHECK_EQ(alone.size(), 10u);
    CHECK(rows(10, 2) == alone);
    CHECK(rows(10, 3) == alone);
}

TEST(a_sighting_starts_every_so_many_frames) {
    auto ids = rows(7, 1);
    CHECK(ids[2].second.starts_with(R"({"start_t":0.0,"id":0,)"));
    CHECK(ids[3].second.starts_with(R"({"start_t":0.2,"id":1,)"));
}
```

**JavaScript**

```js
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { BoundStream, Rational } from '@ffrwd/node';
import { Harness } from '@ffrwd/node/mock';

import { node } from '../src/glow.js';

function open() {
  const v = BoundStream.video('v', 0, 4, 4, 'rgba', new Rational(1, 15)).rate(new Rational(15, 1));
  return new Harness(node, '{"every":3}', [v]);
}

/** A picture lit at one pixel, which moves along the top row. */
function lit(n) {
  const pixels = new Uint8Array(4 * 4 * 4);
  pixels.fill(255, (n % 4) * 4, (n % 4) * 4 + 4);
  return pixels;
}

/** Every row of `ticks` ticks handed to `workers` instances in turn, in pts
 * order. */
function rows(ticks, workers) {
  const instances = Array.from({ length: workers }, open);
  const rows = [];
  for (let n = 0; n < ticks; n += 1) {
    const worker = instances[n % workers];
    const tick = worker.tick(n).ordinal(n).frame(0, n, lit(n));
    rows.push(...worker.process(tick).messages('glows'));
  }
  return rows.sort(([a], [b]) => a - b);
}

test('any number of workers write the same rows', () => {
  const alone = rows(10, 1);
  assert.equal(alone.length, 10);
  assert.deepEqual(rows(10, 2), alone);
  assert.deepEqual(rows(10, 3), alone);
});

test('a sighting starts every so many frames', () => {
  const ids = rows(7, 1).map(([, row]) => row);
  assert.ok(ids[2].startsWith('{"start_t":0,"id":0,'), ids[2]);
  assert.ok(ids[3].startsWith('{"start_t":0.2,"id":1,'), ids[3]);
});
```

**Go**

```go
package main

import (
	"reflect"
	"sort"
	"strings"
	"testing"

	node "github.com/imbcmdth/ffrwd-node/go"
	"github.com/imbcmdth/ffrwd-node/go/mock"
)

func open(t *testing.T) *mock.Harness[Params] {
	t.Helper()
	v := node.VideoStream("v", 0, 4, 4, "rgba", node.R(1, 15)).
		WithRate(node.R(15, 1))
	h, err := mock.Open(Definition, `{"every":3}`, v)
	if err != nil {
		t.Fatal(err)
	}
	return h
}

// lit is a picture lit at one pixel, which moves along the top row.
func lit(n int) []byte {
	pixels := make([]byte, 4*4*4)
	for at := (n % 4) * 4; at < (n%4)*4+4; at++ {
		pixels[at] = 255
	}
	return pixels
}

// rows is every row of ticks ticks handed to workers instances in turn, in
// pts order.
func rows(t *testing.T, ticks, workers int) []node.TimedText {
	t.Helper()
	instances := make([]*mock.Harness[Params], workers)
	for n := range instances {
		instances[n] = open(t)
	}
	var rows []node.TimedText
	for n := range ticks {
		worker := instances[n%workers]
		tick := worker.Tick(int64(n)).
			WithOrdinal(uint64(n)).
			WithFrame(0, int64(n), lit(n))
		emitted, err := worker.Process(tick)
		if err != nil {
			t.Fatal(err)
		}
		rows = append(rows, emitted.Messages("glows")...)
	}
	sort.Slice(rows, func(a, b int) bool {
		if rows[a].Pts != rows[b].Pts {
			return rows[a].Pts < rows[b].Pts
		}
		return rows[a].Text < rows[b].Text
	})
	return rows
}

func TestAnyNumberOfWorkersWriteTheSameRows(t *testing.T) {
	alone := rows(t, 10, 1)
	if len(alone) != 10 {
		t.Fatalf("%d rows", len(alone))
	}
	if got := rows(t, 10, 2); !reflect.DeepEqual(got, alone) {
		t.Fatalf("%v", got)
	}
	if got := rows(t, 10, 3); !reflect.DeepEqual(got, alone) {
		t.Fatalf("%v", got)
	}
}

func TestASightingStartsEverySoManyFrames(t *testing.T) {
	var ids []string
	for _, row := range rows(t, 7, 1) {
		ids = append(ids, row.Text)
	}
	if !strings.HasPrefix(ids[2], `{"start_t":0,"id":0,`) {
		t.Fatal(ids[2])
	}
	if !strings.HasPrefix(ids[3], `{"start_t":0.2,"id":1,`) {
		t.Fatal(ids[3])
	}
}
```

The query gives the same result at any number of workers:

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

That is the output at `--jobs 1`. At `--jobs 4` the output is byte for byte
the same.

## Ways to end up impure

Each of these makes a tick depend on something other than what the host
handed it.

- **Keeping what earlier ticks saw.** Open spans, the previous frame, a
  running total, a flag that records that something already happened. Read
  a window of frames instead ([chapter 5](05-window.md)), or keep rows as
  state.
- **Counting calls.** A counter the node keeps itself counts only the ticks
  its own instance ran. Use the tick's ordinal instead.
- **Keeping rows from an input that is not state.** Rows read per frame are
  handed once, to the instance that runs their tick. Declare the input as
  state, and every instance receives them.
- **Doing something once.** A row written "the first time" is written once
  per instance, so several times in all. Write it on the tick the record
  names, as `cutin` does with its presence rows.
- **The wall clock, random numbers, the network.** Each gives a different
  answer on each worker, and on each run.

Writing to the log is fine. It changes nothing the node emits.
