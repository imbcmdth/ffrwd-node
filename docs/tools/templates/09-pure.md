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

@rust 03-detector/glow/src/lib.rs 87-129

@cpp cpp/03-detector/glow/src/glow_test.cpp

@js js/03-detector/glow/test/glow.test.js

@go go/03-detector/glow/main_test.go

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

@rust 09-pure/glow/src/lib.rs 44-94

@cpp cpp/09-pure/glow/src/glow.cpp 47-94

@js js/09-pure/glow/src/glow.js 22-64

@go go/09-pure/glow/main.go 51-108

A glow that lasts across several blocks becomes several spans, one per
block, and `ffrwd.merge_spans` writes one row for each. A glow that
flickers on and off within one block is one span with gaps.

## Running the harness as several workers

The mock harness opens a node the way the host does. A test can open
several harnesses and distribute the ticks among them, each tick with its
ordinal, exactly as the host would distribute them among workers. A pure
node writes the same rows no matter how the ticks are distributed:

@rust 09-pure/glow/src/lib.rs 96-154

@cpp cpp/09-pure/glow/src/glow_test.cpp

@js js/09-pure/glow/test/glow.test.js

@go go/09-pure/glow/main_test.go

The query gives the same result at any number of workers:

@sql 09-pure/run/spans.sql

@lines 09-pure-spans-jobs1.ndjson ndjson 1-2

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
