# ffrwd-node-cpp

A node is what an ffrwd module is: typed input
ports, typed output ports and a clock. The host calls it once a tick with
what each input holds for that tick, and it emits on its outputs. Its ports
follow from the call's params and from which inputs the call binds, and the
query's compiler reads them before anything runs.

This library is that world for C++. Derive a type from `ffrwd::Node`, hand
it to `FFRWD_EXPORT`, and build it with wasi-sdk for `wasm32-wasip2`. The
library carries the bindings and the rest of what each module would write
for itself: the call sequence, params read against their schema, shapes from
builders, time in any time base, rows of a state input folded, emissions
checked as they are made, and errors as the run's message.

Requires ffrwd 0.29, whose `ffrwd/wasm` is 0.19.1, and C++23.

## The node

```cpp
struct MyNode : ffrwd::Node<MyNode, MyParams> {
    static constexpr std::string_view name = "my-node";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema = R"({...})";  // NO_PARAMS unless given
    static constexpr std::string_view rows_schema = "";             // optional
    static constexpr std::array<std::string_view, 1> rows_language{"language"};  // optional

    static ffrwd::Result<ffrwd::Shape> shape(const MyParams&, const ffrwd::Bound&);
    static ffrwd::Result<MyNode> init(MyParams, const ffrwd::Init&);
    ffrwd::Status set_params(MyParams);               // refuses unless declared
    ffrwd::Status fold(const ffrwd::StateRow&);       // refuses unless declared
    ffrwd::Status process(const ffrwd::Tick&, ffrwd::Out&);
};

FFRWD_EXPORT(MyNode);
```

The constants are `describe`. `shape` answers at compile time, and again at
`init`, which the library calls with the shape resolved. `process` runs once
a tick; the last call is the one with `tick.last()` set, and there is no
separate flush. A node that takes no params leaves out the second template
argument and takes `ffrwd::NoParams`.

**Params.** The call's JSON is read against `params_schema` before any of
these sees it: an empty string is `{}`, a param set to null is not set, the
schema's defaults fill in what the call left out, a whole number given as
`30.0` to an `integer` param reads as `30`, and then the object is read into
`MyParams` by the fields it names:

```cpp
struct MyParams {
    double amount = 0.5;
    std::optional<double> fps;
    FFRWD_FIELDS(amount, fps)
};
```

A param outside the schema, or one that breaks its `type`, `enum`, bounds or
lengths, is refused with the param named. Params equal to the ones in force
never reach `set_params`.

**Shapes.** `Shape`, `Input` and `Output` build the ports the way a query
reads them:

```cpp
ffrwd::Shape()
    .input(ffrwd::Input::video("v").clock().window(15, 1).pixel_formats({"rgba"}))
    .input(ffrwd::Input::rows("boxes").interval().latency(2.0).state().schema<Box>())
    .input(ffrwd::Input::video("feed").optional().hold().lead(0.5).port_param("port").like("v"))
    .output(ffrwd::Output::video("mask").pixel_format("gray"))
    .output(ffrwd::Output::rows("spots").schema<Spot>())
    .pure()
```

An output with no format of its own takes the clock input's, one given only
a pixel format follows the clock input with that override, and
`Output::like("v")` is an output named `v` in input `v`'s format. Before the
host sees a shape the library settles its clock, leaves out every output
that follows an input the call does not bind, and refuses what the host
would refuse (a clock that is optional, many or not lockstep, a held data
input, a frame input paired by interval, a stride past its window, a data
input read for its timing, a data input on a group no hold input is in),
naming the port. What it hands the host is a `NodeShape`, field for field
the WIT's `node-shape`; `init.shape()` is the instance's.

**What the call binds.** `Bound` names the inputs a call binds, how many
streams each takes, and each stream's rate where the compiler knows it: a
frame rate for video, a sample rate for audio. `bound.has("a")`,
`bound.count("inputs")` and `bound.rate_of("v")` read it, and a rate turns
seconds into frames or samples and back: `rate.count(2.0)` is the samples
in two seconds, `rate.duration(1)` the length of one frame. The library
shapes the node again at `init` from the streams bound there, each carrying
the hint the compiler's `shape` was asked with, so the instance's shape is
the plan's.

**Time.** `Rational` is a time base or a rate: `seconds(pts)`,
`pts(seconds)`, `rescale(pts, to)` exactly as ffmpeg rounds, and
`Rational::approximate(29.97, 1001)` for a rate a param gives as a number.
`tick.seconds()` is the tick's time, and `out.pts(port, seconds)` a time on
a port, in the port's own time base or the clock's.

**State.** Before each `process`, every row on an input declared `state()`
reaches `fold`: the rows of ticks this instance did not process first,
oldest first, then the tick's own. A pure node spread over workers gets the
same state on each.

