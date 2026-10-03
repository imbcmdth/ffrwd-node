# 4. A reader of rows

A reader takes rows another node wrote and acts on them: draws boxes, blurs
faces, shows captions. This chapter builds `band`, which darkens a band at
the foot of the picture while a cue is showing and fades it in ahead of the
cue, and `boxmask`, which turns boxes into a matte the size of the picture
without reading a pixel of it.

## Pairing rows with the clock

Every input that is not the clock says how it pairs with the clock. A data
input pairs in one of two ways with a clock input:

- **Lockstep.** The rows stamped at exactly the clock's pts, frame for
  frame. A detector's rows over the same picture pair this way.
- **By interval.** Every message whose pts falls in the tick's interval,
  from the tick's time to the next tick's. Rows on a clock of their own pair
  this way: cues, a schedule, the words a recogniser heard.

A data input may also take its messages as they arrive, unpaired, which is a
sink's way ([chapter 7](07-sink.md)). Pictures and sound held by time are
[chapter 8](08-held.md).

An interval input holds the tick until its producer has said it has nothing
more to send stamped in that interval. A producer says so with its progress,
which the host sends down the edge after every tick. The input can also
bound the wait: its latency is the most it waits, in seconds, counted on the
clock input's own arrival. A message later than that comes with the next
tick, and the run reports it.

`ahead` widens the interval at its end. Messages stamped up to that many
seconds past the interval come with it, for a node that has to act before a
time: a fade that starts before its cue.

## Rows as state

A cue that starts at one tick goes on showing at the next. A node that keeps
rows across ticks declares the input as state. The SDK then hands each row
to the node before the tick it arrives with, oldest first, and the node
keeps what it needs.

State rows are also what lets such a node stay pure. When the host spreads
the node over workers, each instance sees only some of the ticks. Before an
instance's tick, the host hands it the rows of every tick it did not
process, oldest first, and the SDK hands them to the node ahead of the
tick's own. Every instance holds the same cues at the same tick, whichever
ticks it ran.

## band

@rust 04-reader/band/src/lib.rs

@cpp cpp/04-reader/band/src/band.cpp

@js js/04-reader/band/src/band.js

@go go/04-reader/band/main.go

The cue input carries `ahead` from the call's `fade`, so a cue arrives a
fade's length before it starts. Its shape:

@command band.shape.txt

@json band.shape.txt inputs.1

Its rows' schema is the cue's own: `start_t`, `end_t` and `text`. A producer
matches it when every field the schema names is among the producer's, with a
type it takes. Fields beyond them pass by, so any rows carrying those three
will do.

`level`, from [chapter 5](05-window.md), writes a cue for every two seconds
of sound saying how loud it was:

@sql 04-reader/run/band.sql

@out 04-reader-band.sql.compile.txt

`ffrwd explain --delays` says what each node waits for:

@out 04-reader-band.sql.delays.txt

`level` hears two seconds before it writes a cue for them, and `band` waits
half a second past each tick for cues that start then. So the picture leaves
two and a half seconds behind the source, and the sound written beside it
waits as long at the muxer, which the plan sizes.

## A picture read for its timing alone

`boxmask` makes a gray matte, white inside each box and black elsewhere. It
needs the picture's size and the time of each frame, and never a pixel. An
input read for its timing alone says so. The host hands its frames' pts and
durations and the stream's info, and no bytes. Fetching one of its frames,
or handing it on, ends the run with the port named.

@rust 04-reader/boxmask/src/lib.rs

@cpp cpp/04-reader/boxmask/src/boxmask.cpp

@js js/04-reader/boxmask/src/boxmask.js

@go go/04-reader/boxmask/main.go

The rows are lockstep with the picture: `glow` stamps a row for a frame with
that frame's pts. The output is like `v` with one field changed, its pixel
format, so the matte is always the picture's size.

@command boxmask.shape.txt

@json boxmask.shape.txt inputs.0.accepts

@json boxmask.shape.txt outputs.0.format

The compiler hands a timing input the stream in whatever format its source
already has, scaled to 16x16 on the way out of ffmpeg, and tells the node
the picture's own size. Where another node in the same host reads the same
picture, as `glow` does here, the timing input binds that stream instead, so
the picture crosses once:

@sql 04-reader/run/boxmask.sql

@out 04-reader-boxmask.sql.compile.txt

`boxmask` names four fields, and `glow` writes six. Every field `boxmask`
names is among `glow`'s with a type it takes, an `integer` being a `number`,
so the call compiles. A field missing from the producer, or of another type,
is refused at compile time, naming both ports.
