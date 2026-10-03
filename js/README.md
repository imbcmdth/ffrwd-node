# @ffrwd/node

The node world, `ffrwd:av@0.19.1`, for JavaScript. Define a node as an
object, export what `defineNode` makes of it as `node`, and build it into a
wasip2 component with ComponentizeJS. The package does what the Rust crate
in `../rust` does for a Rust module, with the same names where JavaScript
allows them: the call sequence, params read against their schema, shapes
from builders, time in any time base, rows of a state input folded,
emissions checked as they are made, and a thrown error as the run's message.

Requires ffrwd 0.29, whose `ffrwd/wasm` is 0.19.1.

## A node

```js
import { defineNode, Input, Output, Shape } from '@ffrwd/node';

export const node = defineNode({
  name: 'dim',
  version: '0.1.0',
  paramsSchema: '{"type":"object","properties":{"amount":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}',

  shape: (params, bound) =>
    new Shape().input(Input.video('v').clock().pixelFormats(['rgba'])).output(Output.like('v')).pure().oneToOne(),

  init({ amount }, init) {
    const v = init.stream('v').id;
    return {
      setParams(params) {
        amount = params.amount;
      },
      process(tick, out) {
        const frame = tick.frame(v);
        if (frame === undefined) return;
        if (amount === 0) return out.pass('v', v, frame);
        const pixels = tick.fetch(v, frame.index);
        darken(pixels, amount);
        out.frame('v', frame.pts, frame.duration, pixels);
      },
    };
  },
});
```

`name`, `version`, `paramsSchema`, `rowsSchema` and `rowsLanguage` are
`describe`. `shape` answers at compile time, and again at `init`, which the
package calls with the shape resolved. `init` answers the instance: an
object with `process(tick, out)`, and `setParams(params)` and
`fold(row)` when it takes them. Without `setParams` a change of params is
refused; without `fold` a node declaring a state input fails on its first
row. `process` runs once a tick, the last call the one with `tick.last()`
set.

**Params.** The call's JSON is read against `paramsSchema` before any of
these sees it: an empty string is `{}`, a param set to null is not set, the
schema's defaults fill in what the call left out. A param outside the
schema, or one that breaks its `type`, `enum`, bounds or lengths, is
refused with the param named. Params equal to the ones in force never reach
`setParams`.

**Shapes.** `Shape`, `Input` and `Output` chain as the Rust builders do, in
camelCase: `Input.video('v').clock().window(15, 1)`,
`Input.rows('boxes').interval().latency(2).state().schema(BOX)`,
`Input.video('feed').optional().hold().lead(0.5).portParam('port')`,
`Output.video('mask').pixelFormat('gray')`, `Output.like('v')`. A builder
keeps what it was told in `spec`. Before the host sees a shape the package
settles its clock, leaves out every output that follows an unbound input,
and refuses what the host would refuse, naming the port.

**What the call binds.** `bound.has('a')`, `bound.count('inputs')` and
`bound.rateOf('v')` read what the compiler knows; a rate is a `Rational`,
so `rate.count(2)` is the samples in two seconds and `rate.duration(1)` the
length of one frame.

**Time.** `Rational` has `seconds(pts)`, `pts(seconds)`, `rescale(pts, to)`
exactly as ffmpeg rounds, `count`, `duration`, `inverse` and
`Rational.approximate(29.97, 1001)`. Every pts the package hands or takes is
a Number; it is a BigInt only on the wire.

**State.** Before each `process`, every row on an input declared `state()`
reaches the instance's `fold` as a `StateRow`: the rows of ticks this
instance did not process first, oldest first, then the tick's own.

**Emitting.** `out` checks every emission as it is made: the port is one
the shape declares, of the payload's kind, and its pts never go back.
`out.pass`, `out.frame`, `out.same`, `out.message`, `out.row`, `out.rows`,
`out.cue`, `out.progress`, `out.packet`, `out.report` and `out.finish`, and
`out.pts(port, seconds)` for a time on a port.

**Rows.** `tick.rows(id)` parses a data stream's messages, or the rows
riding a frame stream. A row's schema is written from a description of its
fields by type: `schemaOf({ ...SPAN, x: 'integer', label: 'string' })`.
`Spans` names per-tick rows by the span they belong to; a `Span` spread into
a row writes its `start_t` and its `id`. `Cue` and `Cues` are a query's cues.

**Errors.** Throw an `Error` or a string; the run ends with its message.

## Crops and resizes

`@ffrwd/node/frame` gives a JavaScript module what the Rust crate
[ffrwd-frame](https://github.com/imbcmdth/ffrwd-frame) gives a Rust one,
under its names: `new Rgba(pixels, width, height)` over the bytes a fetch
hands over, a `Rect` of them (`Rect.whole`, or `Rect.padded` for a
detector's box widened and clamped to the frame), and `planes`, `tensor`
and `tensors`, which crop, resize with Pillow's bilinear and normalize into
the `Float32Array` or the fp32 bytes a vision model reads. The pixels are
the crate's to the byte, and the tests hold them to digests of its
answers. A normalization of mean 0 and standard deviation 1/255 hands back
plain eight-bit values. Pillow's bicubic and the yuv420p conversion are the
crate's alone.

```js
import { Filter, planes, Rect, Rgba } from '@ffrwd/node/frame';
```

## Building

```
npm install
npm run check
```

`check` runs the tests, builds `examples/dim.js` into `build/dim.wasm` and
asks the sidecar to describe it and print its shape (`FFRWD_WASM` names
`ffrwd-wasm` when it is not on PATH). A module of its own builds the same
way:

```js
import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/dim.js', out: 'build/dim.wasm' });
```

`buildNode` bundles the entry with esbuild into the one file ComponentizeJS
takes and componentizes it against `node-module`, with StarlingMonkey's
`http`, `fetch-event`, `random` and `clocks` turned off. That list is
load-bearing: with `http` on the component imports `wasi:http` and the
sidecar reports it as needing the capability. `aot: true` runs weval over
the JavaScript, which costs startup and size and takes time off
every frame; it needs `@bytecodealliance/weval` installed.

The world comes from the repo's `wit/av.wit`; a packed copy of the package
carries its own, staged by `prepack`.

## Testing on the host

```js
import { BoundStream, Rational } from '@ffrwd/node';
import { Harness } from '@ffrwd/node/mock';

const v = BoundStream.video('v', 0, 64, 48, 'rgba', new Rational(1, 15));
const dim = new Harness(node, '{"amount":0.75}', [v]);
const emitted = dim.process(dim.tick(0).frame(0, 0, pixels));
```

`Harness` opens a node the way the host does and hands it ticks built by
hand: `.ordinal(n)`, `.message(id, pts, text)`, `.row(id, pts, row)`,
`.feed(id, feed)`, `.ended(id, feed)`, `.earlier(id, pts, rows)`, `.last()`.

## Where it parts from the Rust crate

- A node is an object and its instance whatever `init` answers; there is no
  trait and no `export!`.
- Params arrive as the checked JSON object, defaults filled in, not as a
  typed struct.
- A row schema is written from a description, since a JavaScript value does
  not say whether `0` is an integer or a number.
- A whole number written as a row is `2`, where serde writes `2.0`.
- The component carries a JavaScript engine: about 13 MiB, a second to
  start, and roughly two orders slower per pixel than Rust.
  [ffrwd/jsqr](https://github.com/imbcmdth/ffrwd-package-jsqr) measures it.

## License

MIT.