**Emitting.** `Out` checks every emission as it is made: the port is one
the shape declares, of the payload's kind, and its pts never go back,
within a call or across calls, nor a packet's dts. `out.pass("v", v, frame)`
hands an input frame back uncopied, `out.frame` sends new bytes,
`out.row` and `out.rows` send rows, `out.progress` a progress mark,
`out.report` a row for the run's rows output, and `out.finish()` ends a
generator.

**Bytes.** A frame's bytes are `ffrwd::Bytes`: one allocation, moved and
never copied unless `clone()` says so. `tick.fetch` hands the host's copy
over as it is, and `out.frame` hands it back the same way, so a filter that
draws on the picture it fetched sends it on without another copy.

**Rows.** `tick.rows<T>(id)` reads a data stream's messages, or the rows
riding a frame stream, as `T`. A row type names its fields with
`FFRWD_FIELDS`; numbers, strings, booleans, `std::optional`, `std::vector`,
`std::array`, `std::map` keyed by string, `ffrwd::Json` and other row types
read and write as JSON. `schema<T>()` writes a row type's JSON schema from
what `T{}` writes, `integer` and `number` kept apart. `Spans` names
per-tick rows by the span they belong to, with a gap a span survives and a
longest it may run; a `Span` field writes its `start_t` and its `id` in its
place, which `ffrwd.merge_spans` keys a span by, so two things first seen on
one tick stay two spans; `Cue` is a query's `cue`, and `Cues` holds cues
from the tick they arrive on until they end.

**Errors.** `Result<T>` is `std::expected<T, ffrwd::Error>`, and `Status`
is `Result<void>`. `ffrwd::fail("...")` is an error to return,
`FFRWD_TRY(expr)` returns the error of `expr` if it has one, and
`FFRWD_LET(name, expr)` declares `name` as the value of `expr` or returns
its error. An error ends the run with its message.

**Ticks.** `tick.ordinal()` is the tick's number in the run, counted over
every instance, so a node that numbers things by frame stays pure.
`Input::video("v").timing()` reads a picture for its times and size alone:
the host carries no pixels for it, and `tick.fetch` on it, or passing its
frame on, ends the run with the port named. `feed.start.known` is the tick
a start was fixed on, and `tick.ended_feeds(id)` every feed that ended
since the instance's previous call.

## An example: `spot`

Rows alone from a picture: one a frame while a grey mark is in view, every
row of one sighting carrying the time it began as `start_t`, and a new
sighting every `every` frames. The full module, with a stand-in for a real
detector:

```cpp
#include <algorithm>
#include <array>
#include <optional>

#include "ffrwd/node.hpp"

struct Params {
    std::uint64_t every = 30;
    FFRWD_FIELDS(every)
};

struct Spot {
    ffrwd::Span span;
    std::uint32_t x = 0, y = 0, w = 0, h = 0;
    FFRWD_FIELDS(span, x, y, w, h)
};

/// The box around every mid-grey pixel of an rgba picture.
std::optional<std::array<std::uint32_t, 4>> grey(const ffrwd::Bytes& pixels, std::size_t width) {
    std::optional<std::array<std::size_t, 4>> found;
    for (std::size_t at = 0; at * 4 + 3 < pixels.size(); ++at) {
        const std::uint8_t* pixel = pixels.data() + at * 4;
        auto [lo, hi] = std::minmax({pixel[0], pixel[1], pixel[2]});
        if (hi - lo >= 24 || lo <= 96 || hi >= 160) continue;
        std::size_t x = at % width, y = at / width;
        if (!found) found = std::array{x, y, x, y};
        auto& [x0, y0, x1, y1] = *found;
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
    }
    if (!found) return std::nullopt;
    auto [x0, y0, x1, y1] = *found;
    return std::array{std::uint32_t(x0), std::uint32_t(y0), std::uint32_t(x1 - x0 + 1),
                      std::uint32_t(y1 - y0 + 1)};
}

struct SpotNode : ffrwd::Node<SpotNode, Params> {
    static constexpr std::string_view name = "spot";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"every":{"type":"integer","minimum":1,"default":30}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::size_t width = 0;
    ffrwd::Spans<> spans;

    static ffrwd::Result<ffrwd::Shape> shape(const Params&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .output(ffrwd::Output::rows("spots").schema<Spot>());
    }

    static ffrwd::Result<SpotNode> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        SpotNode node;
        node.v = v.id;
        node.width = video->width;
        node.spans = ffrwd::Spans<>().longest(params.every);
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        spans.tick(tick.time_base().seconds(frame->pts));
        auto box = grey(tick.fetch(v, frame->index), width);
        if (!box) return {};
        auto [x, y, w, h] = *box;
        return out.row("spots", frame->pts, Spot{spans.see(), x, y, w, h});
    }
};

FFRWD_EXPORT(SpotNode);
```

