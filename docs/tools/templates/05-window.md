# 5. A window

Some work needs more than one frame at a time, such as measuring the
loudness of a second of sound, or comparing a picture with the picture
before it. A node's clock input is the input that decides when the node
runs, and a tick is one call to the node. The clock input declares how much
of its stream each tick sees, and how far the next tick moves on. This
chapter builds `level`, which writes a cue saying how loud each stretch of
sound was, and `still`, which finds the stretches in which the picture does
not move.

## Window and stride

The clock input's window is the number of frames, or samples of sound, that
one tick sees. The clock input's stride is the number of frames or samples
that one tick consumes, which is how far after this tick the next tick
starts. Every other input hands the node what falls in the tick's interval.
The interval runs from the tick's time to the next tick's time, so the
interval is one stride long.

| window and stride | what each tick sees |
|---|---|
| both 1 | one frame, as in a per-frame node |
| equal | one block, then the next block with no overlap: a tumbling window |
| stride smaller than the window | blocks that overlap by the window minus the stride: a hopping window |
| stride of 1 frame | the newest frames, moving on one frame at a time: a sliding window |

For video, a window of n frames hands the tick n frames, oldest first. For
sound, a window hands the tick a single frame that holds all of the window's
samples as one run, interleaved across the channels. The host, which is the
program that runs the node, builds that frame from whatever packets of sound
arrived, and cuts the samples at the window's edges. The last tick takes
whatever is left, which may be less than a full window.

## The bound rate

A window is counted in frames or samples, but the call's params give the
window's length in seconds. When the compiler asks the node for its shape,
which is the node's list of ports and its clock, the compiler passes the
rate of every stream that the call binds: the frame rate of a picture, or
the sample rate of a sound. So the node can turn seconds into an exact count
of frames or samples, whatever the rate of the stream bound to the node.

## level

@rust 05-window/level/src/lib.rs

@cpp cpp/05-window/level/src/level.cpp

@js js/05-window/level/src/level.js

@go go/05-window/level/main.go

At 44.1 kHz, a window of one second that hops every half second gives this
clock input:

@command level-hopping.shape.txt

@json level-hopping.shape.txt inputs.0

If the shape is asked for without a rate, `level` refuses, and the refusal
names the input. The compiler reports such a refusal at the place in the
query where the call is written. In a query, the compiler always knows the
sample rate of a sound, because the compiler passes the rate at which the
sound will reach the node.

The cue for a window is stamped with the time of the window's first sample.
The node writes the cue on the tick that saw the window. The cue counts as
on time, because the length of the window does not count as lateness. So the
output declares a latency of 0, where latency is how far behind the end of
its tick's interval a row may be stamped. The compiler adds the length of
the window separately when it reports how long each path waits.

@sql 05-window/run/level.sql

@out 05-window-level.sql.compile.txt

@out 05-window-level.sql.delays.txt

When rows with `start_t`, `end_t` and `text` fields are written beside a
picture, the rows become a subtitle track:

@lines 05-window-levels.vtt vtt 1-9

## Latency in seconds

A node may write a row about a time well before the tick on which the node
writes the row. `still` writes a row for each stretch in which the picture
holds still. The row is stamped with the time the stretch started, but
`still` writes the row only once the stretch has ended. An output declares
how far behind the end of its tick's interval a row may be stamped. That
distance, in seconds, is the output's latency. After each tick, the host
tells the output's readers that nothing more will come stamped earlier than
the end of the interval minus the latency. A row that breaks this promise
reaches its readers late, and the run reports the row as late.

`still` ends a stretch after `longest` seconds and starts a new one. The row
for a stretch leaves on the tick after the stretch's last frame. So the
latency of `still` is `longest` plus one frame. The node uses the bound rate
to turn that one frame into seconds.

@rust 05-window/still/src/lib.rs

@cpp cpp/05-window/still/src/still.cpp

@js js/05-window/still/src/still.js

@go go/05-window/still/main.go

The window is two frames, and the window slides one frame at a time, so each
tick sees a frame and the frame before it. The node asks for yuv420p, whose
first plane is the luma, at one byte per pixel. The node compares only the
luma plane.

At 15 frames a second, with `longest` of 2:

@command still.shape.txt

@json still.shape.txt outputs.0

@sql 05-window/run/still.sql

@out 05-window-still.sql.compile.txt

@out 05-window-still.sql.delays.txt

The bars in `smptebars.mp4` never move, so each stretch is cut off at two
seconds:

@lines 05-window-stills-jobs1.ndjson ndjson 1-2

A pure node is one whose every tick depends only on what the host hands it
for that tick. `still` keeps the open stretch from one tick to the next, so
`still` is not pure, just like the first version of `glow` in [chapter
3](03-detector.md). A node that has to remember what it saw runs as a single
instance, which is one running copy of the node.
