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

@rust 03-detector/glow/src/lib.rs 87-129

@cpp cpp/03-detector/glow/src/glow_test.cpp

@js js/03-detector/glow/test/glow.test.js

@go go/03-detector/glow/main_test.go

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

@rust 09-pure/glow/src/lib.rs 44-94

@cpp cpp/09-pure/glow/src/glow.cpp 47-94

@js js/09-pure/glow/src/glow.js 22-64

@go go/09-pure/glow/main.go 51-108

A glow that lasts across blocks is several spans, one per block, and
`ffrwd.merge_spans` writes a row for each. A glow that comes and goes inside
a block is one span with gaps in it.

## The harness as several workers

The mock harness opens a node as the host does, and a test can open several
and hand the ticks around, each with its ordinal, as workers would be handed
them. A pure node writes the same rows whichever way the ticks fall:

@rust 09-pure/glow/src/lib.rs 96-154

@cpp cpp/09-pure/glow/src/glow_test.cpp

@js js/09-pure/glow/test/glow.test.js

@go go/09-pure/glow/main_test.go

The query runs the same at any number of workers:

@sql 09-pure/run/spans.sql

@lines 09-pure-spans-jobs1.ndjson ndjson 1-2

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
