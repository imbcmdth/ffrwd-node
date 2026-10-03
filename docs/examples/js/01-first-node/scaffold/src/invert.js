import { defineNode, Input, Output, Shape } from '@ffrwd/node';

export const node = defineNode({
  name: 'passthrough',
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
        // Your work goes here: `tick.fetch` reads the picture, `out.frame` sends a new one.
        out.pass('v', v, frame);
      },
    };
  },
});
