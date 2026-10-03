# 8. Held inputs

A live programme has pictures that come and go: a camera that connects
during the show, a replay that plays and ends, a wall of feeds where one
drops out. A node reads those as held inputs. The host keeps each one lined
up with the clock, and the node reads at every tick what to show and what
the host knows of the feed. This chapter builds `cutin`, a switch that cuts
to a feed while it is on, and `mosaic`, a compositor that lays any number of
pictures out in a grid.

## Holding a picture

A held input hands the newest frame at or before the tick. The host keeps
the source buffered ahead of the clock, repeats the last frame while the
source falls behind, skips forward when it catches up, and reports both. A
held input hands nothing before its feed's first frame shows and nothing
once the feed has ended. Held sound hands the tick's samples, or nothing
while the source is behind, and the node fills the silence.

## The feed

A feed is one source, from the tick its first frame shows on to the last
tick its last frame shows on. A source whose pts go backwards, or forwards
by more than a second, ends its feed and starts the next. A clock that jumps
ends every feed.

How a source's pts land on the clock is the input's anchor:

- **Shared clock.** The source's pts are on the clock's own timeline, as a
  timed feeder's are. Its first frame waits until the clock reaches it, and
  one the clock has already passed shows at once.
- **First frame.** The source counts from wherever it started. The host
  holds `lead` seconds of it, or all of it if it ends sooner, and then
  starts it `lead` seconds ahead of the clock, on the clock's grid.
- **Tagged.** Shared clock for a source whose tags carry the named tag set
  to `1`, first frame for any other. ffrwd's own switch reads `smart_timed`.

Three more settings shape a feed. `lead` is the seconds ahead of the clock a
first-frame feed starts. `linger` keeps the last frame showing that many
seconds after the source ends. `timeout` gives up on a live source that has
sent nothing for that many seconds of programme time, and ends its feed.

## Feeds by port

A held input may name a param that carries a port. Given a stream, the input
reads it, and the host writes the port it picked into the param. Given only
the port, it binds nothing: the host listens on that loopback port from the
moment the run starts, and whatever connects and writes a NUT of raw video
and PCM is the input's source for as long as it stays. Each connection is a
feed. Its picture is conformed on the way in, to the size of the input it
follows and the first pixel format the input accepts.

## Groups

Held inputs of one group arrive on one connection from one source: a
feeder's picture and its sound. The group's first picture fixes one offset
for all of them, and their feeds start and end on the same tick.

A data input on a group arrives on that connection too. The feeder writes a
JSON data stream beside its picture and sound, its pts counted on the same
origin as the picture's, and each row lands on the tick the picture at its
pts shows on. That is how a feeder says something about what it is sending.

## What the host knows of a feed

At every tick a held input has a feed record, from the tick its start was
fixed until its end:

- **at:** the clock time the feed's first frame shows at.
- **known:** the clock time of the tick its start was fixed on. For a
  first-frame feed, `lead` before `at`. For a timed feeder that arrives
  early, seconds before `at`. A countdown counts from here.
- **first pts:** the source's own pts of its first frame, which with `at`
  maps any of its pts onto the clock.
- **ends:** the last tick it shows on, once the host can tell. A feed read
  from a stream is told on the first tick its last frame's turn is within
  `lead` of. A feed by port is told on the first tick after its connection
  closes. A feed that stops sending is told once its `timeout` runs out, if
  `linger` keeps it.

And at every tick a held input lists the feeds that ended since the
instance's previous call, every one before on its first call, each with
`ends` set to the last tick it showed on. That list holds every end, told
ahead or not: a timeout, a connection closed with nothing queued, a clock
jump.

## Presence from the record alone

A node that says when a feed comes and goes could keep a flag from tick to
tick. Then only one instance could run it. `cutin` reads the record instead,
and writes each row on the one tick the record names:

- `coming` on the tick the start was fixed, when that is before the feed
  shows;
- `on` on the tick whose interval holds `at`;
- `off` on the tick after `ends`, from the list of ended feeds.

Each of those is one tick, run by one instance, and the record reads the
same there whichever instance runs it. So the rows come out the same on any
number of workers.

## cutin

`cutin` shows its programme `v` until a feed is on, and the feed's own frame
while it is, handing each on as it came. Between a feed's start being fixed
and its first frame, it draws a bar across the foot of the programme that
runs down to the cut. Notes the feeder writes beside its picture come out on
`presence` beside the feed's own rows.

@rust 08-held/cutin/src/lib.rs

The feed and the notes, as the shape has them:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/cutin.wasm --params '{"port":9100}' --bound '[{"input":"v","streams":[{"rate":{"num":15,"den":1}}]}]'
```

@json cutin-port.shape.txt inputs.1.pairing

@json cutin-port.shape.txt inputs.2.pairing

`feed` is conformed to `v`, so a frame of the feed can leave on `v` as it
came. `notes` arrives on the group's connection, by interval, with the
group's offset.

Given a port, the feed is whatever connects there. The compile listing names
the port and the node that listens on it:

@sql 08-held/run/cutin.sql

@out 08-held-cutin.sql.compile.txt

Given a stream, the feed is that stream. Here it is the first second and a
half of another file, started a second after it arrives:

@sql 08-held/run/presence.sql

@out 08-held-presence.sql.compile.txt

A run of it writes three rows: `coming` when the feed's start is fixed, `on`
a second later, and `off` on the tick after the feed's last frame. When the
start is fixed depends on when the second file's first frames reach the
host, so the times move from run to run.

## mosaic

A compositor holds many pictures at once. `mosaic` takes any number of
pictures on one port, holds each on the shared clock, and ticks at the rate
of the first. A cell whose feed is down shows grey, and a cell whose feed is
up but has no frame yet stays black.

@rust 08-held/mosaic/src/lib.rs

A port that takes many streams cannot be the clock, so the node ticks at a
rate, here the rate of `v`'s first stream, which the compiler reads off it:

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/mosaic.wasm --params '{"columns":3,"width":960,"height":240}' --bound v,v,v
```

@json mosaic.shape.txt clock

@json mosaic.shape.txt inputs.0.pairing

An array written to the port binds every stream in it, in order:

@sql 08-held/run/mosaic.sql

@out 08-held-mosaic.sql.compile.txt

The switch and compositor in ffrwd's own packages, `ffrwd.switch.switch` and
`ffrwd.blitz.compose`, are these two nodes grown up: sound mixed in and out
with the picture, presence that drives an HTML page, feeds by port for every
input.