Its declaration and a call, from the cookbook:

```pgsql
CREATE FUNCTION spot(v video_stream, every number DEFAULT 30)
RETURNS STRUCT(start_t number, id number, x number, y number, w number, h number)[]
  AS 'build/spot.wasm', 'spot'
  LANGUAGE wasm;

SELECT ffrwd.merge_spans(spot(f.video[1]), max_span => 10) FROM input('testsrc.mp4') f
```

`examples/` holds two modules built with this library: `dim`, which dims
the picture inside the boxes it is handed, and `spot`, which numbers its
sightings by `tick.ordinal()` so it is pure and a run split across workers
agrees on the ids. Cookbook recipes 146 and 152 run with them.

## Building

What it takes:

- [wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases) 34 or newer.
  Its clang targets `wasm32-wasip2` and links a component itself, so there
  is no adapter step.
- [wit-bindgen](https://github.com/bytecodealliance/wit-bindgen/releases)
  0.57.1, which writes the C bindings of `wit/av.wit`.
- [wasm-tools](https://github.com/bytecodealliance/wasm-tools/releases)
  1.258.0, to validate what was built. Optional.
- A host C++23 compiler for the tests, clang 18 or newer.

```
WASI_SDK=C:/tools/wasi-sdk-34.0-x86_64-windows sh build.sh
```

builds `build/dim.wasm` and `build/spot.wasm`, then the tests, and runs
them. `WIT_BINDGEN`, `WASM_TOOLS` and `CXX` name the tools when they are not
on PATH. The library carries the world it speaks, `wit/av.wit`, which is
`ffrwd:av@0.19.1` byte for byte; with `FFRWD_WIT_DIR` set, `sh build.sh
test` checks that copy against the `av.wit` it names.

A module of your own is three steps after `sh build.sh modules` has built
the bindings and `build/wasm/libffrwd-node.a`:

```
$WASI_SDK/bin/clang++ --target=wasm32-wasip2 -std=c++23 -O2 -fno-exceptions -fno-rtti \
    -Iinclude -Ibuild/gen -c spot.cpp -o spot.o
$WASI_SDK/bin/clang++ --target=wasm32-wasip2 -mexec-model=reactor \
    -Wl,--gc-sections -Wl,--strip-all -o spot.wasm \
    spot.o build/wasm/libffrwd-node.a build/wasm/node_module.o build/gen/node_module_component_type.o
ffrwd-wasm --shape spot.wasm --bound v
```

The library is built for size and a node's own code for speed, where its
pixels are. Each example comes to about 230 KB.

## Testing on the host

Everything but the bindings builds on the host, and `FFRWD_EXPORT` checks
the node there without exporting it, so a module's tests build with any
C++23 compiler from `src/*.cpp` less `src/glue.cpp`. `ffrwd::mock::Harness`
opens a node the way the host does and hands it ticks built by hand:

```cpp
#include "ffrwd/mock.hpp"

auto v = ffrwd::BoundStream::video("v", 0, 64, 48, "rgba", ffrwd::Rational(1, 15));
auto spot = ffrwd::mock::Harness<SpotNode>::open(R"({"every":3})", {v});
auto emitted = spot->process(spot->tick(0).frame(0, 0, std::move(pixels)));
assert(emitted->messages("spots").size() == 1);
```

A bound stream's hint is what the harness shapes with: an audio stream's
is its sample rate, and `BoundStream::video(...).rate(ffrwd::Rational(25, 1))`
gives a picture's. A harness numbers each tick one past the last it
processed; `.ordinal(n)` numbers it as a worker handed every other tick
would see it. `.feed(id, feed)` sets a hold input's feed, `.ended(id, feed)`
adds one that ended, `.earlier(id, pts, rows)` adds a state input's rows of
a tick the instance did not process, and `.last_call()` makes it the last.
`tests/` holds the library's own, with the examples', in a small runner of
its own: `sh build.sh test`.

## What it does not do

- Pixels. A node that draws, crops or converts colour brings its own.
- Other worlds. `values`, `encoder` and `decoder`, and imports such as
  `wasi:nn` or `wasi:webgpu`, take bindings of the module's own.
- All of JSON Schema. The params are checked for `type`, `enum`, `const`,
  `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`,
  `minLength`, `maxLength`, `minItems`, `maxItems`, `items`, `properties`,
  `required` and `additionalProperties`; the compiler checks a call against
  the whole schema before the node sees it.
- Exceptions. Modules build with `-fno-exceptions`: a node reports what
  went wrong through `Result`, and whatever the standard library would
  have thrown ends the run where it happens.
- Rows on an input that is not `state()` are not folded, and a node reading
  them keeps them itself.
- Waiting. A self-clocked node's `process` may block until it has something
  to emit; the library neither helps nor hinders.

## License

MIT.
