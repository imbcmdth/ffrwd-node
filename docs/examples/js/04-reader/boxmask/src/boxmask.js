import { defineNode, Input, Output, Shape } from '@ffrwd/node';

/** The fields `boxmask` reads. Any row carrying them will do. */
const BOX = { x: 'number', y: 'number', w: 'number', h: 'number' };

export const node = defineNode({
  name: 'boxmask',
  version: '0.1.0',

  shape() {
    return new Shape()
      .input(Input.video('v').clock().timing())
      .input(Input.rows('boxes').schema(BOX))
      .output(Output.like('v').pixelFormat('gray'))
      .pure()
      .oneToOne();
  },

  init(_, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    const { width, height } = video;
    const boxes = init.stream('boxes').id;
    return {
      process(tick, out) {
        const frame = tick.frame(v.id);
        if (frame === undefined) return;
        const mask = new Uint8Array(width * height);
        for (const found of tick.rows(boxes)) {
          const x0 = Math.min(Math.trunc(Math.max(found.x, 0)), width);
          const y0 = Math.min(Math.trunc(Math.max(found.y, 0)), height);
          const x1 = Math.min(Math.trunc(Math.max(found.x + found.w, 0)), width);
          const y1 = Math.min(Math.trunc(Math.max(found.y + found.h, 0)), height);
          for (let y = y0; y < y1; y += 1) mask.fill(255, y * width + x0, y * width + Math.max(x1, x0));
        }
        out.frame('v', frame.pts, frame.duration, mask);
      },
    };
  },
});
