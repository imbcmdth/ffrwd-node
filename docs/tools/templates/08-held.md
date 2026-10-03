# 8. Held inputs

A live programme has pictures that come and go: a camera that connects
during the show, a replay that plays and then ends, a wall of feeds where
one feed drops out. A node reads pictures like these through held inputs. A
held input is a picture or sound input that the host, which is the program
that runs the node, keeps lined up with the node's clock. The clock decides
when the node runs, and each run of the node is one tick. At every tick, the
node reads from a held input what to show, and what the host knows about the
source currently connected to the input. This chapter builds `cutin`, a
switch that cuts to a feed while the feed is on, and `mosaic`, a compositor
that lays out any number of pictures in a grid.

## Holding a picture

At each tick, a held input hands the node the newest frame whose time is at
or before the tick's time. The host keeps the source buffered ahead of the
clock. While the source falls behind, the host repeats the last frame. When
the source catches up, the host skips forward. The host reports both the
repeats and the skips. A held input hands nothing before the first frame of
its feed is due to show, and nothing once the feed has ended. A held sound
input hands the tick's samples, or nothing while the source is behind. When
a held sound input hands nothing, the node fills the silence itself.

## The feed

A feed is one source as a held input sees it, from the tick on which the
source's first frame shows to the last tick on which the source's last frame
shows. If a source's pts go backwards, or jump forwards by more than a
second, the current feed ends and the next feed starts. If the clock jumps,
every feed ends.

The input's anchor is the rule that places a source's pts on the clock's
timeline. There are three anchors:

- **Shared clock.** The source's pts are already on the clock's own
  timeline, as the pts of a timed feeder are. A timed feeder is a program
  that sends a picture into the run stamped with the programme's own time.
  The source's first frame waits until the clock reaches that frame's time.
  A first frame whose time the clock has already passed shows at once.
- **First frame.** The source counts its pts from wherever the source
  started. The host holds `lead` seconds of the source, or all of the source
  if the source ends sooner. Then the host schedules the source's first
  frame to show `lead` seconds ahead of the clock, on one of the clock's
  ticks.
- **Tagged.** The input names a tag. A source whose stream tags set that tag
  to `1` uses the shared clock anchor, and any other source uses the first
  frame anchor. ffrwd's own switch uses the tag `smart_timed`.

Three more settings control a feed. `lead` is how many seconds ahead of the
clock a first frame feed is scheduled to start. `linger` keeps the last
frame showing for that many seconds after the source ends. `timeout` gives
up on a live source that has sent nothing for that many seconds of programme
time, and ends the source's feed.

## Feeds by port

A held input may name a param that carries a port number. A query can use
such an input in two ways. If the query binds a stream to the input, the
input reads that stream, and the host writes the port number it picked into
the param. If the query gives only the port number, the input binds no
stream. Instead, the host listens on that port on the loopback address from
the moment the run starts. Whatever program connects to the port and writes
NUT with raw video and PCM sound becomes the input's source, for as long as
the program stays connected. Each connection is one feed. If the held input
is declared like another input, the host scales each connection's picture on
the way in to that input's size. The host also converts each connection's
picture to the first pixel format the held input accepts.

## Groups

A group is a set of held inputs that arrive on one connection from one
source, such as the picture and the sound of one feeder. A feeder is a
program that sends pictures and sound into the run. The group's first
picture fixes one offset between source time and clock time, and that offset
applies to every input in the group. The feeds of a group's inputs start and
end on the same tick.

A data input, which carries rows, can also belong to a group. A data input
in a group arrives on the group's connection. The feeder writes a JSON data
stream beside its picture and sound, with pts counted from the same origin
as the picture's pts. Each row lands on the tick on which the picture with
the row's pts shows. A feeder uses this data stream to say something about
what it is sending.

## What the host knows of a feed

A feed's start is fixed when the host settles the offset between the
source's time and the clock's time. At every tick from the tick on which a
feed's start was fixed until the feed's end, a held input has a feed record.
The record holds these fields:

- **at:** the clock time at which the feed's first frame shows.
- **known:** the clock time of the tick on which the feed's start was fixed.
  For a first frame feed, `known` is `lead` seconds before `at`. For a timed
  feeder that arrives early, `known` may be several seconds before `at`. A
  countdown to the feed counts down from `known`.
- **first pts:** the pts of the feed's first frame, in the source's own time
  base. Together with `at`, `first pts` maps any pts of the source onto the
  clock.
- **ends:** the last tick on which the feed shows, once the host can tell.
  For a feed read from a stream, the host fills in `ends` on the first tick
  that is within `lead` seconds of the time the last frame is due to show.
  For a feed by port, the host fills in `ends` on the first tick after the
  connection closes. For a feed that stops sending, the host fills in `ends`
  once the feed's `timeout` runs out, if `linger` keeps the feed showing
  after that.

