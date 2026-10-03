# 2. A per-frame filter

A filter takes a picture and hands back another, frame for frame, shaped by
the params of the call. This chapter builds two: `zoom`, which crops a part
of the picture and scales it back up to fill the frame, and `blend`, which
mixes a second picture over the first. Both hand a frame on untouched when
there is nothing to do.

## Params

A node's params are one JSON object, and the node declares their JSON
Schema. The compiler checks each call against it, and the SDK reads the
call's params against it again before the node sees them: a param left out
takes the schema's default, a param set to null is not set, and a whole
number given as `30.0` to an `integer` param reads as `30`. A param the
schema does not name, or one outside its type, bounds or `enum`, is refused
with the param named.

The query passes params as the function's value arguments, by position or by
name. Ports and values may come in any order.

## zoom

`zoom` takes `amount`, how far in to go, and `x` and `y`, the point to go in
on as fractions of the picture. Its crop is `1 / amount` of each side,
centred on that point as far as the picture allows.

**Rust**

```toml
ffrwd-frame = { git = "https://github.com/imbcmdth/ffrwd-frame", tag = "v0.1.1" }
```

@cpp cpp/02-filter/zoom/src/zoom.cpp 5-5

@js js/02-filter/zoom/src/zoom.js 2-2

**Go**

```go
import "github.com/imbcmdth/ffrwd-node/go/frame"
```

@rust 02-filter/zoom/src/lib.rs

@cpp cpp/02-filter/zoom/src/zoom.cpp

@js js/02-filter/zoom/src/zoom.js

@go go/02-filter/zoom/main.go

**The clock.** The input `v` is the clock, so the node ticks once per frame
of `v` and each tick hands that frame. Its pixels arrive in the format the
input accepts: the compiler converts the stream to rgba before it reaches
the host, and the host hands the bytes over tightly packed, row after row.

**The output follows the input.** An output declared like `v` is named `v`
and takes `v`'s format and time base: the same size, the same pixel format.
A filter that changes neither declares its output this way and never states
a size.

**Passing a frame on.** At an `amount` of 1 the crop is the whole picture.
The node then fetches nothing and hands the input frame back as it came: the
host sends the frame's own bytes on without copying them into the module or
out again. A frame passed on this way has to be in the output's format,
which an output like its input always is.

**New params while it runs.** A call's params may change between ticks. The
node takes the new ones and the next tick uses them. Params equal to the
ones in force never reach the node, and a node that cannot take new params
refuses them, which leaves the old ones in force. A change that would change
the node's shape is refused by the host before the node sees it.

The crop and the resize are Pillow's bilinear, which is what the vision
models ffrwd runs were trained on, and they hand back planar red, green and
blue. With a normalization that scales nothing, the planes hold plain
eight-bit values, which the node interleaves back into rgba.

The query:

@sql 02-filter/run/zoom.sql

@out 02-filter-zoom.sql.compile.txt

The call's params land in the node's options: `zoom=amount=3:x=0.25:y=0.5`.
`y` was never written, and the compiler fills it in from the declaration's
`DEFAULT`.

## blend

`blend` mixes `over` into `v` by `mix`: 0 is `v` alone, 1 is `over` alone.

@rust 02-filter/blend/src/lib.rs

@cpp cpp/02-filter/blend/src/blend.cpp

@js js/02-filter/blend/src/blend.js

@go go/02-filter/blend/main.go

**Lockstep.** `over` is paired lockstep with the clock: each tick hands the
frame of `over` at exactly the clock's pts, one frame per tick. That holds
only when both pictures come from one source, through nodes that hand one
frame out for every frame in, so the compiler checks it. A second picture
that reaches the node by another path is refused:

@sql 02-filter/run/hflip.sql

@out 02-filter-hflip.sql.compile.txt

ffmpeg's `hflip` makes no promise about frames in and out, so its picture
cannot be lockstep with the source's. `zoom` does promise it, and the query
that blends the source with a zoom of itself compiles:

@sql 02-filter/run/blend.sql

@out 02-filter-blend.sql.compile.txt

Both nodes run in one host, and the zoomed picture never leaves it: `zoom`
writes `n1`, and `blend` reads `n1` on its port `over`.

**Like.** `over` also declares that it is conformed to `v`. The host scales
`over` to `v`'s size before the node sees it, so the two pictures always
line up byte for byte. A node that reads two pictures of different sizes
leaves that out and reads each stream's size when it opens. The shape says
it on the input:

@command blend.shape.txt

@json blend.shape.txt inputs.1

**Handing on a frame of another input.** At a `mix` of 1 the output is
`over`'s frame as it came. The node hands it on by its stream and its place
in the tick, stamped with the clock frame's pts and duration. This is how a
switch shows a feed: it never touches the pixels of either picture.
