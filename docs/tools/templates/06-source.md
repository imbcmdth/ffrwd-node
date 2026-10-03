# 6. A source

A source reads nothing and makes streams: a test pattern, a page rendered
from HTML, a broadcast pulled from a relay. A query reads it in `FROM`, as
it reads a file. This chapter builds `bars`, which draws colour bars at a
rate, and `beat`, which makes a picture whenever it has one.

## Its own clock

A node with no input clock keeps time itself, in one of two ways.

- **A rate.** The node ticks so many times a second. The tick's pts is its
  number, from 0, in a time base of one over the rate. In a live run the
  ticks are paced to the wall clock; otherwise they come as fast as the
  outputs drain.
- **Self-clocked.** The node emits when it has something, and its outputs'
  pts are the timeline. A tick's pts is microseconds since the node's first
  call. The call may wait until there is something to emit, and the host
  calls again as soon as it returns. A source on the network is this kind.

An output of a source has no input to take its format from, so it states
one: a size and a pixel format, or a sample rate, channels and a sample
format.

## Bounded, and finished

A source says whether it ends by itself. One that does not is live: the
compiler plans the query as live, and something else ends it. One that does
says so with its last emission: the node finishes, the host makes the last
call, and every output ends.

## Relation rows

A source read in `FROM` is a table: one row per rendition, the way a
manifest's ladder is. The shape lists the rows as JSON objects, and each
output names the row it belongs to. A query picks a rendition by the row's
fields, `WHERE s.height = 720`.

## bars

`bars` ticks at `fps`. With `seconds` it is bounded and finishes on the
first tick at or past that time; without, it runs until its reader stops.

@rust 06-source/bars/src/lib.rs

Its whole shape, bounded:

@out bars-seconds.shape.txt

Declared `RETURNS source`, it is called in `FROM`, and the alias carries the
stream columns it makes:

@sql 06-source/run/bars.sql

@out 06-source-bars.sql.compile.txt

A source binds nothing, so its `-bound` list is empty.

## beat

`beat` makes a frame every `every` seconds of the wall clock, grey by the
second it was made in. It sleeps until the next frame is due, and stamps the
frame at the time it was due, in microseconds from its first call.

@rust 06-source/beat/src/lib.rs

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/beat.wasm --params '{"every":0.5}'
```

@json beat.shape.txt clock

It never ends by itself, so a query that reads it says where to stop:

@sql 06-source/run/beat.sql

@out 06-source-beat.sql.compile.txt

`WHERE s.t < 5` becomes the reader's `-to 5`. The reader takes five seconds
and closes, and a source whose reader has closed ends cleanly.

`beat` reads the wall clock and keeps time between calls, so it is not pure.
A source fed from outside the run seldom is.