At every tick, a held input also lists the feeds that ended since the
instance's previous call. An instance is one running copy of the node. On an
instance's first call, the list holds every feed that ended before that
call. Each feed in the list has `ends` set to the last tick on which the
feed showed. The list holds every end, whether or not the host gave notice
of the end ahead of time. A timeout, a connection that closed with nothing
queued, and a clock jump all appear in the list.

## Presence from the record alone

A node that reports when a feed comes and goes could keep a flag from one
tick to the next. But then the node would not be pure, and only one instance
could run the node. A pure node is one whose every tick depends only on what
the host hands it for that tick. `cutin` reads the feed record instead, and
writes each presence row, which reports a change in the feed, on the one
tick that the record names:

- `coming`, on the tick on which the feed's start was fixed, when that tick
  comes before the feed shows;
- `on`, on the tick whose interval holds `at`, where a tick's interval runs
  from the tick's time to the next tick's time;
- `off`, on the tick after `ends`, which `cutin` finds in the list of ended
  feeds.

Each of those rows belongs to one tick, and one instance runs that tick. The
record reads the same on that tick, whichever instance runs the tick. So the
rows come out the same on any number of worker threads.

## cutin

`cutin` shows its programme input `v` until a feed is on. While a feed is
on, `cutin` shows the feed's own frame instead. In both cases, `cutin` hands
the frame on as it came, without touching its pixels. Between the tick on
which a feed's start is fixed and the feed's first frame, `cutin` draws a
bar across the bottom of the programme. The bar shrinks to nothing at the
moment of the cut, as a countdown. `cutin` writes presence rows, which
report when a feed is coming, on or off, on its output `presence`. Notes
that the feeder writes beside its picture also come out on `presence`,
beside the rows about the feed itself.

@rust 08-held/cutin/src/lib.rs

@cpp cpp/08-held/cutin/src/cutin.cpp

@js js/08-held/cutin/src/cutin.js

@go go/08-held/cutin/main.go

The shape, which lists the node's ports and clock, declares the feed and the
notes like this:

@command cutin-port.shape.txt

@json cutin-port.shape.txt inputs.1.pairing

@json cutin-port.shape.txt inputs.2.pairing

The input `feed` is declared like `v`, so the host scales each frame of the
feed to the size of `v`. Both inputs accept only rgba. So a frame of the
feed can leave on the output `v` as it came. The input `notes` arrives on
the group's connection and uses the group's offset. `notes` is paired by
interval, so each tick hands the notes whose pts fall in the tick's
interval.

When the call gives a port, the feed is whatever program connects to that
port. The compile listing names the port, and the node that listens on that
port:

@sql 08-held/run/cutin.sql

@out 08-held-cutin.sql.compile.txt

When the call gives a stream, the feed is that stream. Here the feed is the
first one and a half seconds of another file. The call sets `lead` to 1, so
the feed starts showing one second after its frames arrive:

@sql 08-held/run/presence.sql

@out 08-held-presence.sql.compile.txt

A run of this query writes three rows: `coming` when the feed's start is
fixed, `on` one second later, and `off` on the tick after the feed's last
frame. The time at which the start is fixed depends on when the second
file's first frames reach the host. So the times in the rows change from run
to run.

## mosaic

A compositor is a node that shows many pictures at once. `mosaic` takes any
number of pictures on one port, and holds each picture with the shared clock
anchor. `mosaic` ticks at the frame rate of the first picture. A grid cell
whose feed is down shows grey. A cell whose feed is up but has no frame yet
stays black.

@rust 08-held/mosaic/src/lib.rs

@cpp cpp/08-held/mosaic/src/mosaic.cpp

@js js/08-held/mosaic/src/mosaic.js

@go go/08-held/mosaic/main.go

A port that takes many streams cannot be the clock. So `mosaic` ticks at a
rate instead: the rate of the first stream bound to `v`, which the compiler
reads from that stream:

@command mosaic.shape.txt

@json mosaic.shape.txt clock

@json mosaic.shape.txt inputs.0.pairing

When the query passes an array to the port, the port binds every stream in
the array, in order:

@sql 08-held/run/mosaic.sql

@out 08-held-mosaic.sql.compile.txt

ffrwd's own packages include a switch, `ffrwd.switch.switch`, and a
compositor, `ffrwd.blitz.compose`. These two nodes are fuller versions of
`cutin` and `mosaic`. The packaged nodes mix sound in and out together with
the picture, write presence rows that drive an HTML page, and accept feeds
by port on every input.
