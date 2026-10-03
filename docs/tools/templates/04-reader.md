# 4. A reader of rows

A reader is a node that takes rows another node wrote and acts on them. A
reader might draw boxes, blur faces or show captions. This chapter builds
two readers. `band` darkens a band across the bottom of the picture while a
cue is showing, and fades the band in before the cue starts. `boxmask` turns
boxes into a matte the size of the picture, without reading a single pixel
of the picture.

## Pairing rows with the clock

A node's clock is the input that decides when the node runs. The host, which
is the program that runs the node, calls the node once per tick of the
clock, and a tick is usually one frame of the clock input. Every input that
is not the clock declares how its contents are paired with the clock's
ticks. A data input, which carries rows, pairs with a clock input in one of
two ways:

- **Lockstep.** The tick hands the rows stamped with exactly the clock
  frame's pts, one frame at a time. The rows a detector writes about the
  same picture pair this way.
- **By interval.** The tick hands every message whose pts falls in the
  tick's interval. The interval runs from the tick's time to the next tick's
  time. Rows whose times do not follow the picture's frames pair this way:
  cues, a schedule, the words a speech recogniser heard.

A data input may also take its messages as they arrive, without pairing them
to any tick. A sink reads its inputs this way ([chapter 7](07-sink.md)).
Pictures and sound paired by time, called held inputs, are the subject of
[chapter 8](08-held.md).

An input paired by interval holds back the tick until the node that produces
the rows has said that it has nothing more to send stamped inside that
interval. The producer says so with its progress, which is a time that the
host sends from the producer to the reader after every tick of the producer.
The input can also put a limit on the wait. The input's latency is the
longest it waits, in seconds. The wait is measured on the clock input: once
frames of the clock input have arrived `latency` seconds past the end of the
interval, the host stops waiting. A message that arrives later than that
comes with the next tick, and the run reports it as late.

`ahead` extends the interval at its end. Messages stamped up to `ahead`
seconds past the end of the interval are handed with that interval. `ahead`
serves a node that has to act before a given time, such as a fade that
starts before its cue.

## Rows as state

A cue that starts at one tick goes on showing at the next tick. A node that
keeps rows across ticks declares the input as state. For a state input, the
SDK, which is the library the module is built with, hands each row to the
node before the tick that the row arrives with, oldest row first. The node
keeps whatever it needs from the rows.

State rows also let a node that keeps rows stay pure. A pure node is one
whose every tick depends only on what the host hands it for that tick. The
host may spread a pure node over several worker threads, running one
instance of the node on each. An instance is one running copy of the node,
and each instance sees only some of the ticks. Before an instance runs a
tick, the host hands the instance the rows of every tick that the instance
did not process, oldest first. The SDK hands those rows to the node ahead of
the tick's own rows. As a result, every instance holds the same cues at the
same tick, whichever ticks the instance ran.

## band

@rust 04-reader/band/src/lib.rs

@cpp cpp/04-reader/band/src/band.cpp

@js js/04-reader/band/src/band.js

@go go/04-reader/band/main.go

The input `cues` sets its `ahead` to the call's `fade` param, so each cue
reaches the node one fade's length before the cue starts. The shape of the
input:

@command band.shape.txt

@json band.shape.txt inputs.1

The schema of the input's rows is the schema of a cue: `start_t`, `end_t`
and `text`. A producer matches the schema when every field that the schema
names is also one of the producer's fields, with a type that the schema
accepts. Any other fields the producer writes are ignored. So any rows that
carry those three fields will do.

`level`, from [chapter 5](05-window.md), writes one cue for every two
seconds of sound, saying how loud those two seconds were:

@sql 04-reader/run/band.sql

@out 04-reader-band.sql.compile.txt

`ffrwd explain --delays` shows what each node waits for:

@out 04-reader-band.sql.delays.txt

`level` has to hear two seconds of sound before it writes the cue for those
two seconds. `band` waits half a second past each tick for cues that start
in that half second. So the picture leaves the plan two and a half seconds
behind the source. The sound that is written beside the picture waits the
same two and a half seconds at the muxer, and the plan sizes the muxer's
wait to match.

## A picture read for its timing alone

`boxmask` makes a matte: a gray picture that is white inside each box and
black everywhere else. `boxmask` needs the picture's size and the time of
each frame, but never a pixel of the picture. An input that is read only for
its timing declares so in the node's shape, which lists the node's ports and
clock. For such an input, the host hands the node each frame's pts and
duration and the stream's info, but no pixel bytes. If the node fetches a
frame of that input, or hands a frame of that input on, the run ends with an
error that names the port.

@rust 04-reader/boxmask/src/lib.rs

@cpp cpp/04-reader/boxmask/src/boxmask.cpp

@js js/04-reader/boxmask/src/boxmask.js

@go go/04-reader/boxmask/main.go

The input `boxes` is lockstep with the picture, because `glow` stamps the
row about a frame with that frame's pts. The output is declared like `v`
with one field changed, the pixel format. So the matte always has the
picture's size.

@command boxmask.shape.txt

@json boxmask.shape.txt inputs.0.accepts

@json boxmask.shape.txt outputs.0.format

For an input read for its timing, the compiler leaves the stream in whatever
format its source already has. The compiler has ffmpeg scale that stream to
16x16 before ffmpeg sends it on, and tells the node the picture's real size.
When another node in the same host reads the same picture, as `glow` does
here, the timing input binds the stream that the other node reads instead.
That way, the picture crosses from ffmpeg to the host only once:

@sql 04-reader/run/boxmask.sql

@out 04-reader-boxmask.sql.compile.txt

`boxmask` names four fields in the schema of its input, and `glow` writes
six. Every field that `boxmask` names is one of `glow`'s fields, with a type
that `boxmask` accepts: an `integer` counts as a `number`. So the call
compiles. If the producer lacked a field that the reader names, or wrote
that field with another type, the compiler would refuse the call, and the
error would name both ports.
