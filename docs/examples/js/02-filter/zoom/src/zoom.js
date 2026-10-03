import { defineNode, Input, Output, Shape } from '@ffrwd/node';
import { Filter, planes, Rect, Rgba } from '@ffrwd/node/frame';

/** What `planes` divides by to hand back eight-bit values unchanged. */
const EIGHT_BITS = { mean: [0, 0, 0], std: [1 / 255, 1 / 255, 1 / 255] };

/** The part of the picture that fills the frame: `1 / amount` of each
 * side, centred on `x`, `y` as far as the picture allows. */
function crop(width, height, { amount, x, y }) {
  const w = Math.max(Math.round(width / amount), 1);
  const h = Math.max(Math.round(height / amount), 1);
  const x0 = Math.trunc(Math.min(Math.max(x * width - w / 2, 0), width - w));
  const y0 = Math.trunc(Math.min(Math.max(y * height - h / 2, 0), height - h));
  return new Rect(x0, y0, x0 + w, y0 + h);
}

/** Planar red, green and blue back to opaque rgba. */
function interleave(planes, pixels) {
  const rgba = new Uint8Array(pixels * 4).fill(255);
  for (let at = 0; at < pixels; at += 1) {
    for (let channel = 0; channel < 3; channel += 1) {
      rgba[at * 4 + channel] = Math.min(Math.max(Math.round(planes[channel * pixels + at]), 0), 255);
    }
  }
  return rgba;
}

export const node = defineNode({
  name: 'zoom',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"amount":{"type":"number","minimum":1,"maximum":16,"default":2},' +
    '"x":{"type":"number","minimum":0,"maximum":1,"default":0.5},' +
    '"y":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false}',

  shape() {
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .output(Output.like('v'))
      .pure()
      .oneToOne();
  },

  init(params, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    const { width, height } = video;
    return {
      setParams(changed) {
        params = changed;
      },
      process(tick, out) {
        const frame = tick.frame(v.id);
        if (frame === undefined) return;
        if (params.amount === 1) return out.pass('v', v.id, frame);
        const picture = new Rgba(tick.fetch(v.id, frame.index), width, height);
        const rgb = planes(picture, crop(width, height, params), width, height, Filter.Bilinear, EIGHT_BITS);
        out.frame('v', frame.pts, frame.duration, interleave(rgb, width * height));
      },
    };
  },
});
