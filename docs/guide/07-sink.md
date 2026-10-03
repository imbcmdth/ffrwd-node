# 7. A sink

A sink is a node that takes streams and sends them somewhere other than a
file the query writes, such as a relay, an HTTP endpoint or a log. A sink
has no output streams. The result of a sink is its effect on the place it
sends to, plus rows that report how the work went. This chapter builds
`tally`, which counts the encoded packets of every stream it is handed, and
reports the counts when the streams end.

## Packets by arrival

A packets input carries a stream as its encoder wrote it. For each packet,
the input carries the packet's bytes, its pts and dts, its duration where
the duration is known, and whether decoding can start at that packet.
Packets come in decode order, and each packet comes once. A sink takes
packets as they arrive, without pairing them to the ticks of a clock. This
way of pairing an input is called pairing by arrival.

A port declares how many streams it takes. A port declared `many` takes as
many streams as the query hands it. Each of those streams has its own codec,
its own time base and its own relation row, which is the row of the source's
table that the stream came from. So one port can read a whole ladder of
renditions. A port may also name the codecs it accepts, and how much of each
stream it needs: every packet, only the keyframes, or only the first packet.
The host, which is the program that runs the node, may hand over more than a
port asks for, but never less.

## A clock that only gives turns

A node's clock decides when the host calls the node, and each call is one
tick. A sink has no stream to keep time by, so a sink ticks at a rate. When
all of a node's inputs pair by arrival, the rate is not a timeline. The rate
only sets a minimum: the node gets a turn at least that often. The host
ticks when something has arrived that no tick has taken yet, or when one
period has passed since the last tick. So the node never runs ahead of the
wall clock with nothing to hand it. A network session uses these turns to
keep its connection alive while no packets come.

## The last call

Once every input has ended, one more tick takes whatever arrived that no
tick had taken yet, and the last call follows at once. The last call happens
exactly once, and the last call may carry nothing. There is no separate
flush call. Whatever the node still holds has to leave on the last call.

## tally

`tally` writes its counts as run rows, which go to the run itself instead of
to an output port. So the shape of `tally`, which lists its ports and its
clock, declares no outputs. The run rows have a schema, which the node
declares beside the schema of its params.

**Rust**

```rust
use ffrwd_node::{Bound, Format, Init, Input, NoParams, Node, Out, Rational, Result, Shape, Tick};
use serde::Serialize;

/// One row per stream, written on the last call.
#[derive(Serialize)]
struct Count {
    port: String,
    codec: String,
    packets: u64,
    keyframes: u64,
    bytes: u64,
    seconds: f64,
}

struct Stream {
    id: u32,
    time_base: Rational,
    count: Count,
    first: Option<i64>,
    last: Option<i64>,
}

struct Tally {
    streams: Vec<Stream>,
}

impl Node for Tally {
    const NAME: &'static str = "tally";
    const VERSION: &'static str = "0.1.0";
    const ROWS_SCHEMA: &'static str = r#"{"type":"object","properties":{"port":{"type":"string"},"codec":{"type":"string"},"packets":{"type":"integer"},"keyframes":{"type":"integer"},"bytes":{"type":"integer"},"seconds":{"type":"number"}},"required":["port","codec","packets","keyframes","bytes","seconds"]}"#;
    type Params = NoParams;

    fn shape(_: &NoParams, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(Input::packets("video").optional().many().arrival())
            .input(Input::packets("audio").optional().many().arrival())
            .rate(Rational::new(10, 1)))
    }

    fn init(_: NoParams, init: &Init) -> Result<Tally> {
        let mut streams = Vec::new();
        for stream in init.all() {
            let Some(Format::Packets(coded)) = &stream.format else {
                return Err(format!("`{}` carries no packets", stream.port).into());
            };
            streams.push(Stream {
                id: stream.id,
                time_base: coded.time_base,
                count: Count {
                    port: stream.port.clone(),
                    codec: coded.codec.clone(),
                    packets: 0,
                    keyframes: 0,
                    bytes: 0,
                    seconds: 0.0,
                },
                first: None,
                last: None,
            });
        }
        Ok(Tally { streams })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        for stream in &mut self.streams {
            for packet in tick.packets(stream.id) {
                let count = &mut stream.count;
                count.packets += 1;
                count.keyframes += u64::from(packet.keyframe);
                count.bytes += packet.data.len() as u64;
                let end = packet.pts + packet.duration.unwrap_or(0);
                stream.first = Some(
                    stream
                        .first
                        .map_or(packet.pts, |first| first.min(packet.pts)),
                );
                stream.last = Some(stream.last.map_or(end, |last| last.max(end)));
            }
        }
        if tick.last() {
            for stream in &mut self.streams {
                if let (Some(first), Some(last)) = (stream.first, stream.last) {
                    stream.count.seconds = stream.time_base.seconds(last - first);
                }
                out.report(&stream.count)?;
            }
        }
        Ok(())
    }
}

ffrwd_node::export!(Tally);
```

