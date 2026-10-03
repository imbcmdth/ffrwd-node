import { defineNode, Input, Output, Shape } from '@ffrwd/node';

export const node = defineNode({
  name: 'invert',
  version: '0.1.0',

  shape() {
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .output(Output.like('v'))
      .pure()
      .oneToOne();
  },

  init(_, init) {
    const v = init.stream('v').id;
    return {
      process(tick, out) {
        const frame = tick.frame(v);
        if (frame === undefined) return;
        const pixels = tick.fetch(v, frame.index);
        for (let at = 0; at < pixels.length; at += 4) {
          pixels[at] = 255 - pixels[at];
          pixels[at + 1] = 255 - pixels[at + 1];
          pixels[at + 2] = 255 - pixels[at + 2];
        }
        out.frame('v', frame.pts, frame.duration, pixels);
      },
    };
  },
});
