import { defineNode, Input, Output, Shape } from '@ffrwd/node';

const GLOW = { start_t: 'number', id: 'integer', x: 'integer', y: 'integer', w: 'integer', h: 'integer' };

/** The box around every pixel of an rgba picture at least `threshold`
 * bright. */
function bright(pixels, width, threshold) {
  let found;
  for (let at = 0; at < Math.floor(pixels.length / 4); at += 1) {
    const luma = (54 * pixels[4 * at] + 183 * pixels[4 * at + 1] + 19 * pixels[4 * at + 2]) >> 8;
    if (luma >= threshold) {
      const [x, y] = [at % width, Math.floor(at / width)];
      const [x0, y0, x1, y1] = found ?? [x, y, x, y];
      found = [Math.min(x0, x), Math.min(y0, y), Math.max(x1, x), Math.max(y1, y)];
    }
  }
  if (found === undefined) return undefined;
  const [x0, y0, x1, y1] = found;
  return [x0, y0, x1 - x0 + 1, y1 - y0 + 1];
}

export const node = defineNode({
  name: 'glow',
  version: '0.2.0',
  paramsSchema:
    '{"type":"object","properties":{"threshold":{"type":"integer","minimum":0,"maximum":255,"default":230},' +
    '"every":{"type":"integer","minimum":1,"default":30}},"additionalProperties":false}',

  shape() {
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .output(Output.rows('glows').schema(GLOW))
      .pure();
  },

  init({ threshold, every }, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    const width = video.width;
    // One frame of the picture, in its time base.
    const step = v.hint.rate === undefined ? 1 : Math.max(v.info.timeBase.pts(v.hint.rate.duration(1)), 1);
    return {
      process(tick, out) {
        const frame = tick.frame(v.id);
        if (frame === undefined) return;
        const pixels = tick.fetch(v.id, frame.index);
        const box = bright(pixels, width, threshold);
        if (box === undefined) return;
        const [x, y, w, h] = box;
        const into = tick.ordinal() % every;
        const glow = {
          start_t: tick.timeBase().seconds(frame.pts - into * step),
          id: Math.floor(tick.ordinal() / every),
          x,
          y,
          w,
          h,
        };
        out.row('glows', frame.pts, glow);
      },
    };
  },
});