**C++**

```cpp
#include <algorithm>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ffrwd/node.hpp"

/// One row per stream, written on the last call.
struct Count {
    std::string port;
    std::string codec;
    std::uint64_t packets = 0;
    std::uint64_t keyframes = 0;
    std::uint64_t bytes = 0;
    double seconds = 0.0;
    FFRWD_FIELDS(port, codec, packets, keyframes, bytes, seconds)
};

struct Stream {
    std::uint32_t id = 0;
    ffrwd::Rational time_base;
    Count count;
    std::optional<std::int64_t> first;
    std::optional<std::int64_t> last;
};

struct Tally : ffrwd::Node<Tally> {
    static constexpr std::string_view name = "tally";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view rows_schema =
        R"({"type":"object","properties":{"port":{"type":"string"},"codec":{"type":"string"},"packets":{"type":"integer"},"keyframes":{"type":"integer"},"bytes":{"type":"integer"},"seconds":{"type":"number"}},"required":["port","codec","packets","keyframes","bytes","seconds"]})";

    std::vector<Stream> streams;

    static ffrwd::Result<ffrwd::Shape> shape(const ffrwd::NoParams&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::packets("video").optional().many().arrival())
            .input(ffrwd::Input::packets("audio").optional().many().arrival())
            .rate(ffrwd::Rational(10, 1));
    }

    static ffrwd::Result<Tally> init(ffrwd::NoParams, const ffrwd::Init& init) {
        Tally node;
        for (const ffrwd::BoundStream& stream : init.all()) {
            const auto* coded = stream.format ? std::get_if<ffrwd::CodedStream>(&*stream.format) : nullptr;
            if (!coded) return ffrwd::fail("`" + stream.port + "` carries no packets");
            node.streams.push_back(
                Stream{stream.id, coded->time_base, Count{stream.port, coded->codec}, {}, {}});
        }
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        for (Stream& stream : streams) {
            for (const ffrwd::Packet& packet : tick.packets(stream.id)) {
                Count& count = stream.count;
                count.packets += 1;
                count.keyframes += packet.keyframe ? 1 : 0;
                count.bytes += packet.data.size();
                std::int64_t end = packet.pts + packet.duration.value_or(0);
                stream.first = std::min(stream.first.value_or(packet.pts), packet.pts);
                stream.last = std::max(stream.last.value_or(end), end);
            }
        }
        if (tick.last()) {
            for (Stream& stream : streams) {
                if (stream.first && stream.last)
                    stream.count.seconds = stream.time_base.seconds(*stream.last - *stream.first);
                out.report(stream.count);
            }
        }
        return {};
    }
};

FFRWD_EXPORT(Tally);
```

**JavaScript**

```js
import { defineNode, Input, Rational, Shape } from '@ffrwd/node';

export const node = defineNode({
  name: 'tally',
  version: '0.1.0',
  rowsSchema:
    '{"type":"object","properties":{"port":{"type":"string"},"codec":{"type":"string"},' +
    '"packets":{"type":"integer"},"keyframes":{"type":"integer"},"bytes":{"type":"integer"},' +
    '"seconds":{"type":"number"}},"required":["port","codec","packets","keyframes","bytes","seconds"]}',

  shape() {
    return new Shape()
      .input(Input.packets('video').optional().many().arrival())
      .input(Input.packets('audio').optional().many().arrival())
      .rate(new Rational(10, 1));
  },

  init(_, init) {
    const streams = init.all().map((stream) => {
      if (stream.format?.tag !== 'packets') throw new Error(`\`${stream.port}\` carries no packets`);
      const coded = stream.format.val;
      return {
        id: stream.id,
        timeBase: coded.timeBase,
        // One row per stream, written on the last call.
        count: { port: stream.port, codec: coded.codec, packets: 0, keyframes: 0, bytes: 0, seconds: 0 },
        first: undefined,
        last: undefined,
      };
    });
    return {
      process(tick, out) {
        for (const stream of streams) {
          for (const packet of tick.packets(stream.id)) {
            const count = stream.count;
            count.packets += 1;
            count.keyframes += packet.keyframe ? 1 : 0;
            count.bytes += packet.data.length;
            const end = packet.pts + (packet.duration ?? 0);
            stream.first = Math.min(stream.first ?? packet.pts, packet.pts);
            stream.last = Math.max(stream.last ?? end, end);
          }
        }
        if (tick.last()) {
          for (const stream of streams) {
            if (stream.first !== undefined && stream.last !== undefined) {
              stream.count.seconds = stream.timeBase.seconds(stream.last - stream.first);
            }
            out.report(stream.count);
          }
        }
      },
    };
  },
});
```

**Go**

```go
package main

