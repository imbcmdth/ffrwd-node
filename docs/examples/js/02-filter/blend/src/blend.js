import { defineNode, Input, Output, Shape } from '@ffrwd/node';

export const node = defineNode({
  name: 'blend',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"mix":{"type":"number","minimum":0,"maximum":1,"default":0.5}},' +
    '"additionalProperties":false}',

  shape() {
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .input(Input.video('over').lockstep().like('v').pixelFormats(['rgba']))
      .output(Output.like('v'))
      .pure()
      .oneToOne();
  },

  init({ mix }, init) {
    const v = init.stream('v').id;
    const over = init.stream('over').id;
    return {
      setParams(params) {
        mix = params.mix;
      },
      process(tick, out) {
        const frame = tick.frame(v);
        if (frame === undefined) return;
        const top = tick.frame(over);
        if (top === undefined) return out.pass('v', v, frame);
        if (mix === 0) return out.pass('v', v, frame);
        if (mix === 1) return out.same('v', frame.pts, frame.duration, over, top.index);
        const pixels = tick.fetch(v, frame.index);
        const above = tick.fetch(over, top.index);
        for (let at = 0; at < pixels.length; at += 1) {
          pixels[at] = Math.round(pixels[at] + (above[at] - pixels[at]) * mix);
        }
        out.frame('v', frame.pts, frame.duration, pixels);
      },
    };
  },
});
