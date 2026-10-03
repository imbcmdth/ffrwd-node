# 3. A detector

A detector reads a picture and says what it found, as rows. It does not hand
the picture back: a node that draws the boxes, blurs them or writes them to
a file reads the picture from where it already is, and the rows from the
detector. This chapter builds `glow`, which finds the bright part of each
frame and writes a box around it.

## Rows out

An output of rows is a stream of JSON objects, one message each, every one
stamped with a pts. The rows of a frame are stamped with that frame's pts,
so a reader of the same picture pairs them with the frame they describe.

A detector writes one row per tick for each thing it sees, and says only
what is true at that tick. A thing that stays in view for a minute is a row
on every tick of that minute, not one row written a minute late. A reader
never waits for a row about a frame it already has.

## start_t names the thing

Rows about one thing seen over many ticks carry the time it was first seen
as `start_t`, and a number, `id`, that keeps apart two things first seen on
the same tick. `start_t` is a time every reader can use: a span reducer
groups the rows by it, and a caption track starts its cue there.

The SDK can keep the spans. The node marks each tick, then says what it saw;
it gets back the span the sighting belongs to, started on this tick when
none is open. A span ends when its thing goes unseen for more ticks than the
gap allows, or when it has run as long as the longest the node sets. A span
written into a row adds its `start_t` and its `id` to the row's fields.

@rust 03-detector/glow/src/lib.rs 1-25

## The node

@rust 03-detector/glow/src/lib.rs 42-85

The box finder between them is plain pixel code; the example holds it whole.

The SDK writes the output's schema from the row's own fields: a float is a
`number`, a whole number an `integer`, and every field is required. Other
fields are allowed, so a reader that names only some of them still matches.

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/glow.wasm --bound v
```

@json glow-3.shape.txt outputs.0

`glow` keeps its spans from one tick to the next, which ties each tick to
the ticks before it. So its shape does not say it is pure, and the host runs
it as one instance, one tick at a time, in order. [Chapter 9](09-pure.md)
makes it pure.

## Calling it

A function returning rows alone declares them as an array of records. The
fields it names are what the query can read of them, a `WHERE` over the rows
for one. A node that reads the rows is matched against the output's own
schema instead ([chapter 4](04-reader.md)).

@sql 03-detector/run/glow.sql

@out 03-detector-glow.sql.compile.txt

`[glows=out0]` labels the rows the port `glows` writes, and the label is a
data edge like any stream. Written to `.ndjson`, each row gets the `pts` it
was stamped with and its `time` in seconds beside its own fields:

@lines 03-detector-glows-jobs1.ndjson ndjson 1-3

## Spans from rows

`ffrwd.merge_spans` turns rows written a tick at a time into one row per
span. Rows sharing a `start_t` are one span, which keeps the last row's
fields and ends at the end of the last tick that carried one. A tick with no
row for it is a gap inside the span, not its end. A span still open after
`max_span` seconds is written as it stands and carries on as a new one, so
`max_span` is also the latest a span row leaves.

@sql 03-detector/run/spans.sql

@out 03-detector-spans.sql.compile.txt

The reducer is the host's own node, `rowmerge`, and runs beside the rows'
producer. The bars of `testsrc.mp4` stay bright the whole four seconds, so
every row `glow` wrote joins one span:

@lines 03-detector-spans-jobs1.ndjson ndjson 1-1

## A row for the run

Rows on an output are a stream that other nodes read. A node may also write
rows for the run itself, which the run reports beside its own; a sink has
nothing else to say ([chapter 7](07-sink.md)). Those rows have a schema of
their own, which `--describe` reports as `rows_schema`. `glow` writes none,
so its `rows_schema` is null.
