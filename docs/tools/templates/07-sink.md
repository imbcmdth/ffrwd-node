# 7. A sink

A sink takes streams and sends them somewhere the query does not write a
file: a relay, an HTTP endpoint, a log. What it makes is its effect, and
rows saying how it went. This chapter builds `tally`, which counts the coded
packets of every stream it is handed and reports the counts when the streams
end.

## Packets by arrival

A packets input carries a stream as its encoder wrote it: each packet's
bytes, its pts and dts, its duration where known, and whether decoding can
start there. Packets come in decode order, each once, and a sink takes them
as they arrive, unpaired with any clock: its inputs pair by arrival.

A port declares how many streams it takes. `many` takes as many as the query
hands over, each with its own codec, time base and relation row, so one port
reads a whole ladder of renditions. A port may also name the codecs it
takes, and how much of each stream it needs: every packet, the keyframes, or
the first packet alone. A host may hand over more than a port asks for,
never less.

## A clock that only gives turns

A sink has no stream to keep time by, so it ticks at a rate. Over inputs
that all pair by arrival, the rate is a floor on how often the node gets a
turn, not a timeline. The host ticks when something has arrived that no tick
has taken, or one period after the last tick, so the node never runs ahead
of the wall clock with nothing to hand it. A network session uses those
turns to keep itself alive while no packet comes.

## The last call

Once every input has ended, a tick takes whatever arrived that no tick had
taken, and the last call follows at once. The last call happens exactly
once, it may carry nothing, and there is no separate flush: what the node
still holds leaves on it.

## tally

`tally` writes its counts as rows for the run, not on an output port, so its
shape declares no outputs. Those rows have a schema, which the node declares
beside its params.

@rust 07-sink/tally/src/lib.rs

@cpp cpp/07-sink/tally/src/tally.cpp

@js js/07-sink/tally/src/tally.js

@go go/07-sink/tally/main.go

Two optional ports of packets by arrival, each taking any number of streams,
and a rate:

@command tally.shape.txt

@json tally.shape.txt inputs.0

@json tally.shape.txt clock

`--describe` carries the schema of the run's rows:

@command tally.describe.txt

@json tally.describe.txt rows_schema

## Calling it

Declared `RETURNS sink`, it is what a `COPY` writes to. The `SELECT` names
the streams, and each binds the port of its kind:

@sql 07-sink/run/tally.sql

@out 07-sink-tally.sql.compile.txt

A port reading packets is handed the input's own stream, copied as it was
coded. `[@rows=out0]` labels the node's run rows, and the run prints them.
Each stream also carries the relation row and rendition it came from, which
a sink handed a ladder names its tracks by.

`WITH` options on the `COPY` shape an encoder the compiler puts in front of
the sink, as they would for a file:

@sql 07-sink/run/encoded.sql

@out 07-sink-encoded.sql.compile.txt
