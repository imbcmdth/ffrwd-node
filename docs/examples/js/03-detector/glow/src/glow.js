import { defineNode, Input, Output, Shape, SPAN, Spans } from '@ffrwd/node';

const GLOW = { ...SPAN, x: 'integer', y: 'integer', w: 'integer', h: 'integer' };

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
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"threshold":{"type":"integer","minimum":0,"maximum":255,"default":230},' +
    '"gap":{"type":"integer","minimum":0,"default":2}},"additionalProperties":false}',

  shape() {
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .output(Output.rows('glows').schema(GLOW));
  },

  init({ threshold, gap }, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    const width = video.width;
    const spans = new Spans().gap(gap);
    return {
      process(tick, out) {
        const frame = tick.frame(v.id);
        if (frame === undefined) return;
        spans.tick(tick.timeBase().seconds(frame.pts));
        const pixels = tick.fetch(v.id, frame.index);
        const box = bright(pixels, width, threshold);
        if (box === undefined) return;
        const [x, y, w, h] = box;
        out.row('glows', frame.pts, { ...spans.see(), x, y, w, h });
      },
    };
  },
});
