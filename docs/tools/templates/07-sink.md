# 7. A sink

A sink is a node that takes streams and sends them somewhere other than a
file the query writes, such as a relay, an HTTP endpoint or a log. A sink
has no output streams. The result of a sink is its effect on the place it
sends to, plus rows that report how the work went. This chapter builds
`tally`, which counts the encoded packets of every stream it is handed, and
reports the counts when the streams end.

## Packets by arrival

A packets input carries a stream as its encoder wrote it. For each packet,
the input carries the packet's bytes, its pts and dts, its duration where
the duration is known, and whether decoding can start at that packet.
Packets come in decode order, and each packet comes once. A sink takes
packets as they arrive, without pairing them to the ticks of a clock. This
way of pairing an input is called pairing by arrival.

A port declares how many streams it takes. A port declared `many` takes as
many streams as the query hands it. Each of those streams has its own codec,
its own time base and its own relation row, which is the row of the source's
table that the stream came from. So one port can read a whole ladder of
renditions. A port may also name the codecs it accepts, and how much of each
stream it needs: every packet, only the keyframes, or only the first packet.
The host, which is the program that runs the node, may hand over more than a
port asks for, but never less.

## A clock that only gives turns

A node's clock decides when the host calls the node, and each call is one
tick. A sink has no stream to keep time by, so a sink ticks at a rate. When
all of a node's inputs pair by arrival, the rate is not a timeline. The rate
only sets a minimum: the node gets a turn at least that often. The host
ticks when something has arrived that no tick has taken yet, or when one
period has passed since the last tick. So the node never runs ahead of the
wall clock with nothing to hand it. A network session uses these turns to
keep its connection alive while no packets come.

## The last call

Once every input has ended, one more tick takes whatever arrived that no
tick had taken yet, and the last call follows at once. The last call happens
exactly once, and the last call may carry nothing. There is no separate
flush call. Whatever the node still holds has to leave on the last call.

## tally

`tally` writes its counts as run rows, which go to the run itself instead of
to an output port. So the shape of `tally`, which lists its ports and its
clock, declares no outputs. The run rows have a schema, which the node
declares beside the schema of its params.

@rust 07-sink/tally/src/lib.rs

@cpp cpp/07-sink/tally/src/tally.cpp

@js js/07-sink/tally/src/tally.js

@go go/07-sink/tally/main.go

The shape has two optional ports of packets paired by arrival, each taking
any number of streams, and a clock that ticks at a rate:

@command tally.shape.txt

@json tally.shape.txt inputs.0

@json tally.shape.txt clock

`--describe` includes the schema of the run rows:

@command tally.describe.txt

@json tally.describe.txt rows_schema

## Calling it

The declaration gives `tally` the return type `sink`, so `tally` is what a
`COPY` writes to. The `SELECT` names the streams, and each stream binds the
port of its own kind:

@sql 07-sink/run/tally.sql

@out 07-sink-tally.sql.compile.txt

A port that reads packets is handed the input file's own stream, copied as
it was encoded, with no decoding. In the plan, `[@rows=out0]` gives a label
to the node's run rows, and the run prints the rows that carry that label.
Each stream also carries the relation row and the rendition it came from. A
sink that is handed a ladder of renditions names its tracks by the relation
row of each stream.

`WITH` options on the `COPY` set up an encoder, which the compiler puts in
front of the sink, just as the options would for a file:

@sql 07-sink/run/encoded.sql

@out 07-sink-encoded.sql.compile.txt
