// `dim`: every picture darkened by `amount`, 0 leaving it as it was and 1
// making it black. The same node as `rust/examples/dim.rs` and
// `go/examples/dim`.

import { defineNode, Input, Output, Shape } from '@ffrwd/node';

/** Every colour byte of an rgba picture scaled by `1 - amount`, alpha kept. */
export function darken(pixels, amount) {
  const keep = Math.round((1 - Math.min(1, Math.max(0, amount))) * 256);
  for (let at = 0; at < pixels.length; at += 4) {
    pixels[at] = (pixels[at] * keep) >> 8;
    pixels[at + 1] = (pixels[at + 1] * keep) >> 8;
    pixels[at + 2] = (pixels[at + 2] * keep) >> 8;
  }
}

export const node = defineNode({
  name: 'dim',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"amount":{"type":"number","minimum":0,"maximum":1,"default":0.5}},' +
    '"additionalProperties":false}',

  shape() {
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .output(Output.like('v'))
      .pure()
      .oneToOne();
  },

  init({ amount }, init) {
    const v = init.stream('v').id;
    return {
      setParams(params) {
        amount = params.amount;
      },
      process(tick, out) {
        const frame = tick.frame(v);
        if (frame === undefined) return;
        if (amount === 0) {
          out.pass('v', v, frame);
          return;
        }
        const pixels = tick.fetch(v, frame.index);
        darken(pixels, amount);
        out.frame('v', frame.pts, frame.duration, pixels);
      },
    };
  },
});
