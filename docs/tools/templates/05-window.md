# 5. A window

Some work needs more than one frame at a time: a loudness over a second of
sound, a picture compared with the one before it. The clock input says how
much each tick sees and how far the next tick moves. This chapter builds
`level`, which writes a cue saying how loud each stretch of sound was, and
`still`, which finds the stretches where the picture does not move.

## Window and stride

The clock input's window is how many frames, or samples of sound, a tick
sees. Its stride is how many the tick consumes, which is how far the next
tick starts after this one. Every other input hands what falls in the tick's
interval, one stride long.

| window and stride | the tick sees |
|---|---|
| 1 and 1 | one frame: a per-frame node |
| equal | a block, then the next block: tumbling |
| stride under the window | blocks that overlap by the difference: hopping |
| stride of 1 frame | the newest frames, moving one at a time: sliding |

A window of video is that many frames in the tick, oldest first. A window of
sound is one frame: the tick's samples as one run, interleaved, re-cut from
whatever packets arrived. The last tick takes whatever is left, which may be
less than a window.

## The bound rate

A window is counted in frames or samples, and the call's params say it in
seconds. The shape is asked with the rate of every stream the call binds:
the frame rate of a picture, the sample rate of a sound. So the node turns
seconds into a count exactly, at whatever rate it is bound.

## level

@rust 05-window/level/src/lib.rs

At 44.1 kHz, a window of one second hopping every half second is:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/level.wasm --params '{"window":1,"hop":0.5}' --bound '[{"input":"a","streams":[{"rate":{"num":44100,"den":1}}]}]'
```

@json level-hopping.shape.txt inputs.0

A call that gives no rate gets a refusal naming the input, which the
compiler reports where the call is written. The compiler always knows a
sound's rate: it is the rate the sound reaches the node at.

A cue for a window is stamped at the window's first sample, and leaves with
the tick that saw the window. That is on time: the window's own length is
not lateness, and the output declares none. The compiler adds the window to
what it says each path waits.

@sql 05-window/run/level.sql

@out 05-window-level.sql.compile.txt

@out 05-window-level.sql.delays.txt

Written beside a picture, rows of `start_t`, `end_t` and `text` are a
subtitle track:

@lines 05-window-levels.vtt vtt 1-9

## Latency in seconds

A node may write a row about a time well before the tick it writes it on.
`still` writes a row for each stretch where the picture holds, stamped at
the stretch's start, once the stretch ends. Its output declares how far
behind the end of its tick's interval a row may be stamped: its latency, in
seconds. Each tick, the host tells the output's readers that nothing more
will come stamped before the interval's end less that latency. A row that
breaks the promise reaches its readers late, and the run reports it.

`still` cuts a stretch at `longest` seconds and carries on with a new one,
and the row for a stretch leaves on the tick after its last frame. So its
latency is `longest` and one frame, which the bound rate turns into seconds.

@rust 05-window/still/src/lib.rs

The window is two frames sliding one at a time: each tick sees a frame and
the one before it. The node asks for yuv420p, whose first plane is the luma,
a byte a pixel, and compares only that.

At 15 frames a second, with `longest` of 2:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/still.wasm --params '{"longest":2}' --bound '[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]'
```

@json still.shape.txt outputs.0

@sql 05-window/run/still.sql

@out 05-window-still.sql.compile.txt

@out 05-window-still.sql.delays.txt

The bars of `smptebars.mp4` never move, so the stretches are cut at two
seconds:

@lines 05-window-stills-jobs1.ndjson ndjson 1-2

`still` keeps the open stretch from tick to tick, so it is not pure, like
the first `glow`. A node that has to remember what it saw runs as one
instance.
