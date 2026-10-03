# 3. A detector

A detector is a node that reads a picture and writes rows that describe what
it found. A row is one JSON object. A detector does not hand the picture
back. A node that draws the boxes, blurs them or writes them to a file reads
the picture from its original stream, and reads the rows from the detector.
This chapter builds `glow`, which finds the bright part of each frame and
writes a row that gives the box around that part.

## Rows out

An output of rows is a stream of messages. Each message is one JSON object,
and each is stamped with a pts. The rows about a frame are stamped with that
frame's pts, so a node that reads the same picture can pair each row with
the frame the row describes.

A detector writes one row per tick for each thing it sees. A tick is one
call to the node, and for `glow` there is one tick per frame. Each row says
only what is true at its tick. A thing that stays in view for a minute gets
a row on every tick of that minute, not a single row written a minute late.
That way, a node that reads the rows never waits for a row about a frame it
already has.

## start_t names the thing

Rows about one thing that is seen over many ticks carry two fields that name
the thing. `start_t` is the time at which the thing was first seen. `id` is
a number that tells apart two things first seen on the same tick. Any reader
can use `start_t` as a time. A span reducer, which merges the rows of one
thing into one row, groups the rows by `start_t`. A caption track starts its
cue at `start_t`.

The SDK, which is the library the module is built with, can keep track of
spans for the node. A span is the stretch of ticks over which one thing is
seen. On each tick, the node first tells the SDK the time of the tick, and
then tells the SDK each thing it saw. For each thing, the SDK returns the
span that the sighting belongs to. If no span is open, the SDK starts a new
span on this tick. A span ends when its thing goes unseen for more ticks
than the gap, which is the number of unseen ticks the node lets a span
survive. A span also ends when it has lasted as long as the longest span the
node allows. When the node writes a span into a row, the span adds its
`start_t` and its `id` to the row's fields.

@rust 03-detector/glow/src/lib.rs 1-25

@cpp cpp/03-detector/glow/src/glow.cpp 1-20

@js js/03-detector/glow/src/glow.js 1-3

@go go/03-detector/glow/main.go 1-27

## The node

@rust 03-detector/glow/src/lib.rs 42-85

@cpp cpp/03-detector/glow/src/glow.cpp 46-87

@js js/03-detector/glow/src/glow.js 22-54

@go go/03-detector/glow/main.go 48-98

The lines between this excerpt and the previous one hold the box finder,
which is plain pixel code. The full example file contains the box finder.

The SDK writes the JSON schema of the output from the fields of the row. A
float field becomes a `number`, a whole-number field becomes an `integer`,
and every field is required. The schema also allows fields that it does not
name. So a node that reads only some of the fields still matches the output.

@command glow-3.shape.txt

@json glow-3.shape.txt outputs.0

`glow` keeps its open spans from one tick to the next, so the result of each
tick depends on the ticks before it. A node like that is not pure. A pure
node is one in which every tick depends only on what the host, the program
that runs the node, hands it for that tick. The shape of `glow`, which lists
its ports and its clock, therefore does not declare the node pure. The host
runs `glow` as a single instance, which is one running copy of the node, one
tick at a time, in order. [Chapter 9](09-pure.md) makes `glow` pure.

## Calling it

When a function returns nothing but rows, its declaration gives the return
type as an array of records. The query can read only the fields that the
declaration names, for example in a `WHERE` over the rows. A node that reads
the rows is matched against the output's own schema instead of this
declaration ([chapter 4](04-reader.md)).

@sql 03-detector/run/glow.sql

@out 03-detector-glow.sql.compile.txt

In the plan, `[glows=out0]` gives a label to the rows that the port `glows`
writes. The label names a data edge, which is a connection in the plan that
carries rows instead of pictures or sound. The plan connects a data edge
like any other stream. When the rows are written to an `.ndjson` file, each
row gains two fields beside its own: the `pts` it was stamped with, and its
`time` in seconds:

@lines 03-detector-glows-jobs1.ndjson ndjson 1-3

## Spans from rows

`ffrwd.merge_spans` is a function that turns rows written one tick at a time
into one row per span. Rows that share a `start_t` belong to one span. The
span's row keeps the fields of the last row in the span. The span ends at
the end of the last tick that carried a row for it. A tick with no row for
the span is a gap inside the span, not the end of the span. When a span is
still open after `max_span` seconds, `merge_spans` writes the span's row as
it stands and continues with a new span. So `max_span` is also the longest
that a span's row can wait before it is written.

@sql 03-detector/run/spans.sql

@out 03-detector-spans.sql.compile.txt

In the plan, `merge_spans` is `rowmerge`, a node built into the host.
`rowmerge` runs in the same host as the node that writes the rows. The bars
of `testsrc.mp4` stay bright for all four seconds, so every row that `glow`
wrote joins one span:

@lines 03-detector-spans-jobs1.ndjson ndjson 1-1

## A row for the run

The rows on an output port form a stream that other nodes read. A node may
also write run rows, which go to the run itself instead of to another node.
The run reports a node's run rows beside the rows it reports about itself. A
sink writes run rows, because a sink has no output port to write anything
else on ([chapter 7](07-sink.md)). Run rows have a schema of their own,
which `--describe` reports as `rows_schema`. `glow` writes no run rows, so
its `rows_schema` is null.
