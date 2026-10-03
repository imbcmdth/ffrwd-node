# 2. A per-frame filter

A filter is a node that takes in one picture and hands back another, once
for every frame. The params of the call decide what the filter does to each
picture. This chapter builds two filters. `zoom` crops part of the picture
and scales the crop back up to fill the frame. `blend` mixes a second
picture over the first. When either filter has nothing to change, it hands
the input frame on untouched.

## Params

A node's params are the settings a query passes to the node, written as one
JSON object. The node declares a JSON Schema for that object. The compiler,
which turns a query into ffmpeg commands, checks each call in the query
against the schema. Before the node sees the params, the SDK, which is the
library the module is built with, reads them against the schema a second
time, with these rules:

- a param that the call leaves out takes the default the schema gives it;
- a param set to null counts as not set;
- a whole number given as `30.0` to an `integer` param reads as `30`.

The SDK refuses a param that the schema does not name, and a param that is
outside its type, its bounds or its `enum`. The refusal names the param.

In a query, the params are the function's value arguments. The query can
pass them by position or by name. The arguments that bind ports and the
arguments that set params may come in any order.

## zoom

`zoom` takes three params. `amount` is how far to zoom in. `x` and `y` give
the point to zoom in on, each as a fraction of the picture's width or
height. The crop is `1 / amount` of the picture's width and `1 / amount` of
its height. The crop is centred on the point, except that it is moved inward
where it would otherwise run past an edge of the picture.

The crop and the resize come from each SDK's frame module, which the node
depends on:

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

**The clock.** A node's clock decides when the node runs. On each tick of
the clock, the host calls the node once. The host is `ffrwd-wasm`, the
program that runs the module beside ffmpeg. Here the clock is the input `v`,
so one tick is one frame of `v`, and each tick hands the node that frame.
The frame's pixels arrive in the format that the input accepts, rgba. The
compiler converts the stream to rgba before the stream reaches the host. The
host hands the pixels to the node tightly packed: each row of pixels follows
the previous row directly, with no padding.

**The output follows the input.** The node declares its output to be like
`v`. An output declared this way is named `v` and takes the format and the
time base of the input `v`, so the output has the same size and the same
pixel format as the input. A filter that changes neither the size nor the
pixel format declares its output this way, and never needs to state a size.

**Passing a frame on.** At an `amount` of 1, the crop is the whole picture.
In that case the node fetches nothing and hands the input frame back as it
came. The host then sends the frame's original bytes on to the output,
without copying them into the module and back out. A frame passed on this
way must already be in the output's format. An output declared like its
input always meets that rule.

**New params while it runs.** The params of a call may change between ticks
while the query runs. The node receives the new params, and the next tick
uses them. If the new params are equal to the ones already in force, the
node never receives them. A node that cannot accept new params refuses them,
and the old params stay in force. If the new params would change the node's
shape, which is the node's set of ports and its clock, the host refuses them
before the node sees them.

The crop and the resize use the bilinear filter of the Python imaging
library Pillow, because the vision models that ffrwd runs were trained on
pictures resized that way. The frame module returns the result as three
separate planes: red, green and blue. The node passes a normalization that
leaves every value unchanged, so each plane holds plain eight-bit values.
The node then interleaves the three planes back into rgba.

The query:

@sql 02-filter/run/zoom.sql

@out 02-filter-zoom.sql.compile.txt

The call's params appear in the node's options:
`zoom=amount=3:x=0.25:y=0.5`. The query never sets `y`, so the compiler
fills it in from the `DEFAULT` in the function's declaration.

## blend

`blend` mixes the picture on its input `over` into the picture on its input
`v`. The param `mix` sets the amount: at 0 the output is `v` alone, and at 1
the output is `over` alone.

@rust 02-filter/blend/src/lib.rs

@cpp cpp/02-filter/blend/src/blend.cpp

@js js/02-filter/blend/src/blend.js

@go go/02-filter/blend/main.go

**Lockstep.** The input `over` is paired with the clock in lockstep.
Lockstep means that each tick hands the node the one frame of `over` whose
pts is exactly the pts of the clock's frame. Two pictures can be in lockstep
only when both come from the same source, and every node between that source
and this node hands out exactly one frame for every frame it takes in. The
compiler checks this. The compiler refuses a second picture that reaches the
node by any other path:

@sql 02-filter/run/hflip.sql

@out 02-filter-hflip.sql.compile.txt

ffmpeg's `hflip` filter makes no promise to hand out one frame for every
frame it takes in, so its picture cannot be lockstep with the source's
picture. `zoom` does make that promise. A query that blends the source with
a zoom of itself therefore compiles:

@sql 02-filter/run/blend.sql

@out 02-filter-blend.sql.compile.txt

Both nodes run in the same host, and the zoomed picture never leaves that
host. `zoom` writes to the label `n1`, and `blend` reads `n1` on its port
`over`.

**Like.** The input `over` also declares that it is like `v`, which means
that the host conforms `over` to `v`. The host scales each frame of `over`
to the size of `v` before the node sees the frame, so the two pictures
always line up byte for byte. A node that needs to read two pictures of
different sizes leaves this declaration out, and reads the size of each
stream when the host opens the node. The shape records the declaration on
the input:

@command blend.shape.txt

@json blend.shape.txt inputs.1

**Handing on a frame of another input.** At a `mix` of 1, the output is the
frame of `over` exactly as it came. The node hands that frame on by naming
its stream and its position in the tick, and stamps it with the pts and
duration of the clock's frame. A switch, which is a node that cuts between
pictures, shows a picture in the same way. So a switch never touches the
pixels of either picture.
