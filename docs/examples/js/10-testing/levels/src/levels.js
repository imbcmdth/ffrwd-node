import { defineNode, Input, Output, Shape } from '@ffrwd/node';

/** `value` with `black` moved to 0 and `white` to 255. */
function stretch(value, { black, white }) {
  return Math.min(Math.max(Math.round(((value - black) * 255) / (white - black)), 0), 255);
}

export const node = defineNode({
  name: 'levels',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"black":{"type":"integer","minimum":0,"maximum":254,"default":16},' +
    '"white":{"type":"integer","minimum":1,"maximum":255,"default":235}},"additionalProperties":false}',

  shape({ black, white }) {
    if (black >= white) throw new Error('levels needs `black` under `white`');
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .output(Output.like('v'))
      .pure()
      .oneToOne();
  },

  init(params, init) {
    const v = init.stream('v').id;
    return {
      setParams(changed) {
        params = changed;
      },
      process(tick, out) {
        const frame = tick.frame(v);
        if (frame === undefined) return;
        if (params.black === 0 && params.white === 255) return out.pass('v', v, frame);
        const pixels = tick.fetch(v, frame.index);
        for (let at = 0; at < pixels.length; at += 4) {
          for (let channel = at; channel < at + 3; channel += 1) pixels[channel] = stretch(pixels[channel], params);
        }
        out.frame('v', frame.pts, frame.duration, pixels);
      },
    };
  },
});