import (
	"fmt"

	node "github.com/imbcmdth/ffrwd-node/go"
)

// Count is one row per stream, written on the last call.
type Count struct {
	Port      string  `json:"port"`
	Codec     string  `json:"codec"`
	Packets   uint64  `json:"packets"`
	Keyframes uint64  `json:"keyframes"`
	Bytes     uint64  `json:"bytes"`
	Seconds   float64 `json:"seconds"`
}

type Stream struct {
	id       uint32
	timeBase node.Rational
	count    Count
	first    *int64
	last     *int64
}

type Tally struct {
	streams []*Stream
}

var Definition = node.Definition[struct{}]{
	Name:       "tally",
	Version:    "0.1.0",
	RowsSchema: `{"type":"object","properties":{"port":{"type":"string"},"codec":{"type":"string"},"packets":{"type":"integer"},"keyframes":{"type":"integer"},"bytes":{"type":"integer"},"seconds":{"type":"number"}},"required":["port","codec","packets","keyframes","bytes","seconds"]}`,
	Shape: func(struct{}, *node.Bound) (node.Shape, error) {
		return node.NewShape().
			Input(node.PacketsInput("video").Optional().Many().Arrival()).
			Input(node.PacketsInput("audio").Optional().Many().Arrival()).
			Rate(node.R(10, 1)), nil
	},
	Init: func(_ struct{}, init *node.Init) (node.Instance, error) {
		var streams []*Stream
		for _, stream := range init.All() {
			if stream.Format == nil || stream.Format.Packets == nil {
				return nil, fmt.Errorf("`%s` carries no packets", stream.Port)
			}
			coded := stream.Format.Packets
			streams = append(streams, &Stream{
				id:       stream.ID,
				timeBase: coded.TimeBase,
				count:    Count{Port: stream.Port, Codec: coded.Codec},
			})
		}
		return &Tally{streams: streams}, nil
	},
}

func (t *Tally) Process(tick *node.Tick, out *node.Out) error {
	for _, stream := range t.streams {
		for _, packet := range tick.Packets(stream.id) {
			count := &stream.count
			count.Packets++
			if packet.Keyframe {
				count.Keyframes++
			}
			count.Bytes += uint64(len(packet.Data))
			end := packet.Pts
			if packet.Duration != nil {
				end += *packet.Duration
			}
			first, last := packet.Pts, end
			if stream.first != nil {
				first = min(*stream.first, packet.Pts)
			}
			if stream.last != nil {
				last = max(*stream.last, end)
			}
			stream.first, stream.last = &first, &last
		}
	}
	if tick.Last() {
		for _, stream := range t.streams {
			if stream.first != nil && stream.last != nil {
				stream.count.Seconds = stream.timeBase.Seconds(*stream.last - *stream.first)
			}
			if err := out.Report(stream.count); err != nil {
				return err
			}
		}
	}
	return nil
}

func init() { node.Export(Definition) }

func main() {}
```

The shape has two optional ports of packets paired by arrival, each taking
any number of streams, and a clock that ticks at a rate:

**Rust**

```
$ ffrwd-wasm --shape target/wasm32-wasip2/release/tally.wasm --bound video,audio
```

**C++**

```
$ ffrwd-wasm --shape build/tally.wasm --bound video,audio
```

**JavaScript**

```
$ ffrwd-wasm --shape build/tally.wasm --bound video,audio
```

**Go**

```
$ ffrwd-wasm --shape build/tally.wasm --bound video,audio
```

```json
{
  "accepts": {
    "channel_counts": [],
    "codecs": [],
    "like": null,
    "pixel_formats": [],
    "sample_formats": [],
    "sample_rates": [],
    "wants": "all"
  },
  "kind": "packets",
  "many": true,
  "name": "video",
  "pairing": {"kind": "arrival"},
  "required": false,
  "rows": "ignore",
  "schema": null,
  "stride": 1,
  "window": 1
}
```

```json
{"den": 1, "kind": "rate", "num": 10}
```

`--describe` includes the schema of the run rows:

**Rust**

```
$ ffrwd-wasm --describe target/wasm32-wasip2/release/tally.wasm
```

**C++**

```
$ ffrwd-wasm --describe build/tally.wasm
```

**JavaScript**

```
$ ffrwd-wasm --describe build/tally.wasm
```

**Go**

```
$ ffrwd-wasm --describe build/tally.wasm
```

```json
{
  "properties": {
    "bytes": {"type": "integer"},
    "codec": {"type": "string"},
    "keyframes": {"type": "integer"},
    "packets": {"type": "integer"},
    "port": {"type": "string"},
    "seconds": {"type": "number"}
  },
  "required": ["port", "codec", "packets", "keyframes", "bytes", "seconds"],
  "type": "object"
}
```

## Calling it

The declaration gives `tally` the return type `sink`, so `tally` is what a
`COPY` writes to. The `SELECT` names the streams, and each stream binds the
port of its own kind:

```sql
CREATE FUNCTION tally() RETURNS sink
  AS 'tally.wasm', 'tally' LANGUAGE wasm;

