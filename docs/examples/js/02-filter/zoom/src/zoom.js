import { defineNode, Input, Output, Shape } from '@ffrwd/node';

/** The part of the picture that fills the frame: `1 / amount` of each
 * side, centred on `x`, `y` as far as the picture allows. */
function crop(width, height, { amount, x, y }) {
  const w = Math.max(Math.round(width / amount), 1);
  const h = Math.max(Math.round(height / amount), 1);
  const x0 = Math.trunc(Math.min(Math.max(x * width - w / 2, 0), width - w));
  const y0 = Math.trunc(Math.min(Math.max(y * height - h / 2, 0), height - h));
  return { x0, y0, x1: x0 + w, y1: y0 + h };
}

// Pillow's bilinear resize, written out here: it works in the same fixed
// point, pass for pass, as the ffrwd-frame crate the Rust example calls, so
// both modules make the same picture to the byte.

/** For each of `size` pixels made from `from`: the first pixel it reads, and
 * its weights in fixed point with `bits` fractional bits. */
function taps(from, size) {
  const scale = from / size;
  const stretch = Math.max(scale, 1);
  const kernels = [];
  for (let at = 0; at < size; at += 1) {
    const centre = (at + 0.5) * scale;
    let first = Math.max(Math.floor(centre - stretch), 0);
    const end = Math.min(Math.ceil(centre + stretch), from);
    const weights = [];
    for (let x = first; x < end; x += 1) {
      const weight = Math.max(0, 1 - Math.abs((x - (centre - 0.5)) * (1 / stretch)));
      if (weight === 0 && weights.length === 0) first += 1;
      else weights.push(weight);
    }
    const sum = weights.reduce((total, weight) => total + weight, 0);
    while (weights.at(-1) === 0) weights.pop();
    kernels.push({ first, weights: weights.map((weight) => (sum === 0 ? weight : weight / sum)) });
  }
  const most = Math.max(0, ...kernels.flatMap((kernel) => kernel.weights));
  let bits = 0;
  while (bits < 21 && Math.round(most * 2 ** (bits + 1)) < 2 ** 15) bits += 1;
  for (const kernel of kernels) kernel.weights = kernel.weights.map((weight) => Math.round(weight * 2 ** bits));
  return { kernels, bits };
}

/** `size` pixels across (or down) made from each row (or column) of an rgb
 * picture `width` x `height`, rounded back to eight bits. */
function pass(rgb, width, height, size, across) {
  const { kernels, bits } = taps(across ? width : height, size);
  const [w, h] = across ? [size, height] : [width, size];
  const out = new Uint8Array(w * h * 3);
  for (let y = 0; y < h; y += 1) {
    for (let x = 0; x < w; x += 1) {
      const { first, weights } = kernels[across ? x : y];
      for (let channel = 0; channel < 3; channel += 1) {
        let sum = 1 << (bits - 1);
        for (let n = 0; n < weights.length; n += 1) {
          const at = across ? (y * width + first + n) * 3 : ((first + n) * width + x) * 3;
          sum += rgb[at + channel] * weights[n];
        }
        out[(y * w + x) * 3 + channel] = Math.min(Math.max(sum >> bits, 0), 255);
      }
    }
  }
  return out;
}

/** `rect` of an rgba picture `stride` pixels wide, resized to `width` x
 * `height`: across first, then down, as Pillow does. Red, green and blue. */
function resize(pixels, stride, rect, width, height) {
  let [w, h] = [rect.x1 - rect.x0, rect.y1 - rect.y0];
  let rgb = new Uint8Array(w * h * 3);
  for (let y = 0; y < h; y += 1) {
    for (let x = 0; x < w; x += 1) {
      const [from, to] = [((rect.y0 + y) * stride + rect.x0 + x) * 4, (y * w + x) * 3];
      for (let channel = 0; channel < 3; channel += 1) rgb[to + channel] = pixels[from + channel];
    }
  }
  if (w !== width) [rgb, w] = [pass(rgb, w, h, width, true), width];
  if (h !== height) [rgb, h] = [pass(rgb, w, h, height, false), height];
  return rgb;
}

/** Red, green and blue back to opaque rgba. */
function interleave(rgb) {
  const pixels = new Uint8Array((rgb.length / 3) * 4).fill(255);
  for (let at = 0; at < rgb.length / 3; at += 1) {
    for (let channel = 0; channel < 3; channel += 1) pixels[at * 4 + channel] = rgb[at * 3 + channel];
  }
  return pixels;
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
        const pixels = tick.fetch(v.id, frame.index);
        const zoomed = interleave(resize(pixels, width, crop(width, height, params), width, height));
        out.frame('v', frame.pts, frame.duration, zoomed);
      },
    };
  },
});
