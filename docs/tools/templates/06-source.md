# 6. A source

A source is a node that has no inputs and makes streams. A source might make
a test pattern, a page rendered from HTML, or a broadcast pulled from a
relay. A query reads a source in its `FROM` clause, in the same way the
query reads a file. This chapter builds `bars`, which draws colour bars at a
fixed rate, and `beat`, which makes a picture whenever it has one ready.

## Its own clock

A node's clock decides when the host, which is the program that runs the
node, calls the node. Each call is one tick. A node that has no input to
serve as its clock keeps time itself, in one of two ways.

- **A rate.** The node ticks a fixed number of times a second. The pts of a
  tick is the tick's number, counted from 0, in a time base of one over the
  rate. In a live run, the host paces the ticks to the wall clock. In any
  other run, the ticks come as fast as the node's outputs are drained.
- **Self-clocked.** The node emits whenever it has something to emit, and
  the pts on the node's outputs are the timeline. The pts of a tick is the
  number of microseconds since the node's first call. The node's call may
  wait until there is something to emit, and the host calls the node again
  as soon as the call returns. A source that reads from the network is
  self-clocked.

A source's output has no input to take its format from, so the output states
a format of its own: a size and a pixel format for video, or a sample rate,
a channel count and a sample format for sound.

## Bounded, and finished

A source declares whether it ends by itself. A source that ends by itself is
bounded. A source that does not end by itself is live. For a live source,
the compiler plans the whole query as live, and something other than the
source has to end the run. A bounded source signals its end with its last
emission. The node marks the result of that tick as finished, the host makes
the node's last call, and every output of the node ends.

## Relation rows

A source read in `FROM` is a table with one row per rendition, in the way
that a streaming manifest lists a ladder of renditions. These rows are
called relation rows. The node's shape, which lists its ports and its clock,
lists the relation rows as JSON objects. Each output names the relation row
it belongs to. A query picks a rendition by the fields of the rendition's
row, for example with `WHERE s.height = 720`.

## bars

`bars` ticks at the rate that its `fps` param gives. When the call sets
`seconds`, `bars` is bounded, and `bars` finishes on the first tick at or
past that many seconds. When the call does not set `seconds`, `bars` runs
until whatever reads its output stops.

@rust 06-source/bars/src/lib.rs

@cpp cpp/06-source/bars/src/bars.cpp

@js js/06-source/bars/src/bars.js

@go go/06-source/bars/main.go

The whole shape of `bars` when the call makes it bounded:

@command bars-seconds.shape.txt

@body bars-seconds.shape.txt

The declaration gives `bars` the return type `source`, so the query calls
`bars` in `FROM`. The alias in the `FROM` clause carries the stream columns
that `bars` makes:

@sql 06-source/run/bars.sql

@out 06-source-bars.sql.compile.txt

A source binds no inputs, so its `-bound` list is empty.

## beat

`beat` makes a frame every `every` seconds by the wall clock. Each frame is
a shade of grey that depends on the wall-clock second in which the frame was
made. `beat` sleeps until the next frame is due. `beat` stamps the frame
with the time at which the frame was due, in microseconds since the node's
first call.

@rust 06-source/beat/src/lib.rs

@cpp cpp/06-source/beat/src/beat.cpp

**JavaScript**

`buildNode`, the SDK's build function, turns off the clocks that WASI
offers. `beat` reads the wall clock and waits on it, so the `build.js` of
`beat` keeps the clocks on.

@code js js/06-source/beat/src/beat.js

@go go/06-source/beat/main.go

@command beat.shape.txt

@json beat.shape.txt clock

`beat` never ends by itself, so a query that reads `beat` has to say where
to stop:

@sql 06-source/run/beat.sql

@out 06-source-beat.sql.compile.txt

The compiler turns `WHERE s.t < 5` into the option `-to 5` on the ffmpeg
that reads the source. That ffmpeg takes five seconds of the stream and then
closes. A source whose reader has closed ends cleanly.

A pure node is one whose every tick depends only on what the host hands it
for that tick. `beat` reads the wall clock and keeps time between calls, so
`beat` is not pure. A source that takes its content from outside the run is
seldom pure.