COPY (
  SELECT f.video[1], f.audio[1]
  FROM input('av.mp4') f
) TO tally()
```

```
$ ffrwd compile -f tally.sql
# named pipes: sidecar0 reads ffmpeg0, ffmpeg0; ffmpeg0 feeds sidecar0, sidecar0
1. ffmpeg: ffmpeg -i av.mp4 -map 0:v:0 -c:0 copy -f nut \
  '<named pipe ffmpeg0-sidecar0 src:f:v:0 write>' -map 0:a:0 -c:0 copy -f nut \
  '<named pipe ffmpeg0-sidecar0 src:f:a:0 write>'
2. sidecar: ffrwd-wasm -f nut -i '<named pipe ffmpeg0-sidecar0 src:f:v:0 read>' -pad \
  '{"row": 0, "rendition": {"name": "240p"}}' -f nut -i \
  '<named pipe ffmpeg0-sidecar0 src:f:a:0 read>' -pad \
  '{"row": 0, "rendition": {"name": "240p"}}' -m tally=tally.wasm -filter_complex \
  '[video=0:v][audio=1:a]tally[@rows=out0]' -bound \
  'tally=[{"input":"video","streams":[{"rate":{"num":15,"den":1}}]},{"input":"audio","streams":[{"rate":{"num":44100,"den":1}}]}]' \
  -map '[out0]' -f ndjson pipe:1
# this listing is not a shell command -- run the plan with `ffrwd run`
```

A port that reads packets is handed the input file's own stream, copied as
it was encoded, with no decoding. In the plan, `[@rows=out0]` gives a label
to the node's run rows, and the run prints the rows that carry that label.
Each stream also carries the relation row and the rendition it came from. A
sink that is handed a ladder of renditions names its tracks by the relation
row of each stream.

`WITH` options on the `COPY` set up an encoder, which the compiler puts in
front of the sink, just as the options would for a file:

```sql
CREATE FUNCTION tally() RETURNS sink
  AS 'tally.wasm', 'tally' LANGUAGE wasm;

COPY (
  SELECT f.video[1], f.audio[1]
  FROM input('av.mp4') f
) TO tally() WITH (video_codec 'libx264', audio_codec 'aac')
```

```
$ ffrwd compile -f encoded.sql
# named pipes: sidecar0 reads ffmpeg0, ffmpeg0; ffmpeg0 feeds sidecar0, sidecar0
1. ffmpeg: ffmpeg -i av.mp4 -map 0:v:0 -c:0 libx264 -pix_fmt:0 yuv420p -f nut \
  '<named pipe ffmpeg0-sidecar0 src:f:v:0 write>' -map 0:a:0 -c:0 aac -f nut \
  '<named pipe ffmpeg0-sidecar0 src:f:a:0 write>'
2. sidecar: ffrwd-wasm -f nut -i '<named pipe ffmpeg0-sidecar0 src:f:v:0 read>' -pad \
  '{"row": 0, "rendition": {"name": "240p"}}' -f nut -i \
  '<named pipe ffmpeg0-sidecar0 src:f:a:0 read>' -pad \
  '{"row": 0, "rendition": {"name": "240p"}}' -m tally=tally.wasm -filter_complex \
  '[video=0:v][audio=1:a]tally[@rows=out0]' -bound \
  'tally=[{"input":"video","streams":[{"rate":{"num":15,"den":1}}]},{"input":"audio","streams":[{"rate":{"num":44100,"den":1}}]}]' \
  -map '[out0]' -f ndjson pipe:1
# this listing is not a shell command -- run the plan with `ffrwd run`
```
